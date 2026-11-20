# Repeated fields as spans in an arena

Date: 2026-11-20. Status: draft for approval. This document changes the type mapping, the public API and the decode rules of `2026-11-18-micropb-design.md`, the sequence descriptors of `2026-11-18-micropb-table-codec-design.md`, and the options file of `2026-11-19-protoc-plugin-design.md` (also its section "Changes: `type:FT_POINTER`").

## Intent

A repeated field is `pb::repeated<T, N>` today. Its storage is in the struct and has the size of the largest message. A message with nested repeated fields thus becomes very large: the `ApToHost` message of a device project is 11024 bytes on the Arm GNU Toolchain 14.2, and most of it is not used. Each repeated field must also have a `max_count` in the options file.

The user wants a repeated field without a size to be a `std::span<const T>` for encode and for decode. To decode, the caller gives a memory area, `pb::arena`. `decode` puts the items of each such field there, in one contiguous block for each field. Then the struct has only the size of its views, and the options file needs no sizes. A field with a size in the options file stays `pb::repeated<T, N>`, with its storage in the struct and no arena.

The library is not used by other projects yet. The user allows changes to all compatibility contracts in this design.

## Goals

* A repeated field can be `std::span<const T>` for encode and for decode, in each position: a plain member, an element of a repeated field, a value of a `std::optional`, a member of a oneof alternative.
* `decode` makes the items of each `std::span<const T>` field in a caller-supplied `pb::arena` and sets the span to them. The items of one field are contiguous.
* `pb::repeated<T, N>` stays as it is: storage in the struct, `CAPACITY_EXCEEDED` when it is full. A message that has only `pb::repeated` fields needs no arena.
* An arena that is too small for the message is a run-time error, `pb::error::ARENA_FULL`. `decode` never writes outside the arena and never drops an item without an error.
* The arena use depends on the message content. The order of the fields on the wire changes only the alignment padding between blocks, at most `alignof( std::max_align_t ) - 1` bytes for each block. (Exception: a nested message that occurs more than once, see "Merge".) The caller measures `used()` and adds a margin; the README states this. (Builder's call: exact use for each order needs a fixed alignment for each block, which costs more RAM on the device.)
* The plugin writes `pb::repeated<T, N>` for a field with `max_count` or `max_size`, and `std::span<const T>` for a field without. A repeated field without `max_count` is no longer an error.

## Non-goals

* A function that gives the arena size for a message before `decode` (`pb::arena_size`). The caller finds the size by test with `arena.used()`. It can come later, if necessary.
* PMR (`std::pmr::memory_resource`): `<memory_resource>` is not in the freestanding subset, and `monotonic_buffer_resource` throws `std::bad_alloc` when it is full. With `-fno-exceptions`, that stops the program.
* A change to the wire format. The bytes stay identical to `protoc`.

## Anti-goals

* A write outside the arena or outside a block, also for input that is not valid or that an attacker made. The bound of a block is examined at each item, not only calculated in advance.
* A silent loss of items when the arena is full.
* A table or a static object that needs initialization at run time (the test `no_startup_code`).
* A second interpreter path for the count. The count uses the reader of the decoder.

## Approaches

How `decode` knows the count of a repeated field before it fills a contiguous block. Protobuf sends no count, and the records of one field can be mixed with other fields.

* **A. Count in the message before the fill.** Selected by the user. `decode` reads the keys of the message and skips the values. It counts the elements of the field. Then it takes one block from the arena and fills it.
* B. One sizing pass over the whole input before `decode`. Rejected: the fill also needs the count of each field, so it must count again or keep a table of counts.
* C. No count: add items at the end of the arena, and move the block when a nested message put a block after it. Rejected: a repeated message with its own repeated fields moves the parent block for each element. The arena waste increases as the square of the count and depends on the order of the fields.

## Design

### `pb::arena`

In `types.hpp`:

```cpp
namespace pb {

/// Memory for the items of repeated fields that `decode` makes. The caller owns
/// the storage. The arena gives out aligned blocks from the start of the storage
/// and never frees one block: `clear()` frees all. Does not allocate.
struct arena
{
	constexpr explicit arena( std::span<std::uint8_t> storage );

	/// Bytes that the blocks use, with the padding for alignment.
	[[nodiscard]] constexpr std::size_t used() const;
	[[nodiscard]] constexpr std::size_t capacity() const;

	/// Frees all blocks. Each message that `decode` wrote with this arena is then not valid.
	constexpr void clear();

	/// A block of `size` bytes at a multiple of `alignment`, or null when the
	/// storage is too small. `alignment` is a power of 2.
	[[nodiscard]] void* allocate( std::size_t size, std::size_t alignment );

private:
	std::span<std::uint8_t> _storage;
	std::size_t _used = 0;
};

} // namespace pb
```

* `allocate` aligns the address, not the offset in the storage. Thus the caller can give storage at any address.
* `decode` does not call `clear()`. The caller decides when the decoded messages are no longer necessary. Thus more than one message can use one arena.
* On error, the blocks that `decode` took stay used until `clear()`.

### Public API

```cpp
enum class error {
	BUFFER_TOO_SMALL,
	TRUNCATED,
	INVALID_VARINT,
	INVALID_WIRE_TYPE,
	INVALID_TAG,
	/// decode: a field has more elements or bytes than its `pb::repeated` or `std::array` holds.
	CAPACITY_EXCEEDED,
	/// decode: the arena has no space for the items of a repeated field.
	ARENA_FULL,
};

template <typename T>
[[nodiscard]] std::expected<std::size_t, error> decode( std::span<const std::uint8_t> in, T& msg, arena& memory );

template <typename T>
[[nodiscard]] std::expected<std::size_t, error> decode( std::span<const std::uint8_t> in, T& msg );
```

* The overload without an arena is for a message that has no `std::span<const T>` repeated field, also not in a nested message. For a message with such a field it is a compile error with the new rule `pb: decode_needs_arena:`.
* `encode` and `encoded_size` do not change.
* After `decode`, `msg` points into `in` (strings, bytes) and into the arena (repeated fields). Both must stay valid while `msg` is in use.

### Type mapping

| C++ type | Proto3 | Encode | Decode |
| --- | --- | --- | --- |
| `std::span<const T>` | `repeated T` | yes | yes, items in the arena |
| `pb::repeated<T, N>` | `repeated T` | yes | yes, items in the struct |
| `std::array<T, N>` | `repeated T` | yes | no (`decode_encode_only`) |
| `std::span<const std::uint8_t>` | `bytes` | yes | yes, a view into the input |
| `pb::repeated<std::uint8_t, N>` | `bytes` | yes | yes, a copy in the struct |
| `std::array<std::uint8_t, N>` | `bytes`, fixed length | yes | yes |

* Removed: `pb::span_buffer<T>`. The arena does its job, also in the positions where a `span_buffer` is not allowed today.
* A `std::span<T>` with a non-const `T`, or with a static extent, stays not mapped (`unmapped_type`) or encode-only (`decode_fixed_span`) as today.
* `std::span<const std::span<const std::uint8_t>>` is `repeated bytes`. Its items are views into the input, in a block in the arena.

### Rules

* Removed: `span_buffer_in_optional`, `span_buffer_in_variant`, `span_buffer_in_repeated`, and their tests in `tests/compile_fail/`. The arena makes the storage for a span in each position.
* Changed: the text of `decode_encode_only` names only `std::array`, and tells to use `pb::repeated` or `std::span<const T>`.
* New: `decode_needs_arena` (see "Public API"), with a test in `tests/compile_fail/`.
* New: `recursive_message`. A span makes a type that contains itself possible (`struct node { std::span<const node> children; };`). The library rejects it, directly or through other types, with a test in `tests/compile_fail/`. Thus the recursion depth of `decode` keeps its bound at compile time, and the input cannot overflow the stack of the device. The plugin keeps its error for a message that contains itself. (Builder's call: recursive messages need a run-time depth limit and a new error; not asked for.)
* The other rules stay. A repeated field still cannot be a oneof alternative, the value of a `std::optional`, or the element of a repeated field.

### Descriptors

* `sequence_storage` keeps `REPEATED` and `ARRAY`. `ACCESSOR` serves only `std::span<const T>`, because `pb::span_buffer` goes away.
* The span accessor gets a second operation that sets the span to a block and a count. The interpreter does not assume the layout of `std::span`.
* `sequence_desc` gets the alignment of the element.
* The type of the element must be trivially copyable and trivially destructible: the arena never calls a destructor, and a merge copies items with `memcpy`. All mapped types are. A `static_assert` in the descriptor examines it.

### Count and fill

`decode` resets `msg` first: each span becomes empty. A `pb::repeated` field fills as today. For a record of a `std::span<const T>` field:

1. If the block of the field has space for the elements of this record, `decode` adds them.
2. Else `decode` counts the elements of this field from this record to the end of the current message: the records of the field number, which it finds from the keys; it skips all other values. Then it takes a new block for the old items and the counted items, copies the old items, and adds the elements of this record.

Thus the first record of a field in a message counts the field one time, and each later record of the field in the same message has space.

The count of the elements in one record:

* A scalar element with the wire type of the element: 1.
* A scalar element in a length-delimited record (packed): for a fixed-width element, the length divided by the element size; for a varint, the number of bytes without bit 7.
* A string, bytes or message element in a length-delimited record: 1.
* A record with a different wire type: 0. `decode` skips it, as today.

The block keeps its capacity in a `std::size_t` in the arena, just before the items. Each add examines it, and a full block goes to step 2. Thus a count that is too small, for example for input that is not valid, cannot cause a write outside the block. If the block has no space after step 2, the input is not valid: `decode` stops with the error of the reader, or with `TRUNCATED` when the reader has none.

A nested message counts its own repeated fields when `decode` reads it. An element of a repeated message field is a nested message.

### Merge

Protobuf merges a nested message that occurs more than once in one message, and a repeated field of it gets the items of all occurrences. The second occurrence finds a full block and makes a new block with a copy of the old items (step 2). The old block stays used until `clear()`. `protoc` does not write a nested message two times, so this case occurs only with input that a program assembles from parts.

A oneof that changes to a different alternative resets the alternative. The blocks of the old alternative stay used until `clear()`.

### Plugin

* A repeated field without `max_count` is `std::span<const T>`. Before, it was an error.
* All other options keep their result: `max_count:N` gives `pb::repeated<T, N>`, `max_size:N` gives `pb::repeated<std::uint8_t, N>`, `fixed_count` and `fixed_length` give `std::array`. A `bytes` field without `max_size` stays `std::span<const std::uint8_t>`.
* `type:FT_POINTER` stays as in "Changes: `type:FT_POINTER`" of the plugin design: it gives the span also when a wildcard line sets `max_count` or `max_size`. Its decode is now possible with an arena. `FT_STATIC` and `FT_DEFAULT` give the result of the other options. `FT_CALLBACK` and `FT_IGNORE` stay errors.
* `int_size:IS_8` on a repeated unsigned field stays an error: `std::span<const std::uint8_t>` is `bytes`.
* The generated headers in `tests/` are made again with the plugin.

## Tests

* The tests that use `pb::span_buffer` change to `std::span<const T>` and an arena. The tests of `pb::repeated` stay. The reference vectors in `tests/vectors/` do not change.
* `test_wire_compat.cpp` and `test_generated.cpp`: at least one message decodes with spans and an arena, and its bytes agree with the reference vectors.
* `test_types.cpp`: `pb::arena` with `static_assert` tests where it is `constexpr`, and run-time tests for alignment and for a full arena.
* `test_decode.cpp`, new cases:
    * An arena that is exactly large enough decodes the message. An arena that is 1 byte smaller gives `ARENA_FULL`.
    * The records of two repeated fields mixed with each other.
    * Packed and unpacked records of one scalar field, mixed.
    * A repeated message whose elements have repeated fields.
    * A nested message that occurs two times (merge with copy).
    * A oneof alternative and a `std::optional` with a message that has a repeated field.
    * Elements with different alignment in one arena (`std::uint8_t`, then `std::uint64_t`).
    * Input that is not valid: a packed varint record whose last varint has no end, a packed fixed record whose length is not a multiple of the element size. No write outside the block (the sanitizer build examines it).
    * For two encodings of one message with a different order of the fields, `used()` differs by no more than the padding bound of "Goals".
* `tests/compile_fail/decode_needs_arena.cpp`, `tests/compile_fail/recursive_message.cpp` (direct, and through a second type).
* `test_plugin.py`: a repeated field without `max_count` gives `std::span<const T>`; the test that expects an error for it changes.
* All tests with reflection and with `-DMICROPB_REFLECTION=OFF`, and the Arm compile tests.

### Size report

The structs of `tests/size/size_report.cpp` keep `pb::repeated`, so the numbers before and after the change compare the same structs. The report gives both, also when they become larger. Today: 16 types 9400 bytes, 32 types 13736 bytes, one more type 271 bytes. A second measurement with one span field in each struct shows the cost of the count and the arena.

## Documents

* `README.md`: the type mapping, the API (`pb::arena`, `decode`), the errors, the options file, the lifetime of a decoded message.
* `2026-11-18-micropb-design.md`, `2026-11-18-micropb-table-codec-design.md`, `2026-11-19-protoc-plugin-design.md`: a short section "Changes: arena spans" that names this document.
* `CLAUDE.md`, section "Code", names `pb::span_buffer<T>` as storage from the caller. It needs a change of one sentence (`pb::arena`). The user decides on it.

## Left to the builder

* The names of the internal functions and the place of the count function in `codec.hpp`.
* Whether `std::array<T, N>` with a scalar or message element stays encode-only or also decodes. Not asked for; the default is no change.
* The order of the commits: a `refac` commit for the arena type alone is possible before the functional commit.

## Changes during the implementation

* **Size report** (GCC 16.2, `-Os`, x86-64, the structs with `pb::repeated`): 16 types 9400 -> 9920 bytes, 32 types 13736 -> 14256 bytes, one more type 271 -> 271 bytes. The count and the new block are reached only through a pointer in the descriptor of a span (`reserve`), so a program without a span field does not contain them. Of the 520 bytes that stay (`nm`, 32 types): `decode_message` +144, `decode_element` +128, `decode_repeated` +80, `decode`, `decode_value` and `reset_value` +16 each; 120 bytes are in other code and in the descriptors (not broken down). Before the pointer, the report was 10616 and 14952 bytes.
* **Size with spans:** the same structs with `std::span<const T>` for each `pb::repeated<T, N>` whose `T` is not `std::uint8_t`, decoded with an arena: 16 types 10788 bytes, 32 types 14628 bytes, one more type 240 bytes (without the pointer: 10724, 14564). The arena storage is not in these numbers.
* **The check for the record:** for each record of a span field, `decode` counts the elements of the record and compares the count with the free items of the block. Only when they do not fit, it counts the field to the end of the message.
* **Block header:** the capacity is in the last bytes of a header of `max( sizeof( std::size_t ), alignof( T ) )` bytes, rounded up to the alignment. For an element of 1 byte, the header is 8 bytes on x86-64 and 4 bytes on Arm.
* **New rule `span_element_trivial`:** the element of a `std::span<const T>` repeated field must be trivially copyable and trivially destructible. The `static_assert` has a rule name and a test in `tests/compile_fail/`. It is in `check_member_type` of `meta_schema.hpp` (with the other type rules), not in the descriptor. A message type with a destructor is an aggregate, so a mapped type can fail this rule. The rule applies also to `encode`.
* **`decode_fixed_span`** now also rejects a repeated field in a `std::span` with a static extent. Before, `decode_encode_only` rejected it. Its text for a repeated field says to use `std::span<const T>` or `pb::repeated` (test `tests/compile_fail/list_decode_fixed_span.cpp`). `needs_arena` is `false` for such a span.
* **A varint without an end in a packed record:** a span field gives `TRUNCATED` (the block is full before the read). A `pb::repeated` field gives the error of the reader, for example `INVALID_VARINT` for more than 10 bytes.
* `pb::span_buffer` and its rules stay until the next task of the plan.
* **Reset of a new value** (after the review): a value that `decode` makes for a `std::optional` (`ENGAGE` of an empty optional) or for a new `oneof` alternative gets `reset_value` before `decode` reads into it. Before, it kept its default member initializers. A span with a default member initializer then pointed to caller data without a block, and `decode` read a capacity before that data and wrote past it. Now each span that `decode` sees is empty or bound by `decode`. A default member initializer in such a value has no effect after `decode`: a field that is not in the input is zero or empty, as for the members of `msg`.
* **Construction of new items** (after the review): a new block in the arena has no objects, and `reset_value` of a message element reads the flag of a `std::optional` and the index of a `std::variant` before it writes. The sequence descriptor of a span has a constructor (`::new ( item ) T()`) that makes each new item before the reset. The copy into a new block (merge) is a `memcpy`: it makes the objects, because each element type is implicit-lifetime (a scalar, a class with a trivial copy constructor and a trivial destructor, or an aggregate without a user-provided destructor). The tests run the arena tests also with storage that holds 0xA5.
* **Hardened bound:** a span with more items than its block capacity gives `ARENA_FULL` in the count, and a full block is `count >= capacity`.
