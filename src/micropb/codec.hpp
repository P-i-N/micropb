#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <expected>
#include <new>
#include <optional>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <string_view>
#include <type_traits>
#include <utility>

#include <micropb/schema.hpp>
#include <micropb/types.hpp>
#include <micropb/wire.hpp>

// The codec has two parts.
// Part 1 makes descriptors: constant tables that say where each field of a
// message is and how to read and write it. Templates make them at compile
// time from the field list of each message type (`pb::fields_of`).
// Part 2 is the interpreter: functions that are not templates. They encode,
// decode and reset each message type from its descriptors.

namespace pb {

namespace detail {

////////////////////////////////////////////////////////////////////////////////
// Part 1: descriptors

/// A scalar code tells the interpreter how to load and store a scalar. The low
/// two bits are log2 of the size in bytes. All codes fit in `0..19` so every
/// scalar fits in a 1-halfword (5-bit op) field entry.
inline constexpr uint8_t scalar_size_mask = 0x03;
/// The type has a sign: the loaded value sign-extends to 64 bits.
inline constexpr uint8_t scalar_signed = 0x04;
/// `bool`: 1 byte (`code == scalar_bool`).
inline constexpr uint8_t scalar_bool = 0x08;
/// Unsigned 32-bit enum (`code == scalar_enum`): `int32` on the wire, sign-extends bit 31.
inline constexpr uint8_t scalar_enum = 0x0A;
/// `sint8`..`sint64`: ZigZag varint (always combined with `scalar_signed`, so `0x0C..0x0F`).
inline constexpr uint8_t scalar_zigzag = 0x0C;
/// Fixed width on the wire: 4 bytes (`0x12`) or 8 bytes (`0x13`).
inline constexpr uint8_t scalar_fixed = 0x10;

[[nodiscard]] constexpr bool is_scalar_zigzag( uint8_t code )
{
	return ( code & scalar_zigzag ) == scalar_zigzag;
}

template <typename T, int_encoding Encoding = int_encoding::VARINT>
[[nodiscard]] consteval uint8_t scalar_code_of()
{
	constexpr uint8_t size = sizeof( T ) == 1 ? 0 : sizeof( T ) == 2 ? 1 : sizeof( T ) == 4 ? 2 : 3;

	if constexpr ( std::is_same_v<T, bool> )
		return scalar_bool;
	else if constexpr ( std::is_enum_v<T> )
	{
		if constexpr ( std::is_signed_v<std::underlying_type_t<T>> )
			return size | scalar_signed;
		else if constexpr ( sizeof( T ) == 4 )
			return scalar_enum;
		else
			return size;
	}
	else if constexpr ( Encoding == int_encoding::ZIGZAG )
		return size | scalar_zigzag;
	else if constexpr ( Encoding == int_encoding::FIXED || wire_type_of<T>() != wire_type::VARINT )
		return size | scalar_fixed;
	else
		return size | ( std::is_signed_v<T> ? scalar_signed : 0 );
}

struct sequence_desc;
struct optional_desc;

/// How to read and write a value of one type. All members, elements, optional
/// values and alternatives of the same type share one descriptor.
struct value_desc
{
	field_kind kind;
	/// SCALAR: the scalar code. MESSAGE: the number of distinct compound types in `table`.
	/// ONEOF: size of `_index` in bytes (1 or 2).
	uint8_t scalar;
	/// MESSAGE: the number of `uint32_t` words in `table`.
	/// ONEOF: byte offset of `_index` inside `pb::oneof`.
	uint16_t words;
	union
	{
		/// FIXED_BYTES: the number of bytes. 0 for SCALAR.
		uint32_t size;
		/// MESSAGE: `message_table_data`, or null when `words == 0`.
		const void* table;
		/// REPEATED, BYTES and `pb::string`. Null for the views `std::string_view` and `std::span<const uint8_t>`.
		const sequence_desc* sequence;
		/// OPTIONAL.
		const optional_desc* optional;
		/// ONEOF: array of `const value_desc*` for alternatives `1..K` (at index `alt - 1`).
		const value_desc* const* oneof;
	};
};

// Each field of a message encodes into 1, 2, or (under `MICROPB_WIDE_TABLES`) 5
// `uint16_t` halfwords, sorted in ascending field number:
// - 1 halfword (bit 15 == 0): `[14:12 tag_delta - 1 (1..8)] [11:5 offset (0..127)] [4:0 op (0..31)]`
//     op 0..19:   inline SCALAR (`op` is the scalar code)
//     op 20:      inline STRING (`std::string_view`)
//     op 21:      inline BYTES view (`std::span<const uint8_t>`)
//     op 22..31:  compound member at `subs[op - 22]`
// - 2 halfwords (bit 15 == 1, `op != 127`): 31 bits across `(w0 << 16) | w1`:
//     `[30:24 op (0..126)] [23:12 offset (0..4095)] [11:0 number (1..4095)]`
//     op 22..94:  compound member at `subs[op - 22]`
//     op 95..126: ONEOF alternative `op - 94` (1..32) on `subs[0]`
// - 5 halfwords (`(w0 >> 8) == 0xFFu`, requires `MICROPB_WIDE_TABLES`):
//     w0: `0xFF00u | base_op`
//     w1: `number & 0xFFFFu`
//     w2: `number >> 16`
//     w3: `offset`
//     w4: `(sub_idx << 8) | alternative`
inline constexpr uint8_t op_string = 20;
inline constexpr uint8_t op_bytes_view = 21;
inline constexpr uint8_t op_sub_base = 22;
inline constexpr uint8_t op_short_sub_limit = 10;
inline constexpr uint8_t op_sub_limit = 73;
inline constexpr uint8_t op_oneof_base = 95;
inline constexpr uint8_t op_oneof_max_alt = 32;
inline constexpr uint8_t op_wide = 127;

/// Unpacked entry of a message field table.
struct field_entry
{
	uint32_t number;
	uint16_t offset;
	/// 0 for a plain member, else the 1-based index of the `std::variant` alternative.
	uint16_t alternative;
	/// Index into `subs` when `base_op == op_sub_base`.
	uint16_t sub_idx;
	uint8_t base_op;
};

/// The items of a sequence at this moment.
struct sequence_view
{
	void* data;
	size_t count;
};

using sequence_accessor = sequence_view ( * )( void* sequence );

/// Sets the sequence to `count` items at `data`.
using sequence_binder = void ( * )( void* sequence, void* data, size_t count );

/// Makes a value-initialized element at `item`.
using item_constructor = void ( * )( void* item );

/// Makes space in the arena block of a span for the elements of a record (`reserve`).
using sequence_reserver =
  bool ( * )( reader& in, field_key key, const sequence_desc& sequence, void* value, arena* memory );

enum class sequence_storage : uint8_t
{
	/// `pb::repeated`, `pb::bytes`, `pb::string` with a `uint8_t` count (`max_size <= 255`).
	REPEATED_8,
	/// `pb::repeated`, `pb::bytes`, `pb::string` with a `uint16_t` count (`max_size <= 65535`).
	REPEATED_16,
	/// `std::array`: the items are at offset 0, the count is `capacity`.
	ARRAY,
	/// `std::span<const T>`: `span->access` gives the items.
	ACCESSOR,
};

/// Callbacks and alignment of a `std::span` sequence. Only span types make one.
struct span_desc
{
	sequence_accessor access;
	/// `std::span<const T>`: `decode` sets it to a block in the arena. Null for a fixed span.
	sequence_binder bind;
	/// `std::span<const T>`: makes each new item in the arena. Null when encode-only.
	item_constructor construct;
	/// `std::span<const T>`: `reserve`. Null for a fixed span: then a program
	/// without a dynamic span field does not contain the count.
	sequence_reserver reserve;
	uint16_t element_align;
};

struct sequence_desc
{
	const value_desc* element;
	/// Non-null only for `sequence_storage::ACCESSOR`.
	const span_desc* span;
	uint16_t element_size;
	uint16_t capacity;
	uint16_t count_offset;
	sequence_storage storage;
};

enum class optional_op : uint8_t
{
	/// Gives the address of the value, or null when there is no value.
	GET,
	/// Makes a value-initialized value if there is none. Gives its address.
	ENGAGE,
	/// Removes the value. Gives null.
	RESET,
};

using optional_accessor = void* ( * )( void* optional, optional_op op );

struct optional_desc
{
	const value_desc* value;
	optional_accessor access;
};

static_assert( sizeof( value_desc ) <= 2 * sizeof( void* ) );

template <typename T, int_encoding Encoding = int_encoding::VARINT>
struct value_descriptor
{
	static const value_desc value;
};

////////////////////////////////////////////////////////////////////////////////

/// The count member of `pb::repeated` is private. The codec reads and writes it
/// through this friend of the type.
struct sequence_access
{
	template <typename S>
	static constexpr size_t items_offset = offsetof( S, _items );
	template <typename S>
	static constexpr size_t count_offset = offsetof( S, _count );
	template <typename S>
	static constexpr size_t count_size = sizeof( typename S::count_type );
};

/// The storage and index members of `pb::oneof` are private. The codec reads
/// and writes them through this friend of the type.
struct oneof_access
{
	template <typename O>
	static constexpr size_t storage_offset = offsetof( O, _storage );
	template <typename O>
	static constexpr size_t index_offset = offsetof( O, _index );
	template <typename O>
	static constexpr size_t index_size = sizeof( typename O::index_type );
};

/// Accessor of `std::span<const T>`. The span keeps its items in a different
/// object, so the interpreter cannot find the items from an offset.
template <typename S>
[[nodiscard]] sequence_view access_sequence( void* sequence )
{
	S& items = *static_cast<S*>( sequence );
	const void* data = items.data();
	return { const_cast<void*>( data ), items.size() };
}

/// Binder of `std::span<const T>`.
template <typename S>
void bind_span( void* sequence, void* data, size_t count )
{
	*static_cast<S*>( sequence ) = S( static_cast<const element_t<S>*>( data ), count );
}

/// Constructor of the element of `std::span<const T>`. A new block in the arena has
/// no objects, and `reset_value` reads a `std::optional` or `std::variant` before
/// it writes.
template <typename E>
void construct_item( void* item )
{
	::new ( item ) E();
}

/// Accessor of `std::optional`. The codec cannot make a value with a `const`
/// member again in its place. Such a value type is encode-only
/// (`check_decodable`), and `ENGAGE` does nothing for it.
template <typename O>
[[nodiscard]] void* access_optional( void* optional, optional_op op )
{
	O& value = *static_cast<O*>( optional );
	if ( op == optional_op::RESET )
	{
		if constexpr ( std::is_trivially_destructible_v<O> )
			::new ( optional ) O();
		else
			value.reset();
		return nullptr;
	}
	if ( op == optional_op::ENGAGE && !value )
	{
		if constexpr ( requires { value.emplace(); } )
			value.emplace();
	}
	return value ? &*value : nullptr;
}

////////////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline bool reserve(
  reader& in, field_key key, const sequence_desc& sequence, void* value, arena* memory );

template <typename S>
[[nodiscard]] consteval span_desc describe_span()
{
	using element = element_t<S>;
	constexpr auto align = static_cast<uint16_t>( alignof( element ) );
	if constexpr ( !is_fixed_span<S> )
	{
		// An element without a default constructor, for example with a `const`
		// member, is encode-only (`decode_const_member`): it gets no constructor.
		item_constructor construct = nullptr;
		if constexpr ( std::is_default_constructible_v<element> )
			construct = construct_item<element>;
		return { access_sequence<S>, bind_span<S>, construct, reserve, align };
	}
	else
	{
		return { access_sequence<S>, nullptr, nullptr, nullptr, align };
	}
}

template <typename S>
inline constexpr span_desc span_descriptor = describe_span<S>();

template <typename S, int_encoding Encoding = int_encoding::VARINT>
[[nodiscard]] consteval sequence_desc describe_sequence()
{
	if constexpr ( is_string<S> )
	{
		static_assert( sequence_access::items_offset<S> == 0 );
		static_assert( sequence_access::count_offset<S> <= max_offset,
		               "pb: member_offset_too_large: a member must be at an offset of 65535 bytes or less" );
		static_assert( S::max_size <= max_offset );
		constexpr auto storage =
		  sequence_access::count_size<S> == 1 ? sequence_storage::REPEATED_8 : sequence_storage::REPEATED_16;
		return { nullptr,
		         nullptr,
		         1,
		         static_cast<uint16_t>( S::max_size ),
		         static_cast<uint16_t>( sequence_access::count_offset<S> ),
		         storage };
	}
	else
	{
		using element = element_t<S>;
		constexpr const value_desc* item = &value_descriptor<element, Encoding>::value;
		static_assert( sizeof( element ) <= max_offset );
		constexpr auto item_size = static_cast<uint16_t>( sizeof( element ) );
		static_assert( sequence_traits<S>::capacity <= max_offset );
		constexpr auto capacity = static_cast<uint16_t>( sequence_traits<S>::capacity );

		if constexpr ( is_repeated<S> || is_bytes<S> )
		{
			static_assert( sequence_access::items_offset<S> == 0 );
			static_assert( sequence_access::count_offset<S> <= max_offset,
			               "pb: member_offset_too_large: a member must be at an offset of 65535 bytes or less" );
			constexpr auto storage =
			  sequence_access::count_size<S> == 1 ? sequence_storage::REPEATED_8 : sequence_storage::REPEATED_16;
			return { item,
			         nullptr,
			         item_size,
			         capacity,
			         static_cast<uint16_t>( sequence_access::count_offset<S> ),
			         storage };
		}
		else if constexpr ( is_std_array<S> )
		{
			return { item, nullptr, item_size, capacity, 0, sequence_storage::ARRAY };
		}
		else
		{
			return { item, &span_descriptor<S>, item_size, 0, 0, sequence_storage::ACCESSOR };
		}
	}
}

template <typename S, int_encoding Encoding = int_encoding::VARINT>
inline constexpr sequence_desc sequence_descriptor = describe_sequence<S, Encoding>();

template <typename O, int_encoding Encoding = int_encoding::VARINT>
inline constexpr optional_desc optional_descriptor{ &value_descriptor<typename O::value_type, Encoding>::value,
                                                    access_optional<O> };

template <typename V, int_encoding... AltEncodings>
struct oneof_alternatives;

template <typename... A, int_encoding... AltEncodings>
struct oneof_alternatives<oneof<A...>, AltEncodings...>
{
	static constexpr const value_desc* value[sizeof...( A )] = { &value_descriptor<A, AltEncodings>::value... };
};

template <typename V, int_encoding... AltEncodings>
[[nodiscard]] consteval value_desc describe_oneof()
{
	static_assert( oneof_access::storage_offset<V> == 0 );
	static_assert( oneof_access::index_offset<V> <= max_offset,
	               "pb: member_offset_too_large: a member must be at an offset of 65535 bytes or less" );
	return {
		.kind = field_kind::ONEOF,
		.scalar = static_cast<uint8_t>( oneof_access::index_size<V> ),
		.words = static_cast<uint16_t>( oneof_access::index_offset<V> ),
		.oneof = oneof_alternatives<V, AltEncodings...>::value,
	};
}

template <typename V, int_encoding... AltEncodings>
inline constexpr value_desc oneof_value_desc = describe_oneof<V, AltEncodings...>();

template <typename V>
struct default_oneof_value_desc;

template <typename... A>
struct default_oneof_value_desc<oneof<A...>>
{
	static constexpr value_desc value =
	  describe_oneof<oneof<A...>, ( static_cast<void>( sizeof( A ) ), int_encoding::VARINT )...>();
};

/// Compound descriptor of the member type `M`, or null when `M` encodes inline
/// in a field word (`SCALAR`, `std::string_view`, and `std::span<const uint8_t>`).
template <typename M, int_encoding Encoding = int_encoding::VARINT>
[[nodiscard]] consteval const value_desc* sub_desc_of()
{
	using U = std::remove_const_t<M>;
	constexpr field_kind kind = kind_of<U>();
	if constexpr ( kind == field_kind::SCALAR || std::is_same_v<U, std::string_view> ||
	               std::is_same_v<U, std::span<const uint8_t>> )
		return nullptr;
	else
		return &value_descriptor<U, Encoding>::value;
}

template <typename M, int_encoding Encoding = int_encoding::VARINT>
[[nodiscard]] consteval uint8_t base_op_of()
{
	using U = std::remove_const_t<M>;
	constexpr field_kind kind = kind_of<U>();
	if constexpr ( kind == field_kind::SCALAR )
	{
		constexpr uint8_t code = scalar_code_of<U, Encoding>();
		static_assert( code < op_string );
		return code;
	}
	else if constexpr ( std::is_same_v<U, std::string_view> )
		return op_string;
	else if constexpr ( std::is_same_v<U, std::span<const uint8_t>> )
		return op_bytes_view;
	else
		return op_sub_base;
}

[[nodiscard]] consteval bool fits_one_halfword( const field_entry& entry, uint32_t prev_number )
{
	if ( entry.alternative != 0 || entry.offset > 0x7Fu )
		return false;
	const uint32_t delta = entry.number - prev_number;
	if ( delta < 1u || delta > 8u )
		return false;
	if ( entry.base_op == op_sub_base )
		return entry.sub_idx < op_short_sub_limit;
	return true;
}

[[nodiscard]] consteval bool fits_two_halfwords( const field_entry& entry )
{
	if ( entry.number > 0xFFFu || entry.offset > 0xFFFu )
		return false;
	if ( entry.alternative != 0 )
		return entry.sub_idx == 0 && entry.alternative <= op_oneof_max_alt;
	if ( entry.base_op == op_sub_base )
		return entry.sub_idx < op_sub_limit;
	return true;
}

template <size_t SubCount, size_t WordCount>
struct message_table_data
{
	std::array<const value_desc*, SubCount> subs;
	std::array<uint16_t, WordCount> words;
};

template <size_t WordCount>
struct message_table_data<0, WordCount>
{
	std::array<uint16_t, WordCount> words;
};

template <>
struct message_table_data<0, 0>
{};

/// The field table of a message type: distinct compound descriptors followed by
/// 1- or 3-word field entries in ascending field number.
template <typename L>
struct field_table;

template <typename T,
          typename... M,
          uint32_t... Number,
          size_t... Offset,
          size_t... Alternative,
          int_encoding... Encoding>
struct field_table<field_list<T, field_at<M, Number, Offset, Alternative, Encoding>...>>
{
	static constexpr size_t entry_count = sizeof...( M );

	template <typename Target, int_encoding TargetEncoding>
	[[nodiscard]] static consteval size_t type_id_of()
	{
		if constexpr ( entry_count > 0 )
		{
			const bool same[]{ ( std::is_same_v<Target, std::remove_const_t<M>> && TargetEncoding == Encoding )... };
			for ( size_t i = 0; i < entry_count; ++i )
				if ( same[i] )
					return i;
		}
		return 0;
	}

	template <size_t VarOffset, size_t AltIdx>
	[[nodiscard]] static consteval int_encoding alt_encoding_at()
	{
		int_encoding result = int_encoding::VARINT;
		if constexpr ( entry_count > 0 )
		{
			const size_t offsets[]{ Offset... };
			const size_t alts[]{ Alternative... };
			const int_encoding encs[]{ Encoding... };
			for ( size_t i = 0; i < entry_count; ++i )
				if ( offsets[i] == VarOffset && alts[i] == AltIdx + 1 )
					result = encs[i];
		}
		return result;
	}

	template <typename V, size_t VarOffset, size_t... Is>
	[[nodiscard]] static consteval const value_desc* oneof_sub_of( std::index_sequence<Is...> )
	{
		constexpr bool all_varint = ( ( alt_encoding_at<VarOffset, Is>() == int_encoding::VARINT ) && ... );
		if constexpr ( all_varint )
			return &value_descriptor<V>::value;
		else
			return &oneof_value_desc<V, alt_encoding_at<VarOffset, Is>()...>;
	}

	template <typename V, size_t VarOffset, size_t... Is>
	[[nodiscard]] static consteval size_t oneof_key_of( std::index_sequence<Is...> )
	{
		constexpr bool all_varint = ( ( alt_encoding_at<VarOffset, Is>() == int_encoding::VARINT ) && ... );
		if constexpr ( all_varint )
			return type_id_of<V, int_encoding::VARINT>();
		else
			return entry_count + VarOffset;
	}

	template <typename FieldM, size_t FieldOffset, size_t FieldAlt, int_encoding FieldEnc>
	[[nodiscard]] static consteval const value_desc* row_sub_of()
	{
		using U = std::remove_const_t<FieldM>;
		if constexpr ( FieldAlt != 0 && is_oneof<U> )
			return oneof_sub_of<U, FieldOffset>( std::make_index_sequence<U::alternative_count>{} );
		else
			return sub_desc_of<FieldM, FieldEnc>();
	}

	template <typename FieldM, size_t FieldOffset, size_t FieldAlt, int_encoding FieldEnc>
	[[nodiscard]] static consteval size_t row_key_of()
	{
		using U = std::remove_const_t<FieldM>;
		if constexpr ( FieldAlt != 0 && is_oneof<U> )
			return oneof_key_of<U, FieldOffset>( std::make_index_sequence<U::alternative_count>{} );
		else
			return type_id_of<U, FieldEnc>();
	}

	struct plan
	{
		size_t subs_n;
		size_t words_n;
		bool has_wide;
		std::array<const value_desc*, entry_count> subs;
		std::array<field_entry, entry_count> entries;
	};

	[[nodiscard]] static consteval plan build_plan()
	{
		list_traits<field_list<T, field_at<M, Number, Offset, Alternative, Encoding>...>>::check();
		struct raw_row
		{
			uint32_t number;
			size_t offset;
			size_t alternative;
			size_t type_id;
			uint8_t base_op;
			const value_desc* sub;
		};
		std::array<raw_row, entry_count> rows{ raw_row{ Number,
		                                                Offset,
		                                                Alternative,
		                                                row_key_of<M, Offset, Alternative, Encoding>(),
		                                                base_op_of<M, Encoding>(),
		                                                row_sub_of<M, Offset, Alternative, Encoding>() }... };
		std::sort( rows.begin(), rows.end(), []( const raw_row& a, const raw_row& b ) { return a.number < b.number; } );

		plan p{};
		std::array<size_t, entry_count> sub_ids{};
		// Put a `std::variant` descriptor first (`subs[0]`) so its alternatives fit in 1 word.
		for ( unsigned pass = 0; pass < 2; ++pass )
		{
			for ( size_t i = 0; i < entry_count; ++i )
			{
				if ( rows[i].base_op != op_sub_base || ( ( rows[i].alternative != 0 ) != ( pass == 0 ) ) )
					continue;
				bool found = false;
				for ( size_t j = 0; j < p.subs_n; ++j )
					if ( sub_ids[j] == rows[i].type_id )
						found = true;
				if ( !found )
				{
					sub_ids[p.subs_n] = rows[i].type_id;
					p.subs[p.subs_n++] = rows[i].sub;
				}
			}
		}

		uint32_t prev_number = 0;
		for ( size_t i = 0; i < entry_count; ++i )
		{
			uint16_t sub_idx = 0;
			if ( rows[i].base_op == op_sub_base )
			{
				for ( size_t j = 0; j < p.subs_n; ++j )
					if ( sub_ids[j] == rows[i].type_id )
						sub_idx = static_cast<uint16_t>( j );
			}
			p.entries[i] = field_entry{
				rows[i].number,
				static_cast<uint16_t>( rows[i].offset ),
				static_cast<uint16_t>( rows[i].alternative ),
				sub_idx,
				rows[i].base_op,
			};
			if ( fits_one_halfword( p.entries[i], prev_number ) )
				p.words_n += 1u;
			else if ( fits_two_halfwords( p.entries[i] ) )
				p.words_n += 2u;
			else
			{
				p.has_wide = true;
				p.words_n += 5u;
			}
			prev_number = rows[i].number;
		}
		return p;
	}

	static constexpr plan layout = build_plan();
	static_assert( layout.subs_n <= 255 && layout.words_n <= 65535 );
#ifndef MICROPB_WIDE_TABLES
	static_assert( !layout.has_wide,
	               "pb: wide_table_required: define MICROPB_WIDE_TABLES for tags > 4095, offsets > 4095, "
	               "more than 73 compound types, multiple oneofs, or more than 32 oneof alternatives" );
#endif

	using table_type = message_table_data<layout.subs_n, layout.words_n>;

	[[nodiscard]] static consteval table_type make()
	{
		if constexpr ( layout.subs_n > 0 && layout.words_n > 0 )
			static_assert( offsetof( table_type, words ) == layout.subs_n * sizeof( const value_desc* ) );

		table_type out{};
		if constexpr ( layout.subs_n > 0 )
		{
			for ( size_t j = 0; j < layout.subs_n; ++j )
				out.subs[j] = layout.subs[j];
		}
		if constexpr ( layout.words_n > 0 )
		{
			size_t w = 0;
			uint32_t prev_number = 0;
			for ( size_t i = 0; i < entry_count; ++i )
			{
				const field_entry& e = layout.entries[i];
				if ( fits_one_halfword( e, prev_number ) )
				{
					const uint32_t op =
					  e.base_op == op_sub_base ? ( uint32_t{ op_sub_base } + e.sub_idx ) : uint32_t{ e.base_op };
					const uint32_t delta = e.number - prev_number - 1u;
					out.words[w++] =
					  static_cast<uint16_t>( ( delta << 12 ) | ( static_cast<uint32_t>( e.offset ) << 5 ) | op );
				}
				else if ( fits_two_halfwords( e ) )
				{
					const uint32_t op = e.alternative != 0 ? ( uint32_t{ op_oneof_base } + e.alternative - 1u )
					                         : e.base_op == op_sub_base ? ( uint32_t{ op_sub_base } + e.sub_idx )
					                                                    : uint32_t{ e.base_op };
					const uint32_t packed =
					  0x80000000u | ( op << 24 ) | ( static_cast<uint32_t>( e.offset ) << 12 ) | e.number;
					out.words[w++] = static_cast<uint16_t>( packed >> 16 );
					out.words[w++] = static_cast<uint16_t>( packed & 0xFFFFu );
				}
				else
				{
					out.words[w++] = static_cast<uint16_t>( 0xFF00u | e.base_op );
					out.words[w++] = static_cast<uint16_t>( e.number & 0xFFFFu );
					out.words[w++] = static_cast<uint16_t>( e.number >> 16 );
					out.words[w++] = e.offset;
					out.words[w++] = static_cast<uint16_t>( ( static_cast<uint32_t>( e.sub_idx ) << 8 ) | e.alternative );
				}
				prev_number = e.number;
			}
		}
		return out;
	}

	static constexpr table_type value = make();
};

template <typename T, int_encoding Encoding>
[[nodiscard]] consteval value_desc describe_value()
{
	constexpr field_kind kind = kind_of<T>();

	if constexpr ( kind == field_kind::SCALAR )
		return { .kind = kind, .scalar = scalar_code_of<T, Encoding>(), .words = 0, .size = 0 };
	else if constexpr ( std::is_same_v<T, std::string_view> )
		return { .kind = kind, .scalar = 0, .words = 0, .sequence = nullptr };
	else if constexpr ( kind == field_kind::FIXED_BYTES )
		return { .kind = kind, .scalar = 0, .words = 0, .size = static_cast<uint32_t>( sequence_traits<T>::capacity ) };
	else if constexpr ( std::is_same_v<T, std::span<const uint8_t>> )
		// Only this exact type is the bytes view that the interpreter copies as
		// one object. A span with a static extent has a different layout: it
		// goes through its accessor, as each other sequence.
		return { .kind = kind, .scalar = 0, .words = 0, .sequence = nullptr };
	else if constexpr ( kind == field_kind::STRING || kind == field_kind::BYTES || kind == field_kind::REPEATED )
		return { .kind = kind, .scalar = 0, .words = 0, .sequence = &sequence_descriptor<T, Encoding> };
	else if constexpr ( kind == field_kind::MESSAGE )
	{
		using list = field_list_of<T>;
		const void* table = nullptr;
		if constexpr ( field_table<list>::layout.words_n > 0 )
			table = &field_table<list>::value;
		return {
			.kind = kind,
			.scalar = static_cast<uint8_t>( field_table<list>::layout.subs_n ),
			.words = static_cast<uint16_t>( field_table<list>::layout.words_n ),
			.table = table,
		};
	}
	else if constexpr ( kind == field_kind::OPTIONAL )
		return { .kind = kind, .scalar = 0, .words = 0, .optional = &optional_descriptor<T, Encoding> };
	else
		return default_oneof_value_desc<T>::value;
}

template <typename T, int_encoding Encoding>
constexpr value_desc value_descriptor<T, Encoding>::value = describe_value<T, Encoding>();

////////////////////////////////////////////////////////////////////////////////
// Part 2: interpreter. No function in this part is a template.

/// Address of the member that is `offset` bytes into `object`.
[[nodiscard]] inline const void* member_at( const void* object, size_t offset )
{
	return static_cast<const unsigned char*>( object ) + offset;
}

[[nodiscard]] inline void* member_at( void* object, size_t offset )
{
	return static_cast<unsigned char*>( object ) + offset;
}

[[nodiscard]] inline const value_desc* const* message_subs( const value_desc& message )
{
	return static_cast<const value_desc* const*>( message.table );
}

[[nodiscard]] inline const uint16_t* message_words( const value_desc& message )
{
	if ( message.table == nullptr )
		return nullptr;
	return static_cast<const uint16_t*>(
	  member_at( message.table, size_t{ message.scalar } * sizeof( const value_desc* ) ) );
}

[[nodiscard]] constexpr field_entry decode_entry( const uint16_t* words, size_t& pos, uint32_t prev_number )
{
	const uint16_t w0 = words[pos++];
	if ( ( w0 & 0x8000u ) == 0 )
	{
		const uint32_t number = prev_number + ( ( w0 >> 12 ) & 7u ) + 1u;
		const auto offset = static_cast<uint16_t>( ( w0 >> 5 ) & 0x7Fu );
		const auto op = static_cast<uint8_t>( w0 & 0x1Fu );
		if ( op >= op_sub_base )
			return { number, offset, 0, static_cast<uint16_t>( uint32_t{ op } - op_sub_base ), op_sub_base };
		return { number, offset, 0, 0, op };
	}

#ifdef MICROPB_WIDE_TABLES
	if ( ( w0 >> 8 ) == 0xFFu )
	{
		const uint32_t number = uint32_t{ words[pos] } | ( uint32_t{ words[pos + 1] } << 16 );
		const uint16_t offset = words[pos + 2];
		const uint16_t tail = words[pos + 3];
		pos += 4;
		return {
			number,
			offset,
			static_cast<uint16_t>( tail & 0xFFu ),
			static_cast<uint16_t>( tail >> 8 ),
			static_cast<uint8_t>( w0 & 0xFFu ),
		};
	}
#endif

	const uint32_t packed = ( uint32_t{ w0 } << 16 ) | words[pos++];
	const auto op = static_cast<uint8_t>( ( w0 >> 8 ) & 0x7Fu );
	const auto offset = static_cast<uint16_t>( ( packed >> 12 ) & 0xFFFu );
	const uint32_t number = packed & 0xFFFu;
	if ( op >= op_oneof_base )
		return { number, offset, static_cast<uint16_t>( uint32_t{ op } - op_oneof_base + 1u ), 0, op_sub_base };
	if ( op >= op_sub_base )
		return { number, offset, 0, static_cast<uint16_t>( uint32_t{ op } - op_sub_base ), op_sub_base };
	return { number, offset, 0, 0, op };
}

[[nodiscard]] constexpr value_desc resolve_type( const value_desc* const* subs, const field_entry& entry )
{
	if ( entry.base_op < op_string )
		return { .kind = field_kind::SCALAR, .scalar = entry.base_op, .words = 0, .size = 0 };
	if ( entry.base_op == op_string )
		return { .kind = field_kind::STRING, .scalar = 0, .words = 0, .sequence = nullptr };
	if ( entry.base_op == op_bytes_view )
		return { .kind = field_kind::BYTES, .scalar = 0, .words = 0, .sequence = nullptr };
	return *subs[entry.sub_idx];
}

[[nodiscard]] inline wire_type wire_of_scalar( uint8_t code )
{
	if ( ( code & scalar_fixed ) == 0 )
		return wire_type::VARINT;
	return ( code & scalar_size_mask ) == 2 ? wire_type::FIXED32 : wire_type::FIXED64;
}

/// Wire type of a field of this type. A `std::optional` has the wire type of
/// its value. A repeated field has the wire type of one unpacked element.
[[nodiscard]] inline wire_type wire_of( const value_desc& type )
{
	if ( type.kind == field_kind::OPTIONAL )
		return wire_of( *type.optional->value );
	if ( type.kind == field_kind::REPEATED )
		return wire_of( *type.sequence->element );
	if ( type.kind == field_kind::SCALAR )
		return wire_of_scalar( type.scalar );
	return wire_type::LENGTH_DELIMITED;
}

[[nodiscard]] inline bool is_repeated_storage( sequence_storage storage )
{
	return storage == sequence_storage::REPEATED_8 || storage == sequence_storage::REPEATED_16;
}

[[nodiscard]] inline size_t load_count( const sequence_desc& sequence, const void* object )
{
	const void* ptr = member_at( object, sequence.count_offset );
	if ( sequence.storage == sequence_storage::REPEATED_8 )
		return *static_cast<const uint8_t*>( ptr );
	uint16_t count;
	std::memcpy( &count, ptr, sizeof count );
	return count;
}

inline void store_count( const sequence_desc& sequence, void* object, size_t count )
{
	void* ptr = member_at( object, sequence.count_offset );
	if ( sequence.storage == sequence_storage::REPEATED_8 )
	{
		*static_cast<uint8_t*>( ptr ) = static_cast<uint8_t>( count );
		return;
	}
	const auto narrow = static_cast<uint16_t>( count );
	std::memcpy( ptr, &narrow, sizeof narrow );
}

[[nodiscard]] inline size_t oneof_index( const value_desc& type, const void* object )
{
	const void* ptr = member_at( object, type.words );
	if ( type.scalar == 1 )
		return *static_cast<const uint8_t*>( ptr );
	uint16_t index;
	std::memcpy( &index, ptr, sizeof index );
	return index;
}

inline void set_oneof_index( const value_desc& type, void* object, size_t index )
{
	void* ptr = member_at( object, type.words );
	if ( type.scalar == 1 )
	{
		*static_cast<uint8_t*>( ptr ) = static_cast<uint8_t>( index );
		return;
	}
	const auto narrow = static_cast<uint16_t>( index );
	std::memcpy( ptr, &narrow, sizeof narrow );
}

/// The items of the sequence at `object`. The interpreter writes through the
/// result only when it decodes.
[[nodiscard]] inline sequence_view view_of( const sequence_desc& sequence, void* object )
{
	if ( is_repeated_storage( sequence.storage ) )
		return { object, load_count( sequence, object ) };
	if ( sequence.storage == sequence_storage::ARRAY )
		return { object, sequence.capacity };
	return sequence.span->access( object );
}

/// Value of the scalar at `value` as the wire carries it: the varint value, or
/// the bits of a fixed-width value. A negative integer sign-extends to 64 bits.
/// An enum is `int32`, so an unsigned enum value of 2^31 or more is negative.
[[nodiscard]] inline uint64_t load_scalar( const void* value, uint8_t code )
{
	uint64_t bits = 0;
	switch ( code & scalar_size_mask )
	{
		case 0: {
			uint8_t narrow;
			std::memcpy( &narrow, value, sizeof narrow );
			bits = narrow;
			break;
		}
		case 1: {
			uint16_t narrow;
			std::memcpy( &narrow, value, sizeof narrow );
			bits = narrow;
			break;
		}
		case 2: {
			uint32_t narrow;
			std::memcpy( &narrow, value, sizeof narrow );
			bits = narrow;
			break;
		}
		default: std::memcpy( &bits, value, sizeof bits );
	}

	if ( code & scalar_signed )
	{
		const unsigned shift = 64 - ( 8u << ( code & scalar_size_mask ) );
		bits = static_cast<uint64_t>( static_cast<int64_t>( bits << shift ) >> shift );
	}
	if ( code == scalar_enum )
		bits = static_cast<uint64_t>( static_cast<int64_t>( static_cast<int32_t>( bits ) ) );
	if ( is_scalar_zigzag( code ) )
		bits = zigzag_encode( static_cast<int64_t>( bits ) );
	return bits;
}

/// Writes the value of a scalar without a key.
inline void emit_scalar( writer& sink, uint8_t code, uint64_t bits )
{
	switch ( wire_of_scalar( code ) )
	{
		case wire_type::FIXED32: sink.write_fixed32( static_cast<uint32_t>( bits ) ); break;
		case wire_type::FIXED64: sink.write_fixed64( bits ); break;
		default: sink.write_varint( bits );
	}
}

/// Writes a string or bytes field. `always` is `true` for explicit presence.
inline void emit_bytes( writer& sink, uint32_t number, std::span<const uint8_t> bytes, bool always )
{
	if ( !always && bytes.empty() )
		return;

	sink.write_key( number, wire_type::LENGTH_DELIMITED );
	sink.write_varint( bytes.size() );
	sink.write_bytes( bytes );
}

inline void emit_message( writer& sink, const value_desc& message, const void* object );

// The compiler does not inline or clone the three entry points of the
// interpreter (`gnu::noinline`, `gnu::noclone`). The tables let one copy of
// the interpreter serve all message types. If the compiler puts the
// interpreter into its caller, it makes a copy for each message type and
// undoes that. The compiler then also examines the paths of other message
// types against the type that it knows at the call, and gives warnings about
// accesses that never occur.

#if __has_cpp_attribute( gnu::noclone )
#define MICROPB_ENTRY [[nodiscard, gnu::noinline, gnu::noclone]]
#else
#define MICROPB_ENTRY [[nodiscard, gnu::noinline]]
#endif

/// Encoded size of the fields of the message at `object`, without key and
/// length prefix. A writer without a span only counts, so the size and the
/// bytes come from one code path.
MICROPB_ENTRY inline size_t encoded_size( const value_desc& message, const void* object )
{
	writer sink;
	emit_message( sink, message, object );
	return sink.size();
}

/// Writes a field of a message type: the key, the length prefix and the fields.
inline void emit_submessage( writer& sink, uint32_t number, const value_desc& message, const void* object )
{
	sink.write_key( number, wire_type::LENGTH_DELIMITED );
	if ( !sink.counts_only() )
	{
		// The length prefix comes before the fields, so measure them first.
		sink.write_varint( encoded_size( message, object ) );
		emit_message( sink, message, object );
		return;
	}

	// The writer writes no byte, so the order is free: count the fields, then
	// the length prefix. This walks the message only one time.
	const size_t start = sink.size();
	emit_message( sink, message, object );
	sink.advance( varint_size( sink.size() - start ) );
}

/// Writes the scalar elements of a repeated field as one packed field. `items`
/// must not be empty: with no items, the function writes a key and the length
/// 0, which `protoc` does not write.
inline void emit_packed( writer& sink, uint32_t number, const sequence_desc& sequence, const sequence_view& items )
{
	const uint8_t code = sequence.element->scalar;
#ifdef MICROPB_FAST
	const bool fixed = ( code & scalar_fixed ) != 0;

	// The length prefix comes before the values, so measure them first.
	size_t payload_size = 0;
	if ( fixed )
	{
		payload_size = items.count * sequence.element_size;
	}
	else
	{
		const auto* ptr = static_cast<const unsigned char*>( items.data );
		for ( size_t i = 0; i < items.count; ++i, ptr += sequence.element_size )
			payload_size += varint_size( load_scalar( ptr, code ) );
	}

	sink.write_key( number, wire_type::LENGTH_DELIMITED );
	sink.write_varint( payload_size );
	if ( sink.counts_only() )
	{
		sink.advance( payload_size );
		return;
	}

	if ( fixed )
	{
		if constexpr ( std::endian::native == std::endian::little )
		{
			sink.write_bytes( std::span<const uint8_t>( static_cast<const uint8_t*>( items.data ), payload_size ) );
			return;
		}
		const auto* ptr = static_cast<const unsigned char*>( items.data );
		for ( size_t i = 0; i < items.count; ++i, ptr += sequence.element_size )
			emit_scalar( sink, code, load_scalar( ptr, code ) );
	}
	else
	{
		const auto* ptr = static_cast<const unsigned char*>( items.data );
		for ( size_t i = 0; i < items.count; ++i, ptr += sequence.element_size )
			sink.write_varint( load_scalar( ptr, code ) );
	}
#else
	const auto write_all = [&]( writer& out ) {
		const auto* ptr = static_cast<const unsigned char*>( items.data );
		for ( size_t i = 0; i < items.count; ++i, ptr += sequence.element_size )
			emit_scalar( out, code, load_scalar( ptr, code ) );
	};

	writer counter( std::span<uint8_t>{} );
	write_all( counter );
	sink.write_key( number, wire_type::LENGTH_DELIMITED );
	sink.write_varint( counter.size() );
	write_all( sink );
#endif
}

/// Writes one field. `always` is `true` for explicit presence: the function
/// writes the field also when its value is the default value.
inline void emit_value( writer& sink, uint32_t number, const value_desc& type, const void* value, bool always )
{
	switch ( type.kind )
	{
		case field_kind::SCALAR: {
			// Zero bits mean the default value. `-0.0` has a sign bit, so the codec writes it.
			const uint64_t bits = load_scalar( value, type.scalar );
			if ( always || bits != 0 )
			{
				sink.write_key( number, wire_of_scalar( type.scalar ) );
				emit_scalar( sink, type.scalar, bits );
			}
			break;
		}
		case field_kind::STRING:
			if ( type.sequence == nullptr )
			{
				std::string_view text;
				std::memcpy( static_cast<void*>( &text ), value, sizeof text );
				emit_bytes( sink, number, { reinterpret_cast<const uint8_t*>( text.data() ), text.size() }, always );
				break;
			}
			[[fallthrough]];
		case field_kind::BYTES: {
			std::span<const uint8_t> bytes;
			if ( type.sequence == nullptr )
			{
				std::memcpy( static_cast<void*>( &bytes ), value, sizeof bytes );
			}
			else
			{
				const sequence_view items = view_of( *type.sequence, const_cast<void*>( value ) );
				bytes = { static_cast<const uint8_t*>( items.data ), items.count };
			}
			emit_bytes( sink, number, bytes, always );
			break;
		}
		case field_kind::FIXED_BYTES:
			// The length is never 0 here, except for an array of length 0.
			emit_bytes( sink, number, { static_cast<const uint8_t*>( value ), type.size }, always );
			break;
		case field_kind::MESSAGE: emit_submessage( sink, number, type, value ); break;
		case field_kind::OPTIONAL: {
			const optional_desc& optional = *type.optional;
			if ( const void* held = optional.access( const_cast<void*>( value ), optional_op::GET ) )
				emit_value( sink, number, *optional.value, held, true );
			break;
		}
		case field_kind::REPEATED: {
			const sequence_desc& sequence = *type.sequence;
			const value_desc& element = *sequence.element;
			const sequence_view items = view_of( sequence, const_cast<void*>( value ) );
			if ( items.count == 0 )
				break;

			if ( element.kind != field_kind::SCALAR )
			{
				for ( size_t i = 0; i < items.count; ++i )
					emit_value( sink, number, element, member_at( items.data, i * sequence.element_size ), true );
				break;
			}

			emit_packed( sink, number, sequence, items );
			break;
		}
		case field_kind::ONEOF:
			// `emit_message` writes the active alternative of a oneof.
			break;
	}
}

/// Writes the fields of the message at `object` in ascending field number.
inline void emit_message( writer& sink, const value_desc& message, const void* object )
{
	const value_desc* const* subs = message_subs( message );
	const uint16_t* words = message_words( message );
	uint32_t prev = 0;
	for ( size_t pos = 0; pos < message.words; )
	{
#ifdef MICROPB_FAST
		const uint16_t w0 = words[pos];
		if ( ( w0 & 0x8000u ) == 0 )
		{
			const auto op = static_cast<uint8_t>( w0 & 0x1Fu );
			if ( op < op_string )
			{
				prev += ( ( w0 >> 12 ) & 7u ) + 1u;
				++pos;
				const uint64_t bits = load_scalar( member_at( object, ( w0 >> 5 ) & 0x7Fu ), op );
				if ( bits != 0 )
				{
					sink.write_key( prev, wire_of_scalar( op ) );
					emit_scalar( sink, op, bits );
				}
				continue;
			}
		}
#endif

		const field_entry field = decode_entry( words, pos, prev );
		prev = field.number;
		const value_desc type = resolve_type( subs, field );
		const void* member = member_at( object, field.offset );
		if ( field.alternative == 0 )
		{
			emit_value( sink, field.number, type, member, false );
			continue;
		}

		if ( oneof_index( type, member ) == field.alternative )
			emit_value( sink, field.number, *type.oneof[field.alternative - 1], member, true );
	}
}

MICROPB_ENTRY inline std::expected<size_t, error> encode(
  const value_desc& message, const void* object, std::span<uint8_t> out )
{
	writer sink( out );
	emit_message( sink, message, object );
	if ( !sink.fits() )
		return std::unexpected( error::BUFFER_TOO_SMALL );
	return sink.size();
}

////////////////////////////////////////////////////////////////////////////////

/// Stores a scalar from the value that the wire carries. A value that is too
/// wide for the type loses its upper bits, as in the Google C++ runtime.
inline void store_scalar( void* value, uint8_t code, uint64_t bits )
{
	if ( is_scalar_zigzag( code ) )
	{
		if ( ( code & scalar_size_mask ) == 2 )
			bits = static_cast<uint32_t>( zigzag_decode( static_cast<uint32_t>( bits ) ) );
		else
			bits = static_cast<uint64_t>( zigzag_decode( bits ) );
	}
	if ( code == scalar_bool )
		bits = bits != 0;

	switch ( code & scalar_size_mask )
	{
		case 0: {
			const auto narrow = static_cast<uint8_t>( bits );
			std::memcpy( value, &narrow, sizeof narrow );
			break;
		}
		case 1: {
			const auto narrow = static_cast<uint16_t>( bits );
			std::memcpy( value, &narrow, sizeof narrow );
			break;
		}
		case 2: {
			const auto narrow = static_cast<uint32_t>( bits );
			std::memcpy( value, &narrow, sizeof narrow );
			break;
		}
		default: std::memcpy( value, &bits, sizeof bits );
	}
}

/// Reads the value of a scalar without a key. Returns 0 when the read fails.
[[nodiscard]] inline uint64_t read_scalar( reader& in, uint8_t code )
{
	switch ( wire_of_scalar( code ) )
	{
		case wire_type::FIXED32: return in.read_fixed32();
		case wire_type::FIXED64: return in.read_fixed64();
		default: return in.read_varint();
	}
}

inline void reset_message( const value_desc& message, void* object );

/// Sets the value at `value` to its initial state. A `pb::repeated` keeps its
/// storage and only gets the count 0. A `std::span<const T>` becomes empty and
/// loses its block.
inline void reset_value( const value_desc& type, void* value )
{
	switch ( type.kind )
	{
		case field_kind::SCALAR: store_scalar( value, type.scalar & scalar_size_mask, 0 ); break;
		case field_kind::FIXED_BYTES: std::memset( value, 0, type.size ); break;
		case field_kind::STRING:
			if ( type.sequence == nullptr )
			{
				const std::string_view empty;
				std::memcpy( value, static_cast<const void*>( &empty ), sizeof empty );
			}
			else
			{
				*static_cast<char*>( value ) = '\0';
				store_count( *type.sequence, value, 0 );
			}
			break;
		case field_kind::BYTES:
			if ( type.sequence == nullptr )
			{
				const std::span<const uint8_t> empty;
				std::memcpy( value, static_cast<const void*>( &empty ), sizeof empty );
				break;
			}
			[[fallthrough]];
		case field_kind::REPEATED:
			if ( type.sequence->span != nullptr && type.sequence->span->bind != nullptr )
				type.sequence->span->bind( value, nullptr, 0 );
			else if ( is_repeated_storage( type.sequence->storage ) )
				store_count( *type.sequence, value, 0 );
			break;
		case field_kind::MESSAGE: reset_message( type, value ); break;
		case field_kind::OPTIONAL: static_cast<void>( type.optional->access( value, optional_op::RESET ) ); break;
		case field_kind::ONEOF: set_oneof_index( type, value, 0 ); break;
	}
}

/// Resets each member of the message at `object`, also in a nested message.
inline void reset_message( const value_desc& message, void* object )
{
	const value_desc* const* subs = message_subs( message );
	const uint16_t* words = message_words( message );
	for ( size_t pos = 0; pos < message.words; )
	{
#ifdef MICROPB_FAST
		const uint16_t w0 = words[pos];
		if ( ( w0 & 0x8000u ) == 0 )
		{
			const auto op = static_cast<uint8_t>( w0 & 0x1Fu );
			if ( op < op_string )
			{
				++pos;
				store_scalar( member_at( object, ( w0 >> 5 ) & 0x7Fu ), op & scalar_size_mask, 0 );
				continue;
			}
		}
		else if ( ( ( w0 >> 8 ) & 0x7Fu ) > op_oneof_base && ( w0 >> 8 ) != 0xFFu )
		{
			// Skip extra alternatives of a 2-halfword oneof without decoding the entry.
			pos += 2;
			continue;
		}
#endif

		// A `pb::oneof` member has one entry for each alternative. Reset it one time.
		const field_entry field = decode_entry( words, pos, 0 );
		if ( field.alternative <= 1 )
		{
			const value_desc type = resolve_type( subs, field );
			reset_value( type, member_at( object, field.offset ) );
		}
	}
}

inline void decode_message( reader& in, const value_desc& message, void* object, arena* memory );
inline void decode_value( reader& in, wire_type wire, const value_desc& type, void* value, arena* memory );

/// Capacity of the arena block at `data`: a `size_t` just before the first
/// item. 0 for a span without a block.
[[nodiscard]] inline size_t block_capacity( const void* data )
{
	size_t capacity = 0;
	if ( data != nullptr )
		std::memcpy( &capacity, static_cast<const unsigned char*>( data ) - sizeof capacity, sizeof capacity );
	return capacity;
}

/// Capacity of the sequence at `items` for `decode`. The capacity of a span is
/// the capacity of its arena block.
[[nodiscard]] inline size_t decode_capacity( const sequence_desc& sequence, const sequence_view& items )
{
	if ( sequence.span != nullptr && sequence.span->bind != nullptr )
		return block_capacity( items.data );
	return sequence.capacity;
}

/// Elements of a repeated field in the value of one record, which the function
/// reads from `in`. 0 for a wrong wire type and for a read that fails.
[[nodiscard]] inline size_t count_record( reader& in, wire_type wire, const sequence_desc& sequence )
{
	const value_desc& element = *sequence.element;
	if ( element.kind == field_kind::SCALAR && wire == wire_type::LENGTH_DELIMITED )
	{
		const std::span<const uint8_t> bytes = in.read_length_delimited();
		if ( element.scalar & scalar_fixed )
			return bytes.size() >> ( element.scalar & scalar_size_mask );

		// Each varint ends with a byte without bit 7.
		const auto last = []( uint8_t byte ) { return ( byte & 0x80 ) == 0; };
		return static_cast<size_t>( std::count_if( bytes.begin(), bytes.end(), last ) );
	}

	const bool match = wire == wire_of( element );
	in.skip( wire );
	return match && !in.failed() ? 1 : 0;
}

/// Elements of the field `key.number` from the record at `in` to the end of the
/// message. `in` is a copy of the reader after the key of the record. A read
/// that fails ends the count.
[[nodiscard]] inline size_t count_elements( reader in, field_key key, const sequence_desc& sequence )
{
	size_t count = count_record( in, key.type, sequence );
	while ( !in.empty() )
	{
		const field_key next = in.read_key();
		if ( in.failed() )
			break;

		if ( next.number == key.number )
			count += count_record( in, next.type, sequence );
		else
			in.skip( next.type );
	}
	return count;
}

/// Takes a block for `capacity` items from `memory`, copies `items` into it and
/// sets the span at `value` to it. `false` when the arena is full.
[[nodiscard]] inline bool grow(
  const sequence_desc& sequence, void* value, const sequence_view& items, size_t capacity, arena* memory )
{
	// The block has `header` bytes with the capacity at their end, then the items.
	const size_t alignment = std::max<size_t>( sequence.span->element_align, alignof( size_t ) );
	const size_t header = ( sizeof( size_t ) + alignment - 1 ) & ~( alignment - 1 );
	if ( memory == nullptr || capacity > ( size_t( -1 ) - header ) / sequence.element_size )
		return false;

	auto* block = static_cast<unsigned char*>( memory->allocate( header + capacity * sequence.element_size, alignment ) );
	if ( block == nullptr )
		return false;

	unsigned char* data = block + header;
	std::memcpy( data - sizeof capacity, &capacity, sizeof capacity );
	// `memcpy` makes the objects in the block: each element type is implicit-lifetime.
	if ( items.count != 0 )
		std::memcpy( data, items.data, items.count * sequence.element_size );
	sequence.span->bind( value, data, items.count );
	return true;
}

/// Makes space in the arena block of the span at `value` for the elements of
/// the record at `in`. When the block is too small, takes a new block for the
/// items and for all elements of the field from this record to the end of the
/// message. `false` when the arena is full.
[[nodiscard]] inline bool reserve(
  reader& in, field_key key, const sequence_desc& sequence, void* value, arena* memory )
{
	// Only a span that `decode` did not bind can have more items than its block.
	const sequence_view items = view_of( sequence, value );
	const size_t capacity = decode_capacity( sequence, items );
	if ( items.count > capacity )
		return false;

	reader record = in;
	const size_t needed = count_record( record, key.type, sequence );
	if ( record.failed() )
	{
		in.absorb( record );
		return true;
	}
	if ( needed <= capacity - items.count )
		return true;
	return grow( sequence, value, items, items.count + count_elements( in, key, sequence ), memory );
}

[[nodiscard]] inline bool is_bound_span( const sequence_desc& sequence )
{
	return sequence.span != nullptr && sequence.span->bind != nullptr;
}

[[nodiscard]] inline error capacity_error( const sequence_desc& sequence )
{
	return is_bound_span( sequence ) ? error::TRUNCATED : error::CAPACITY_EXCEEDED;
}

inline void commit_count( const sequence_desc& sequence, void* value, void* data, size_t count )
{
	if ( is_bound_span( sequence ) )
		sequence.span->bind( value, data, count );
	else
		store_count( sequence, value, count );
}

/// Reads one element from `in` into the next free item of the sequence at
/// `value`. A full sequence is an error: the function examines the capacity
/// before it reads the element. A full span block means that the count of the
/// elements was too small, thus the input is not valid. The item starts from
/// its initial state: records of a repeated field do not merge.
inline void decode_element( reader& in, wire_type wire, const sequence_desc& sequence, void* value, arena* memory )
{
	const sequence_view items = view_of( sequence, value );
	if ( items.count >= decode_capacity( sequence, items ) )
		return in.fail( capacity_error( sequence ) );

	void* item = member_at( items.data, items.count * sequence.element_size );
	if ( sequence.span != nullptr && sequence.span->construct != nullptr )
		sequence.span->construct( item );
	reset_value( *sequence.element, item );
	decode_value( in, wire, *sequence.element, item, memory );
	if ( in.failed() )
		return;

	commit_count( sequence, value, items.data, items.count + 1 );
}

#ifdef MICROPB_FAST
/// Reads a packed scalar record into `value`. Scalar elements are trivially
/// copyable and implicit-lifetime, so the loop writes directly into storage.
inline void decode_packed_scalars( reader& in, const sequence_desc& sequence, void* value )
{
	const std::span<const uint8_t> bytes = in.read_length_delimited();
	if ( in.failed() || bytes.empty() )
		return;

	const sequence_view items = view_of( sequence, value );
	const error full_err = capacity_error( sequence );
	const size_t capacity = decode_capacity( sequence, items );
	const uint8_t code = sequence.element->scalar;
	const size_t elem_size = sequence.element_size;
	size_t count = items.count;
	auto* dst = static_cast<unsigned char*>( member_at( items.data, count * elem_size ) );

	if ( code & scalar_fixed )
	{
		const unsigned shift = code & scalar_size_mask;
		const size_t full = bytes.size() >> shift;
		const size_t avail = capacity - count;
		const size_t copy_count = std::min( full, avail );

		if ( copy_count != 0 )
		{
			if constexpr ( std::endian::native == std::endian::little )
			{
				std::memcpy( dst, bytes.data(), copy_count << shift );
			}
			else
			{
				reader packed( bytes.first( copy_count << shift ) );
				for ( size_t i = 0; i < copy_count; ++i, dst += elem_size )
					store_scalar( dst, code, read_scalar( packed, code ) );
			}
			count += copy_count;
		}

		if ( full > avail )
			in.fail( full_err );
		else if ( ( bytes.size() & ( elem_size - 1 ) ) != 0 )
			in.fail( count >= capacity ? full_err : error::TRUNCATED );
	}
	else
	{
		reader packed( bytes );
		while ( !packed.empty() )
		{
			if ( count >= capacity )
			{
				in.fail( full_err );
				break;
			}
			const uint64_t bits = packed.read_varint();
			if ( packed.failed() )
			{
				in.absorb( packed );
				break;
			}
			store_scalar( dst, code, bits );
			dst += elem_size;
			++count;
		}
	}

	if ( count != items.count )
		commit_count( sequence, value, items.data, count );
}
#endif

/// Reads one record of a repeated field. A scalar element can be a packed
/// record or a single value, also mixed in one message.
inline void decode_repeated( reader& in, field_key key, const sequence_desc& sequence, void* value, arena* memory )
{
	if ( sequence.span != nullptr && sequence.span->reserve != nullptr &&
	     !sequence.span->reserve( in, key, sequence, value, memory ) )
		return in.fail( error::ARENA_FULL );

	const value_desc& element = *sequence.element;
	if ( element.kind == field_kind::SCALAR && key.type == wire_type::LENGTH_DELIMITED )
	{
#ifdef MICROPB_FAST
		decode_packed_scalars( in, sequence, value );
#else
		reader packed( in.read_length_delimited() );
		while ( !packed.empty() )
			decode_element( packed, wire_of( element ), sequence, value, memory );
		in.absorb( packed );
#endif
	}
	else if ( key.type == wire_of( element ) )
	{
		decode_element( in, key.type, sequence, value, memory );
	}
	else
	{
		in.skip( key.type );
	}
}

/// Reads the value of one field that is not repeated into `value`. The function
/// skips a field with the wrong wire type, as an unknown field. A read that
/// fails does not change a scalar, a string or a bytes field.
inline void decode_value( reader& in, wire_type wire, const value_desc& type, void* value, arena* memory )
{
	if ( wire != wire_of( type ) )
		return in.skip( wire );

	if ( type.kind == field_kind::SCALAR )
	{
		const uint64_t bits = read_scalar( in, type.scalar );
		if ( !in.failed() )
			store_scalar( value, type.scalar, bits );
		return;
	}
	if ( type.kind == field_kind::OPTIONAL )
	{
		// A new value starts from the reset state, as each member of `msg` does.
		const optional_desc& optional = *type.optional;
		void* held = optional.access( value, optional_op::GET );
		if ( held == nullptr )
		{
			held = optional.access( value, optional_op::ENGAGE );
			reset_value( *optional.value, held );
		}
		return decode_value( in, wire, *optional.value, held, memory );
	}

	// All other kinds are length-delimited. The bytes are a view into the input.
	const std::span<const uint8_t> bytes = in.read_length_delimited();
	if ( in.failed() )
		return;

	switch ( type.kind )
	{
		case field_kind::STRING:
			if ( type.sequence == nullptr )
			{
				const std::string_view text( reinterpret_cast<const char*>( bytes.data() ), bytes.size() );
				std::memcpy( value, static_cast<const void*>( &text ), sizeof text );
				break;
			}
			[[fallthrough]];
		case field_kind::BYTES: {
			if ( type.sequence == nullptr )
			{
				std::memcpy( value, static_cast<const void*>( &bytes ), sizeof bytes );
				break;
			}

			if ( bytes.size() > type.sequence->capacity )
				return in.fail( error::CAPACITY_EXCEEDED );
			if ( !bytes.empty() )
				std::memcpy( value, bytes.data(), bytes.size() );
			if ( type.kind == field_kind::STRING )
				static_cast<char*>( value )[bytes.size()] = '\0';
			store_count( *type.sequence, value, bytes.size() );
			break;
		}
		case field_kind::FIXED_BYTES:
			// Input that is shorter than the array leaves the remaining bytes 0.
			if ( bytes.size() > type.size )
				return in.fail( error::CAPACITY_EXCEEDED );
			std::memset( value, 0, type.size );
			if ( !bytes.empty() )
				std::memcpy( value, bytes.data(), bytes.size() );
			break;
		case field_kind::MESSAGE: {
			// The codec does not reset the message: a second occurrence merges into the first.
			reader part( bytes );
			decode_message( part, type, value, memory );
			in.absorb( part );
			break;
		}
		default: break;
	}
}

#ifdef MICROPB_FAST
/// Finds the entry of the field `number` in `words`. The search starts at the
/// halfword index `cursor` (with preceding field number `cursor_prev`). After
/// the end of the table, it continues at 0. After a match, `cursor` and
/// `cursor_prev` name the start of the entry.
[[nodiscard]] inline bool find_field( const uint16_t* words,
                                      size_t total_words,
                                      uint32_t number,
                                      size_t& cursor,
                                      uint32_t& cursor_prev,
                                      field_entry& out )
{
	for ( unsigned pass = 0; pass < 2; ++pass )
	{
		size_t pos = pass == 0 ? cursor : 0;
		uint32_t prev = pass == 0 ? cursor_prev : 0;
		const size_t stop = pass == 0 ? total_words : cursor;
		while ( pos < stop )
		{
			const uint16_t w0 = words[pos];
			uint32_t entry_number;
			size_t step;
			if ( ( w0 & 0x8000u ) == 0 )
			{
				entry_number = prev + ( ( w0 >> 12 ) & 7u ) + 1u;
				step = 1;
			}
#ifdef MICROPB_WIDE_TABLES
			else if ( ( w0 >> 8 ) == 0xFFu )
			{
				entry_number = uint32_t{ words[pos + 1] } | ( uint32_t{ words[pos + 2] } << 16 );
				step = 5;
			}
#endif
			else
			{
				entry_number = words[pos + 1] & 0xFFFu;
				step = 2;
			}

			if ( entry_number == number )
			{
				cursor = pos;
				cursor_prev = prev;
				out = decode_entry( words, pos, prev );
				return true;
			}
			prev = entry_number;
			pos += step;
		}
	}
	return false;
}
#else
/// Finds the entry of the field `number` in `words`.
[[nodiscard]] inline bool find_field( const uint16_t* words, size_t total_words, uint32_t number, field_entry& out )
{
	uint32_t prev = 0;
	for ( size_t pos = 0; pos < total_words; )
	{
		const field_entry entry = decode_entry( words, pos, prev );
		if ( entry.number == number )
		{
			out = entry;
			return true;
		}
		prev = entry.number;
	}
	return false;
}
#endif

/// Reads all fields of `in` into the message at `object`, until the end of
/// the input or the first error. Does not reset the message: a field that
/// occurs again changes the value that is there.
inline void decode_message( reader& in, const value_desc& message, void* object, arena* memory )
{
	const value_desc* const* subs = message_subs( message );
	const uint16_t* words = message_words( message );
#ifdef MICROPB_FAST
	// Fields usually arrive in ascending order, and a repeated field arrives
	// many times. The search starts at the entry of the last match.
	size_t cursor = 0;
	uint32_t cursor_prev = 0;
#endif
	while ( !in.empty() )
	{
		const field_key key = in.read_key();
		if ( in.failed() )
			return;

		field_entry field{};
#ifdef MICROPB_FAST
		if ( !find_field( words, message.words, key.number, cursor, cursor_prev, field ) )
#else
		if ( !find_field( words, message.words, key.number, field ) )
#endif
		{
			in.skip( key.type );
			continue;
		}

		void* member = member_at( object, field.offset );
#ifdef MICROPB_FAST
		if ( field.base_op < op_string )
		{
			if ( key.type != wire_of_scalar( field.base_op ) )
				in.skip( key.type );
			else
			{
				const uint64_t bits = read_scalar( in, field.base_op );
				if ( !in.failed() )
					store_scalar( member, field.base_op, bits );
			}
			continue;
		}
#endif

		const value_desc type = resolve_type( subs, field );
		if ( type.kind == field_kind::REPEATED )
		{
			decode_repeated( in, key, *type.sequence, member, memory );
			continue;
		}
		if ( field.alternative == 0 )
		{
			decode_value( in, key.type, type, member, memory );
			continue;
		}

		// A different active alternative gives way to a new one in the reset
		// state, as each member of `msg` has. The same alternative stays, so a
		// message merges. The codec skips a field with a wrong wire type and
		// keeps the variant.
		const value_desc& alternative = *type.oneof[field.alternative - 1];
		if ( key.type != wire_of( alternative ) )
		{
			in.skip( key.type );
			continue;
		}

		if ( oneof_index( type, member ) != field.alternative )
		{
			set_oneof_index( type, member, field.alternative );
			reset_value( alternative, member );
		}
		decode_value( in, key.type, alternative, member, memory );
	}
}

/// `memory` is null for a message without a `std::span<const T>` repeated field.
MICROPB_ENTRY inline std::expected<size_t, error> decode(
  const value_desc& message, std::span<const uint8_t> in, void* object, arena* memory )
{
	reset_message( message, object );

	reader source( in );
	decode_message( source, message, object, memory );
	if ( source.failed() )
		return std::unexpected( source.status().error() );
	return in.size();
}

#undef MICROPB_ENTRY

} // namespace detail

////////////////////////////////////////////////////////////////////////////////

/// Size in bytes of the encoding of `msg`.
template <typename T>
[[nodiscard]] size_t encoded_size( const T& msg )
{
	return detail::encoded_size( detail::value_descriptor<T>::value, &msg );
}

/// Encodes `msg` into `out` in proto3 wire format and returns the number of
/// bytes that it wrote. Does not write past `out`. When `out` is too small, returns
/// `error::BUFFER_TOO_SMALL`; the bytes of `out` that it wrote are not valid.
template <typename T>
[[nodiscard]] std::expected<size_t, error> encode( const T& msg, std::span<uint8_t> out )
{
	return detail::encode( detail::value_descriptor<T>::value, &msg, out );
}

/// Decodes the proto3 message in `in` into `msg` and returns `in.size()`. Resets
/// `msg` first. On error, `msg` holds the fields that `decode` read before the
/// error. A `std::string_view` or a `std::span<const uint8_t>` of `msg`
/// points into `in`. The items of each `std::span<const T>` repeated field are
/// in one block of `memory`. `in` and `memory` must stay valid while `msg` is
/// in use. When `memory` is too small, returns `error::ARENA_FULL`. `decode`
/// does not free the blocks of `memory`.
template <typename T>
[[nodiscard]] std::expected<size_t, error> decode( std::span<const uint8_t> in, T& msg, arena& memory )
{
	detail::check_decodable<T>();
	return detail::decode( detail::value_descriptor<T>::value, in, &msg, &memory );
}

/// `decode` with caller-owned scratch bytes (`std::span<uint8_t>`, a
/// `uint8_t[N]` array, or a `std::array<uint8_t, N>`). `in` and
/// `memory` must stay valid while `msg` is in use.
template <typename T>
[[nodiscard]] std::expected<size_t, error> decode( std::span<const uint8_t> in, T& msg, std::span<uint8_t> memory )
{
	arena pool( memory );
	return decode( in, msg, pool );
}

/// `decode` for a message without a `std::span<const T>` repeated field, also
/// not in a nested message.
template <typename T>
[[nodiscard]] std::expected<size_t, error> decode( std::span<const uint8_t> in, T& msg )
{
	detail::check_decodable<T>();
	detail::check_no_arena<T>();
	return detail::decode( detail::value_descriptor<T>::value, in, &msg, nullptr );
}

} // namespace pb
