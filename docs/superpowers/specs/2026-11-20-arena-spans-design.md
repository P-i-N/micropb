# Repeated fields as spans in an arena

Date: 2026-11-20. Status: draft for approval. This document changes the type mapping, the public API and the decode rules of `2026-11-18-micropb-design.md`, the sequence descriptors of `2026-11-18-micropb-table-codec-design.md`, and the options file of `2026-11-19-protoc-plugin-design.md` (also its section "Changes: `type:FT_POINTER`").

## Intent

A repeated field is `pb::repeated<T, N>` today. Its storage is in the struct and has the size of the largest message. A message with nested repeated fields thus becomes very large: the `ApToHost` message of a device project is 11024 bytes on the Arm GNU Toolchain 14.2, and most of it is not used. Each repeated field must also have a `max_count` in the options file.

The user wants a repeated field to be a `std::span<const T>` for encode and for decode. To decode, the caller gives a memory area, `pb::arena`. `decode` puts the items of each repeated field there, in one contiguous block for each field. Then the struct has only the size of its views, and the options file needs no sizes.

The library is not used by other projects yet. The user allows changes to all compatibility contracts in this design.

## Goals

* Each repeated field is `std::span<const T>`, in each position: a plain member, an element of a repeated field, a value of a `std::optional`, a member of a oneof alternative.
* `decode` makes the items of each repeated field in a caller-supplied `pb::arena` and sets the span to them. The items of one field are contiguous.
* An arena that is too small for the message is a run-time error, `pb::error::ARENA_FULL`. `decode` never writes outside the arena and never drops an item without an error.
* The arena use depends on the message content. The order of the fields on the wire changes only the alignment padding between blocks, at most `alignof( std::max_align_t ) - 1` bytes for each block. (Exception: a nested message that occurs more than once, see "Merge".) The caller measures `used()` and adds a margin; the README states this. (Builder's call: exact use for each order needs a fixed alignment for each block, which costs more RAM on the device.)
* The plugin writes a `std::span<const T>` for each repeated field. The options file needs no `max_count` and no `max_size`.

## Non-goals

* A function that gives the arena size for a message before `decode` (`pb::arena_size`). The caller finds the size by test with `arena.used()`. It can come later, if necessary.
* Storage of fixed size in the struct (`pb::repeated<T, N>`): removed by decision of the user.
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
	/// decode: a `bytes` field is longer than its `std::array`.
	CAPACITY_EXCEEDED,
	/// decode: the arena has no space for the items of a repeated field.
	ARENA_FULL,
};

template <typename T>
[[nodiscard]] std::expected<std::size_t, error> decode( std::span<const std::uint8_t> in, T& msg, arena& memory );

template <typename T>
[[nodiscard]] std::expected<std::size_t, error> decode( std::span<const std::uint8_t> in, T& msg );
```

* The overload without an arena is for a message that has no repeated field, also not in a nested message. For a message with a repeated field it is a compile error with the new rule `pb: decode_needs_arena:`.
* `encode` and `encoded_size` do not change.
* After `decode`, `msg` points into `in` (strings, bytes) and into the arena (repeated fields). Both must stay valid while `msg` is in use.

### Type mapping

| C++ type | Proto3 | Encode | Decode |
| --- | --- | --- | --- |
| `std::span<const T>` | `repeated T` | yes | yes, items in the arena |
| `std::array<T, N>` | `repeated T` | yes | no (`decode_encode_only`) |
| `std::span<const std::uint8_t>` | `bytes` | yes | yes, a view into the input |
| `std::array<std::uint8_t, N>` | `bytes`, fixed length | yes | yes |

* Removed: `pb::repeated<T, N>` and `pb::span_buffer<T>`, also as `bytes` with `std::uint8_t` elements.
* A `std::span<T>` with a non-const `T`, or with a static extent, stays not mapped (`unmapped_type`) or encode-only (`decode_fixed_span`) as today.
* `std::span<const std::span<const std::uint8_t>>` is `repeated bytes`. Its items are views into the input, in a block in the arena.

### Rules

* Removed: `span_buffer_in_optional`, `span_buffer_in_variant`, `span_buffer_in_repeated`, and their tests in `tests/compile_fail/`. The arena makes the storage for a span in each position.
* Changed: the text of `decode_encode_only` names only `std::array`.
* New: `decode_needs_arena` (see "Public API"), with a test in `tests/compile_fail/`.
* New: `recursive_message`. A span makes a type that contains itself possible (`struct node { std::span<const node> children; };`). The library rejects it, directly or through other types, with a test in `tests/compile_fail/`. Thus the recursion depth of `decode` keeps its bound at compile time, and the input cannot overflow the stack of the device. The plugin keeps its error for a message that contains itself. (Builder's call: recursive messages need a run-time depth limit and a new error; not asked for.)
* The other rules stay. A repeated field still cannot be a oneof alternative, the value of a `std::optional`, or the element of a repeated field.

### Descriptors

* `sequence_storage` has two values: `ARRAY` (`std::array`, encode only) and `SPAN` (`std::span<const T>`). `REPEATED` and the count offset go away.
* The span accessor gets a second operation that sets the span to a block and a count. The interpreter does not assume the layout of `std::span`.
* `sequence_desc` gets the alignment of the element.
* The type of the element must be trivially copyable and trivially destructible: the arena never calls a destructor, and a merge copies items with `memcpy`. All mapped types are. A `static_assert` in the descriptor examines it.

### Count and fill

`decode` resets `msg` first: each span becomes empty. Then, for a record of a repeated field:

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

* Each repeated field is `std::span<const T>`. A field without `max_count` is no longer an error.
* `bytes` is always `std::span<const std::uint8_t>`, or `std::array<std::uint8_t, N>` with `max_size:N fixed_length:true`.
* `max_count:N fixed_count:true` stays: `std::array<T, N>`, encode only.
* `max_count` without `fixed_count`, and `max_size` without `fixed_length`, do not apply: a warning, as for each option that does not apply.
* `type:FT_POINTER`, `FT_STATIC` and `FT_DEFAULT` have no effect, because a repeated field is always a span. The plugin accepts them without a warning, so that a NanoPB file works. `FT_CALLBACK` and `FT_IGNORE` stay errors.
* `int_size:IS_8` on a repeated unsigned field stays an error: `std::span<const std::uint8_t>` is `bytes`.
* The generated headers in `tests/` are made again with the plugin.

## Tests

* The tests that use `pb::repeated` or `pb::span_buffer` change to `std::span<const T>` and an arena. The reference vectors in `tests/vectors/` do not change.
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
* `test_plugin.py`: repeated fields without `max_count`; the warnings for `max_count` and `max_size`; `type` without effect.
* All tests with reflection and with `-DMICROPB_REFLECTION=OFF`, and the Arm compile tests.

### Size report

The structs of `tests/size/size_report.cpp` change from `pb::repeated` to `std::span<const T>`. The report gives the numbers before and after the change, also when they become larger. Today: 16 types 9400 bytes, 32 types 13736 bytes, one more type 271 bytes.

## Documents

* `README.md`: the type mapping, the API (`pb::arena`, `decode`), the errors, the options file, the lifetime of a decoded message.
* `2026-11-18-micropb-design.md`, `2026-11-18-micropb-table-codec-design.md`, `2026-11-19-protoc-plugin-design.md`: a short section "Changes: arena spans" that names this document.
* `CLAUDE.md`, section "Code", names `pb::repeated<T, N>` and `pb::span_buffer<T>` as the storage. It needs a change of one sentence. The user decides on it.

## Left to the builder

* The names of the internal functions and the place of the count function in `codec.hpp`.
* Whether `std::array<T, N>` with a scalar or message element stays encode-only or also decodes. Not asked for; the default is no change.
* The order of the commits: a `refac` commit for the arena type alone is possible before the functional commit.
