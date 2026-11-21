#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

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
/// two bits are log2 of the size in bytes. The other bits are flags.
inline constexpr std::uint8_t scalar_size_mask = 0x03;
/// The type has a sign: the loaded value sign-extends to 64 bits.
inline constexpr std::uint8_t scalar_signed = 0x04;
/// `sint32` or `sint64`: ZigZag varint.
inline constexpr std::uint8_t scalar_zigzag = 0x08;
/// Fixed width on the wire: 4 bytes or 8 bytes, the size of the type.
inline constexpr std::uint8_t scalar_fixed = 0x10;
inline constexpr std::uint8_t scalar_bool = 0x20;
/// An enum: `int32` on the wire.
inline constexpr std::uint8_t scalar_enum = 0x40;

template <typename T>
[[nodiscard]] consteval std::uint8_t scalar_code_of()
{
	constexpr std::uint8_t size = sizeof( T ) == 1 ? 0 : sizeof( T ) == 2 ? 1 : sizeof( T ) == 4 ? 2 : 3;

	if constexpr ( std::is_same_v<T, bool> )
		return size | scalar_bool;
	else if constexpr ( std::is_enum_v<T> )
		return size | scalar_enum | ( std::is_signed_v<std::underlying_type_t<T>> ? scalar_signed : 0 );
	else if constexpr ( is_zigzag<T> )
		return size | scalar_signed | scalar_zigzag;
	else if constexpr ( wire_type_of<T>() != wire_type::VARINT )
		return size | scalar_fixed;
	else
		return size | ( std::is_signed_v<T> ? scalar_signed : 0 );
}

struct sequence_desc;
struct optional_desc;
struct variant_desc;

/// How to read and write a value of one type. All members, elements, optional
/// values and alternatives of the same type share one descriptor.
struct value_desc
{
	field_kind kind;
	/// SCALAR: the scalar code. MESSAGE: the number of distinct compound types in `table`.
	std::uint8_t scalar;
	/// MESSAGE: the number of `std::uint32_t` words in `table`.
	std::uint16_t words;
	union
	{
		/// FIXED_BYTES: the number of bytes. 0 for SCALAR and STRING.
		std::uint32_t size;
		/// MESSAGE: `message_table_data`, or null when `words == 0`.
		const void* table;
		/// REPEATED and BYTES. Null for the view `std::span<const std::uint8_t>`.
		const sequence_desc* sequence;
		/// OPTIONAL.
		const optional_desc* optional;
		/// ONEOF.
		const variant_desc* variant;
	};
};

// Each field of a message encodes into 1 or 3 `std::uint32_t` words, sorted in
// ascending field number:
// - 1 word (bit 31 == 0): `[30:24 op] [23:12 offset] [11:0 number]`
//     op 0..70:   inline SCALAR (`op` is the scalar code)
//     op 71:      inline STRING (`std::string_view`)
//     op 72:      inline BYTES view (`std::span<const std::uint8_t>`)
//     op 73..95:  compound member at `subs[op - 73]`
//     op 96..127: ONEOF alternative `op - 95` (1..32) on `subs[0]`
// - 3 words (bit 31 == 1), when `number > 4095`, `offset > 4095`, `sub_idx >= 23`,
//   a second `std::variant` member, or `alternative > 32`:
//     word 0: `0x80000000u | number`
//     word 1: `(offset << 16) | alternative`
//     word 2: `(sub_idx << 8) | base_op`
inline constexpr std::uint8_t op_string = 71;
inline constexpr std::uint8_t op_bytes_view = 72;
inline constexpr std::uint8_t op_sub_base = 73;
inline constexpr std::uint8_t op_sub_limit = 23;
inline constexpr std::uint8_t op_oneof_base = 96;
inline constexpr std::uint8_t op_oneof_max_alt = 32;

/// Unpacked entry of a message field table.
struct field_entry
{
	std::uint32_t number;
	std::uint16_t offset;
	/// 0 for a plain member, else the 1-based index of the `std::variant` alternative.
	std::uint16_t alternative;
	/// Index into `subs` when `base_op == op_sub_base`.
	std::uint16_t sub_idx;
	std::uint8_t base_op;
};

/// The items of a sequence at this moment.
struct sequence_view
{
	void* data;
	std::size_t count;
};

using sequence_accessor = sequence_view ( * )( void* sequence );

/// Sets the sequence to `count` items at `data`.
using sequence_binder = void ( * )( void* sequence, void* data, std::size_t count );

/// Makes a value-initialized element at `item`.
using item_constructor = void ( * )( void* item );

/// Makes space in the arena block of a span for the elements of a record (`reserve`).
using sequence_reserver =
  bool ( * )( reader& in, field_key key, const sequence_desc& sequence, void* value, arena* memory );

enum class sequence_storage : std::uint8_t
{
	/// `pb::repeated`: the items are at offset 0, the count is at `count_offset`.
	REPEATED,
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
	std::uint16_t element_align;
};

struct sequence_desc
{
	const value_desc* element;
	/// Non-null only for `sequence_storage::ACCESSOR`.
	const span_desc* span;
	std::uint32_t element_size;
	std::uint32_t capacity;
	std::uint16_t count_offset;
	sequence_storage storage;
};

enum class optional_op : std::uint8_t
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

/// Gives the address of alternative `index` when it is active. If it is not
/// active and `select` is `true`, makes it active with a value-initialized
/// value first. Else gives null. Index 0 is `std::monostate`, so
/// `access(variant, 0, true)` resets the variant.
using variant_accessor = void* ( * )( void* variant, std::size_t index, bool select );

struct variant_desc
{
	/// One entry for each alternative. The entry of `std::monostate` is null.
	const value_desc* const* alternatives;
	variant_accessor access;
};

static_assert( sizeof( value_desc ) <= 2 * sizeof( void* ) );

template <typename T>
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
	static constexpr std::size_t items_offset = offsetof( S, _items );
	template <typename S>
	static constexpr std::size_t count_offset = offsetof( S, _count );
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
void bind_span( void* sequence, void* data, std::size_t count )
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
		value.reset();
	}
	else if ( op == optional_op::ENGAGE && !value )
	{
		if constexpr ( requires { value.emplace(); } )
			value.emplace();
	}
	return value ? &*value : nullptr;
}

/// Makes alternative `I` of `value` active and gives its address. An
/// alternative that is `const` or has a `const` member is encode-only: null.
template <std::size_t I, typename V>
[[nodiscard]] void* emplace_alternative( V& value )
{
	if constexpr ( !std::is_const_v<std::variant_alternative_t<I, V>> && requires { value.template emplace<I>(); } )
		return &value.template emplace<I>();
	return nullptr;
}

template <typename V, std::size_t... I>
[[nodiscard]] void* access_alternative( V& value, std::size_t index, bool select, std::index_sequence<I...> )
{
	void* result = nullptr;
	if ( value.index() == index )
	{
		static_cast<void>(
		  ( ( index == I && ( result = const_cast<void*>( static_cast<const void*>( std::get_if<I>( &value ) ) ), true ) ) ||
		    ... ) );
		return result;
	}
	if ( !select )
		return nullptr;

	static_cast<void>( ( ( index == I && ( result = emplace_alternative<I>( value ), true ) ) || ... ) );
	return result;
}

template <typename V>
[[nodiscard]] void* access_variant( void* variant, std::size_t index, bool select )
{
	return access_alternative(
	  *static_cast<V*>( variant ), index, select, std::make_index_sequence<std::variant_size_v<V>>{} );
}

////////////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline bool reserve(
  reader& in, field_key key, const sequence_desc& sequence, void* value, arena* memory );

template <typename S>
[[nodiscard]] consteval span_desc describe_span()
{
	using element = element_t<S>;
	constexpr auto align = static_cast<std::uint16_t>( alignof( element ) );
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

template <typename S>
[[nodiscard]] consteval sequence_desc describe_sequence()
{
	using element = element_t<S>;
	constexpr const value_desc* item = &value_descriptor<element>::value;
	constexpr std::uint32_t item_size = sizeof( element );
	constexpr auto capacity = static_cast<std::uint32_t>( sequence_traits<S>::capacity );

	if constexpr ( is_repeated<S> )
	{
		static_assert( sequence_access::items_offset<S> == 0 );
		static_assert( sequence_access::count_offset<S> <= max_offset,
		               "pb: member_offset_too_large: a member must be at an offset of 65535 bytes or less" );
		return { item,
		         nullptr,
		         item_size,
		         capacity,
		         static_cast<std::uint16_t>( sequence_access::count_offset<S> ),
		         sequence_storage::REPEATED };
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

template <typename S>
inline constexpr sequence_desc sequence_descriptor = describe_sequence<S>();

template <typename O>
inline constexpr optional_desc optional_descriptor{ &value_descriptor<typename O::value_type>::value,
                                                    access_optional<O> };

/// The descriptor of the alternative `A` of a `std::variant`. Null for `std::monostate`.
template <typename A>
[[nodiscard]] consteval const value_desc* describe_alternative()
{
	if constexpr ( std::is_same_v<A, std::monostate> )
		return nullptr;
	else
		return &value_descriptor<A>::value;
}

template <typename V>
struct variant_alternatives;

template <typename... A>
struct variant_alternatives<std::variant<A...>>
{
	static constexpr const value_desc* value[sizeof...( A )] = { describe_alternative<A>()... };
};

template <typename V>
inline constexpr variant_desc variant_descriptor{ variant_alternatives<V>::value, access_variant<V> };

/// Compound descriptor of the member type `M`, or null when `M` encodes inline
/// in a field word (`SCALAR`, `STRING`, and `std::span<const std::uint8_t>`).
template <typename M>
[[nodiscard]] consteval const value_desc* sub_desc_of()
{
	using U = std::remove_const_t<M>;
	constexpr field_kind kind = kind_of<U>();
	if constexpr ( kind == field_kind::SCALAR || kind == field_kind::STRING ||
	               std::is_same_v<U, std::span<const std::uint8_t>> )
		return nullptr;
	else
		return &value_descriptor<U>::value;
}

template <typename M>
[[nodiscard]] consteval std::uint8_t base_op_of()
{
	using U = std::remove_const_t<M>;
	constexpr field_kind kind = kind_of<U>();
	if constexpr ( kind == field_kind::SCALAR )
	{
		constexpr std::uint8_t code = scalar_code_of<U>();
		static_assert( code < op_string );
		return code;
	}
	else if constexpr ( kind == field_kind::STRING )
		return op_string;
	else if constexpr ( std::is_same_v<U, std::span<const std::uint8_t>> )
		return op_bytes_view;
	else
		return op_sub_base;
}

[[nodiscard]] consteval bool fits_one_word( const field_entry& entry )
{
	if ( entry.number > 0xFFFu || entry.offset > 0xFFFu )
		return false;
	if ( entry.alternative != 0 )
		return entry.sub_idx == 0 && entry.alternative <= op_oneof_max_alt;
	if ( entry.base_op == op_sub_base )
		return entry.sub_idx < op_sub_limit;
	return true;
}

template <std::size_t SubCount, std::size_t WordCount>
struct message_table_data
{
	std::array<const value_desc*, SubCount> subs;
	std::array<std::uint32_t, WordCount> words;
};

template <std::size_t WordCount>
struct message_table_data<0, WordCount>
{
	std::array<std::uint32_t, WordCount> words;
};

template <>
struct message_table_data<0, 0>
{};

/// The field table of a message type: distinct compound descriptors followed by
/// 1- or 3-word field entries in ascending field number.
template <typename L>
struct field_table;

template <typename T, typename... M, std::uint32_t... Number, std::size_t... Offset, std::size_t... Alternative>
struct field_table<field_list<T, field_at<M, Number, Offset, Alternative>...>>
{
	static constexpr std::size_t entry_count = sizeof...( M );

	template <typename Target>
	[[nodiscard]] static consteval std::size_t type_id_of()
	{
		if constexpr ( entry_count > 0 )
		{
			const bool same[]{ std::is_same_v<Target, std::remove_const_t<M>>... };
			for ( std::size_t i = 0; i < entry_count; ++i )
				if ( same[i] )
					return i;
		}
		return 0;
	}

	struct plan
	{
		std::size_t subs_n;
		std::size_t words_n;
		std::array<const value_desc*, entry_count> subs;
		std::array<field_entry, entry_count> entries;
	};

	[[nodiscard]] static consteval plan build_plan()
	{
		list_traits<field_list<T, field_at<M, Number, Offset, Alternative>...>>::check();
		struct raw_row
		{
			std::uint32_t number;
			std::size_t offset;
			std::size_t alternative;
			std::size_t type_id;
			std::uint8_t base_op;
			const value_desc* sub;
		};
		std::array<raw_row, entry_count> rows{ raw_row{ Number,
		                                                Offset,
		                                                Alternative,
		                                                type_id_of<std::remove_const_t<M>>(),
		                                                base_op_of<M>(),
		                                                sub_desc_of<M>() }... };
		std::sort( rows.begin(), rows.end(), []( const raw_row& a, const raw_row& b ) { return a.number < b.number; } );

		plan p{};
		std::array<std::size_t, entry_count> sub_ids{};
		// Put a `std::variant` descriptor first (`subs[0]`) so its alternatives fit in 1 word.
		for ( unsigned pass = 0; pass < 2; ++pass )
		{
			for ( std::size_t i = 0; i < entry_count; ++i )
			{
				if ( rows[i].base_op != op_sub_base || ( ( rows[i].alternative != 0 ) != ( pass == 0 ) ) )
					continue;
				bool found = false;
				for ( std::size_t j = 0; j < p.subs_n; ++j )
					if ( sub_ids[j] == rows[i].type_id )
						found = true;
				if ( !found )
				{
					sub_ids[p.subs_n] = rows[i].type_id;
					p.subs[p.subs_n++] = rows[i].sub;
				}
			}
		}

		for ( std::size_t i = 0; i < entry_count; ++i )
		{
			std::uint16_t sub_idx = 0;
			if ( rows[i].base_op == op_sub_base )
			{
				for ( std::size_t j = 0; j < p.subs_n; ++j )
					if ( sub_ids[j] == rows[i].type_id )
						sub_idx = static_cast<std::uint16_t>( j );
			}
			p.entries[i] = field_entry{
				rows[i].number,
				static_cast<std::uint16_t>( rows[i].offset ),
				static_cast<std::uint16_t>( rows[i].alternative ),
				sub_idx,
				rows[i].base_op,
			};
			p.words_n += fits_one_word( p.entries[i] ) ? 1u : 3u;
		}
		return p;
	}

	static constexpr plan layout = build_plan();
	static_assert( layout.subs_n <= 255 && layout.words_n <= 65535 );

	using table_type = message_table_data<layout.subs_n, layout.words_n>;

	[[nodiscard]] static consteval table_type make()
	{
		if constexpr ( layout.subs_n > 0 && layout.words_n > 0 )
			static_assert( offsetof( table_type, words ) == layout.subs_n * sizeof( const value_desc* ) );

		table_type out{};
		if constexpr ( layout.subs_n > 0 )
		{
			for ( std::size_t j = 0; j < layout.subs_n; ++j )
				out.subs[j] = layout.subs[j];
		}
		if constexpr ( layout.words_n > 0 )
		{
			std::size_t w = 0;
			for ( std::size_t i = 0; i < entry_count; ++i )
			{
				const field_entry& e = layout.entries[i];
				if ( fits_one_word( e ) )
				{
					const std::uint32_t op = e.alternative != 0 ? ( std::uint32_t{ op_oneof_base } + e.alternative - 1u )
					                         : e.base_op == op_sub_base ? ( std::uint32_t{ op_sub_base } + e.sub_idx )
					                                                    : std::uint32_t{ e.base_op };
					out.words[w++] = ( op << 24 ) | ( static_cast<std::uint32_t>( e.offset ) << 12 ) | e.number;
				}
				else
				{
					out.words[w++] = 0x80000000u | e.number;
					out.words[w++] = ( static_cast<std::uint32_t>( e.offset ) << 16 ) | e.alternative;
					out.words[w++] = ( static_cast<std::uint32_t>( e.sub_idx ) << 8 ) | e.base_op;
				}
			}
		}
		return out;
	}

	static constexpr table_type value = make();
};

template <typename T>
[[nodiscard]] consteval value_desc describe_value()
{
	constexpr field_kind kind = kind_of<T>();

	if constexpr ( kind == field_kind::SCALAR )
		return { .kind = kind, .scalar = scalar_code_of<T>(), .words = 0, .size = 0 };
	else if constexpr ( kind == field_kind::STRING )
		return { .kind = kind, .scalar = 0, .words = 0, .size = 0 };
	else if constexpr ( kind == field_kind::FIXED_BYTES )
		return { .kind = kind, .scalar = 0, .words = 0, .size = static_cast<std::uint32_t>( sequence_traits<T>::capacity ) };
	else if constexpr ( std::is_same_v<T, std::span<const std::uint8_t>> )
		// Only this exact type is the bytes view that the interpreter copies as
		// one object. A span with a static extent has a different layout: it
		// goes through its accessor, as each other sequence.
		return { .kind = kind, .scalar = 0, .words = 0, .sequence = nullptr };
	else if constexpr ( kind == field_kind::BYTES || kind == field_kind::REPEATED )
		return { .kind = kind, .scalar = 0, .words = 0, .sequence = &sequence_descriptor<T> };
	else if constexpr ( kind == field_kind::MESSAGE )
	{
		// The field list of `T` must name `T`: a list of a different type has the offsets of that type.
		using list = field_list_of<T>;
		static_assert( std::is_same_v<typename list_traits<list>::message, T>,
		               "pb: field_list_type: pb::fields_of<T> must derive from pb::field_list<T, ...> with the same T" );
		const void* table = nullptr;
		if constexpr ( field_table<list>::layout.words_n > 0 )
			table = &field_table<list>::value;
		return {
			.kind = kind,
			.scalar = static_cast<std::uint8_t>( field_table<list>::layout.subs_n ),
			.words = static_cast<std::uint16_t>( field_table<list>::layout.words_n ),
			.table = table,
		};
	}
	else if constexpr ( kind == field_kind::OPTIONAL )
		return { .kind = kind, .scalar = 0, .words = 0, .optional = &optional_descriptor<T> };
	else
		return { .kind = kind, .scalar = 0, .words = 0, .variant = &variant_descriptor<T> };
}

template <typename T>
constexpr value_desc value_descriptor<T>::value = describe_value<T>();

////////////////////////////////////////////////////////////////////////////////
// Part 2: interpreter. No function in this part is a template.

/// Address of the member that is `offset` bytes into `object`.
[[nodiscard]] inline const void* member_at( const void* object, std::size_t offset )
{
	return static_cast<const unsigned char*>( object ) + offset;
}

[[nodiscard]] inline void* member_at( void* object, std::size_t offset )
{
	return static_cast<unsigned char*>( object ) + offset;
}

[[nodiscard]] inline const value_desc* const* message_subs( const value_desc& message )
{
	return static_cast<const value_desc* const*>( message.table );
}

[[nodiscard]] inline const std::uint32_t* message_words( const value_desc& message )
{
	if ( message.table == nullptr )
		return nullptr;
	return static_cast<const std::uint32_t*>(
	  member_at( message.table, std::size_t{ message.scalar } * sizeof( const value_desc* ) ) );
}

[[nodiscard]] constexpr field_entry decode_entry( const std::uint32_t* words, std::size_t& pos )
{
	const std::uint32_t w0 = words[pos++];
	if ( ( w0 & 0x80000000u ) == 0 )
	{
		const auto op = static_cast<std::uint8_t>( w0 >> 24 );
		const auto offset = static_cast<std::uint16_t>( ( w0 >> 12 ) & 0xFFFu );
		const std::uint32_t number = w0 & 0xFFFu;
		if ( op >= op_oneof_base )
			return { number, offset, static_cast<std::uint16_t>( std::uint32_t{ op } - op_oneof_base + 1u ), 0, op_sub_base };
		if ( op >= op_sub_base )
			return { number, offset, 0, static_cast<std::uint16_t>( std::uint32_t{ op } - op_sub_base ), op_sub_base };
		return { number, offset, 0, 0, op };
	}

	const std::uint32_t w1 = words[pos++];
	const std::uint32_t w2 = words[pos++];
	return {
		w0 & 0x7FFFFFFFu,
		static_cast<std::uint16_t>( w1 >> 16 ),
		static_cast<std::uint16_t>( w1 & 0xFFFFu ),
		static_cast<std::uint16_t>( w2 >> 8 ),
		static_cast<std::uint8_t>( w2 & 0xFFu ),
	};
}

[[nodiscard]] constexpr value_desc resolve_type( const value_desc* const* subs, const field_entry& entry )
{
	if ( entry.base_op < op_string )
		return { .kind = field_kind::SCALAR, .scalar = entry.base_op, .words = 0, .size = 0 };
	if ( entry.base_op == op_string )
		return { .kind = field_kind::STRING, .scalar = 0, .words = 0, .size = 0 };
	if ( entry.base_op == op_bytes_view )
		return { .kind = field_kind::BYTES, .scalar = 0, .words = 0, .sequence = nullptr };
	return *subs[entry.sub_idx];
}

[[nodiscard]] inline wire_type wire_of_scalar( std::uint8_t code )
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

[[nodiscard]] inline std::size_t* repeated_count( const sequence_desc& sequence, void* object )
{
	return static_cast<std::size_t*>( member_at( object, sequence.count_offset ) );
}

/// The items of the sequence at `object`. The interpreter writes through the
/// result only when it decodes.
[[nodiscard]] inline sequence_view view_of( const sequence_desc& sequence, void* object )
{
	switch ( sequence.storage )
	{
		case sequence_storage::REPEATED: return { object, *repeated_count( sequence, object ) };
		case sequence_storage::ARRAY: return { object, sequence.capacity };
		case sequence_storage::ACCESSOR: break;
	}
	return sequence.span->access( object );
}

/// Value of the scalar at `value` as the wire carries it: the varint value, or
/// the bits of a fixed-width value. A negative integer sign-extends to 64 bits.
/// An enum is `int32`, so an unsigned enum value of 2^31 or more is negative.
[[nodiscard]] inline std::uint64_t load_scalar( const void* value, std::uint8_t code )
{
	std::uint64_t bits = 0;
	switch ( code & scalar_size_mask )
	{
		case 0: {
			std::uint8_t narrow;
			std::memcpy( &narrow, value, sizeof narrow );
			bits = narrow;
			break;
		}
		case 1: {
			std::uint16_t narrow;
			std::memcpy( &narrow, value, sizeof narrow );
			bits = narrow;
			break;
		}
		case 2: {
			std::uint32_t narrow;
			std::memcpy( &narrow, value, sizeof narrow );
			bits = narrow;
			break;
		}
		default: std::memcpy( &bits, value, sizeof bits );
	}

	if ( code & scalar_signed )
	{
		const unsigned shift = 64 - ( 8u << ( code & scalar_size_mask ) );
		bits = static_cast<std::uint64_t>( static_cast<std::int64_t>( bits << shift ) >> shift );
	}
	if ( code & scalar_enum )
		bits = static_cast<std::uint64_t>( static_cast<std::int64_t>( static_cast<std::int32_t>( bits ) ) );
	if ( code & scalar_zigzag )
		bits = zigzag_encode( static_cast<std::int64_t>( bits ) );
	return bits;
}

/// Writes the value of a scalar without a key.
inline void emit_scalar( writer& sink, std::uint8_t code, std::uint64_t bits )
{
	switch ( wire_of_scalar( code ) )
	{
		case wire_type::FIXED32: sink.write_fixed32( static_cast<std::uint32_t>( bits ) ); break;
		case wire_type::FIXED64: sink.write_fixed64( bits ); break;
		default: sink.write_varint( bits );
	}
}

/// Writes a string or bytes field. `always` is `true` for explicit presence.
inline void emit_bytes( writer& sink, std::uint32_t number, std::span<const std::uint8_t> bytes, bool always )
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
MICROPB_ENTRY inline std::size_t encoded_size( const value_desc& message, const void* object )
{
	writer sink;
	emit_message( sink, message, object );
	return sink.size();
}

/// Writes a field of a message type: the key, the length prefix and the fields.
inline void emit_submessage( writer& sink, std::uint32_t number, const value_desc& message, const void* object )
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
	const std::size_t start = sink.size();
	emit_message( sink, message, object );
	sink.advance( varint_size( sink.size() - start ) );
}

/// Writes the scalar elements of a repeated field as one packed field. `items`
/// must not be empty: with no items, the function writes a key and the length
/// 0, which `protoc` does not write.
inline void emit_packed( writer& sink, std::uint32_t number, const sequence_desc& sequence, const sequence_view& items )
{
	const std::uint8_t code = sequence.element->scalar;

	// The length prefix comes before the values, so measure them first.
	writer measure;
	if ( code & scalar_fixed )
	{
		measure.advance( items.count * sequence.element_size );
	}
	else
	{
		for ( std::size_t i = 0; i < items.count; ++i )
			measure.write_varint( load_scalar( member_at( items.data, i * sequence.element_size ), code ) );
	}

	sink.write_key( number, wire_type::LENGTH_DELIMITED );
	sink.write_varint( measure.size() );
	for ( std::size_t i = 0; i < items.count; ++i )
	{
		const void* item = member_at( items.data, i * sequence.element_size );
		emit_scalar( sink, code, load_scalar( item, code ) );
	}
}

/// Writes one field. `always` is `true` for explicit presence: the function
/// writes the field also when its value is the default value.
inline void emit_value( writer& sink, std::uint32_t number, const value_desc& type, const void* value, bool always )
{
	switch ( type.kind )
	{
		case field_kind::SCALAR: {
			// Zero bits mean the default value. `-0.0` has a sign bit, so the codec writes it.
			const std::uint64_t bits = load_scalar( value, type.scalar );
			if ( always || bits != 0 )
			{
				sink.write_key( number, wire_of_scalar( type.scalar ) );
				emit_scalar( sink, type.scalar, bits );
			}
			break;
		}
		case field_kind::STRING: {
			std::string_view text;
			std::memcpy( static_cast<void*>( &text ), value, sizeof text );
			emit_bytes( sink, number, { reinterpret_cast<const std::uint8_t*>( text.data() ), text.size() }, always );
			break;
		}
		case field_kind::BYTES: {
			std::span<const std::uint8_t> bytes;
			if ( type.sequence == nullptr )
			{
				std::memcpy( static_cast<void*>( &bytes ), value, sizeof bytes );
			}
			else
			{
				const sequence_view items = view_of( *type.sequence, const_cast<void*>( value ) );
				bytes = { static_cast<const std::uint8_t*>( items.data ), items.count };
			}
			emit_bytes( sink, number, bytes, always );
			break;
		}
		case field_kind::FIXED_BYTES:
			// The length is never 0 here, except for an array of length 0.
			emit_bytes( sink, number, { static_cast<const std::uint8_t*>( value ), type.size }, always );
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
				for ( std::size_t i = 0; i < items.count; ++i )
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
	const std::uint32_t* words = message_words( message );
	for ( std::size_t pos = 0; pos < message.words; )
	{
		const field_entry field = decode_entry( words, pos );
		const value_desc type = resolve_type( subs, field );
		const void* member = member_at( object, field.offset );
		if ( field.alternative == 0 )
		{
			emit_value( sink, field.number, type, member, false );
			continue;
		}

		const variant_desc& variant = *type.variant;
		if ( const void* active = variant.access( const_cast<void*>( member ), field.alternative, false ) )
			emit_value( sink, field.number, *variant.alternatives[field.alternative], active, true );
	}
}

MICROPB_ENTRY inline std::expected<std::size_t, error> encode(
  const value_desc& message, const void* object, std::span<std::uint8_t> out )
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
inline void store_scalar( void* value, std::uint8_t code, std::uint64_t bits )
{
	if ( code & scalar_zigzag )
	{
		if ( ( code & scalar_size_mask ) == 2 )
			bits = static_cast<std::uint32_t>( zigzag_decode( static_cast<std::uint32_t>( bits ) ) );
		else
			bits = static_cast<std::uint64_t>( zigzag_decode( bits ) );
	}
	if ( code & scalar_bool )
		bits = bits != 0;

	switch ( code & scalar_size_mask )
	{
		case 0: {
			const auto narrow = static_cast<std::uint8_t>( bits );
			std::memcpy( value, &narrow, sizeof narrow );
			break;
		}
		case 1: {
			const auto narrow = static_cast<std::uint16_t>( bits );
			std::memcpy( value, &narrow, sizeof narrow );
			break;
		}
		case 2: {
			const auto narrow = static_cast<std::uint32_t>( bits );
			std::memcpy( value, &narrow, sizeof narrow );
			break;
		}
		default: std::memcpy( value, &bits, sizeof bits );
	}
}

/// Reads the value of a scalar without a key. Returns 0 when the read fails.
[[nodiscard]] inline std::uint64_t read_scalar( reader& in, std::uint8_t code )
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
		case field_kind::SCALAR: std::memset( value, 0, std::size_t{ 1 } << ( type.scalar & scalar_size_mask ) ); break;
		case field_kind::FIXED_BYTES: std::memset( value, 0, type.size ); break;
		case field_kind::STRING: {
			const std::string_view empty;
			std::memcpy( value, static_cast<const void*>( &empty ), sizeof empty );
			break;
		}
		case field_kind::BYTES:
			if ( type.sequence == nullptr )
			{
				const std::span<const std::uint8_t> empty;
				std::memcpy( value, static_cast<const void*>( &empty ), sizeof empty );
				break;
			}
			[[fallthrough]];
		case field_kind::REPEATED:
			if ( type.sequence->span != nullptr && type.sequence->span->bind != nullptr )
				type.sequence->span->bind( value, nullptr, 0 );
			else if ( type.sequence->storage == sequence_storage::REPEATED )
				*repeated_count( *type.sequence, value ) = 0;
			break;
		case field_kind::MESSAGE: reset_message( type, value ); break;
		case field_kind::OPTIONAL: static_cast<void>( type.optional->access( value, optional_op::RESET ) ); break;
		case field_kind::ONEOF: static_cast<void>( type.variant->access( value, 0, true ) ); break;
	}
}

/// Resets each member of the message at `object`, also in a nested message.
inline void reset_message( const value_desc& message, void* object )
{
	const value_desc* const* subs = message_subs( message );
	const std::uint32_t* words = message_words( message );
	for ( std::size_t pos = 0; pos < message.words; )
	{
		// A `std::variant` member has one entry for each alternative. Reset it one time.
		const field_entry field = decode_entry( words, pos );
		if ( field.alternative <= 1 )
		{
			const value_desc type = resolve_type( subs, field );
			reset_value( type, member_at( object, field.offset ) );
		}
	}
}

inline void decode_message( reader& in, const value_desc& message, void* object, arena* memory );
inline void decode_value( reader& in, wire_type wire, const value_desc& type, void* value, arena* memory );

/// Capacity of the arena block at `data`: a `std::size_t` just before the first
/// item. 0 for a span without a block.
[[nodiscard]] inline std::size_t block_capacity( const void* data )
{
	std::size_t capacity = 0;
	if ( data != nullptr )
		std::memcpy( &capacity, static_cast<const unsigned char*>( data ) - sizeof capacity, sizeof capacity );
	return capacity;
}

/// Capacity of the sequence at `items` for `decode`. The capacity of a span is
/// the capacity of its arena block.
[[nodiscard]] inline std::size_t decode_capacity( const sequence_desc& sequence, const sequence_view& items )
{
	if ( sequence.span != nullptr && sequence.span->bind != nullptr )
		return block_capacity( items.data );
	return sequence.capacity;
}

/// Elements of a repeated field in the value of one record, which the function
/// reads from `in`. 0 for a wrong wire type and for a read that fails.
[[nodiscard]] inline std::size_t count_record( reader& in, wire_type wire, const sequence_desc& sequence )
{
	const value_desc& element = *sequence.element;
	if ( element.kind == field_kind::SCALAR && wire == wire_type::LENGTH_DELIMITED )
	{
		const std::span<const std::uint8_t> bytes = in.read_length_delimited();
		if ( element.scalar & scalar_fixed )
			return bytes.size() >> ( element.scalar & scalar_size_mask );

		// Each varint ends with a byte without bit 7.
		const auto last = []( std::uint8_t byte ) { return ( byte & 0x80 ) == 0; };
		return static_cast<std::size_t>( std::count_if( bytes.begin(), bytes.end(), last ) );
	}

	const bool match = wire == wire_of( element );
	in.skip( wire );
	return match && !in.failed() ? 1 : 0;
}

/// Elements of the field `key.number` from the record at `in` to the end of the
/// message. `in` is a copy of the reader after the key of the record. A read
/// that fails ends the count.
[[nodiscard]] inline std::size_t count_elements( reader in, field_key key, const sequence_desc& sequence )
{
	std::size_t count = count_record( in, key.type, sequence );
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
  const sequence_desc& sequence, void* value, const sequence_view& items, std::size_t capacity, arena* memory )
{
	// The block has `header` bytes with the capacity at their end, then the items.
	const std::size_t alignment = std::max<std::size_t>( sequence.span->element_align, alignof( std::size_t ) );
	const std::size_t header = ( sizeof( std::size_t ) + alignment - 1 ) & ~( alignment - 1 );
	if ( memory == nullptr || capacity > ( std::size_t( -1 ) - header ) / sequence.element_size )
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
	const std::size_t capacity = decode_capacity( sequence, items );
	if ( items.count > capacity )
		return false;

	reader record = in;
	const std::size_t needed = count_record( record, key.type, sequence );
	if ( record.failed() )
	{
		in.absorb( record );
		return true;
	}
	if ( needed <= capacity - items.count )
		return true;
	return grow( sequence, value, items, items.count + count_elements( in, key, sequence ), memory );
}

/// Reads one element from `in` into the next free item of the sequence at
/// `value`. A full sequence is an error: the function examines the capacity
/// before it reads the element. A full span block means that the count of the
/// elements was too small, thus the input is not valid. The item starts from
/// its initial state: records of a repeated field do not merge.
inline void decode_element( reader& in, wire_type wire, const sequence_desc& sequence, void* value, arena* memory )
{
	const sequence_view items = view_of( sequence, value );
	const bool is_span = sequence.span != nullptr && sequence.span->bind != nullptr;
	if ( items.count >= decode_capacity( sequence, items ) )
		return in.fail( is_span ? error::TRUNCATED : error::CAPACITY_EXCEEDED );

	void* item = member_at( items.data, items.count * sequence.element_size );
	if ( sequence.span != nullptr && sequence.span->construct != nullptr )
		sequence.span->construct( item );
	reset_value( *sequence.element, item );
	decode_value( in, wire, *sequence.element, item, memory );
	if ( in.failed() )
		return;

	if ( is_span )
		sequence.span->bind( value, items.data, items.count + 1 );
	else
		*repeated_count( sequence, value ) = items.count + 1;
}

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
		reader packed( in.read_length_delimited() );
		while ( !packed.empty() )
			decode_element( packed, wire_of( element ), sequence, value, memory );
		in.absorb( packed );
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
		const std::uint64_t bits = read_scalar( in, type.scalar );
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
	const std::span<const std::uint8_t> bytes = in.read_length_delimited();
	if ( in.failed() )
		return;

	switch ( type.kind )
	{
		case field_kind::STRING: {
			const std::string_view text( reinterpret_cast<const char*>( bytes.data() ), bytes.size() );
			std::memcpy( value, static_cast<const void*>( &text ), sizeof text );
			break;
		}
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
			*repeated_count( *type.sequence, value ) = bytes.size();
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

/// Finds the entry of the field `number` in `words`. The search starts at the
/// word index `cursor`. After the end of the table, it continues at 0. After a
/// match, `cursor` is the start word index of the entry.
[[nodiscard]] inline bool find_field( const std::uint32_t* words,
                                      std::size_t total_words,
                                      std::uint32_t number,
                                      std::size_t& cursor,
                                      field_entry& out )
{
	for ( unsigned pass = 0; pass < 2; ++pass )
	{
		std::size_t pos = pass == 0 ? cursor : 0;
		const std::size_t stop = pass == 0 ? total_words : cursor;
		while ( pos < stop )
		{
			const std::uint32_t w0 = words[pos];
			const bool wide = ( w0 & 0x80000000u ) != 0;
			const std::uint32_t entry_number = wide ? ( w0 & 0x7FFFFFFFu ) : ( w0 & 0xFFFu );
			if ( entry_number == number )
			{
				cursor = pos;
				out = decode_entry( words, pos );
				return true;
			}
			pos += wide ? 3u : 1u;
		}
	}
	return false;
}

/// Reads all fields of `in` into the message at `object`, until the end of
/// the input or the first error. Does not reset the message: a field that
/// occurs again changes the value that is there.
inline void decode_message( reader& in, const value_desc& message, void* object, arena* memory )
{
	const value_desc* const* subs = message_subs( message );
	const std::uint32_t* words = message_words( message );
	// Fields usually arrive in ascending order, and a repeated field arrives
	// many times. The search starts at the entry of the last match.
	std::size_t cursor = 0;
	while ( !in.empty() )
	{
		const field_key key = in.read_key();
		if ( in.failed() )
			return;

		field_entry field{};
		if ( !find_field( words, message.words, key.number, cursor, field ) )
		{
			in.skip( key.type );
			continue;
		}

		const value_desc type = resolve_type( subs, field );
		void* member = member_at( object, field.offset );
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
		const variant_desc& variant = *type.variant;
		const value_desc& alternative = *variant.alternatives[field.alternative];
		if ( key.type != wire_of( alternative ) )
		{
			in.skip( key.type );
			continue;
		}

		void* active = variant.access( member, field.alternative, false );
		if ( active == nullptr )
		{
			active = variant.access( member, field.alternative, true );
			reset_value( alternative, active );
		}
		decode_value( in, key.type, alternative, active, memory );
	}
}

/// `memory` is null for a message without a `std::span<const T>` repeated field.
MICROPB_ENTRY inline std::expected<std::size_t, error> decode(
  const value_desc& message, std::span<const std::uint8_t> in, void* object, arena* memory )
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
[[nodiscard]] std::size_t encoded_size( const T& msg )
{
	return detail::encoded_size( detail::value_descriptor<T>::value, &msg );
}

/// Encodes `msg` into `out` in proto3 wire format and returns the number of
/// bytes that it wrote. Does not write past `out`. When `out` is too small, returns
/// `error::BUFFER_TOO_SMALL`; the bytes of `out` that it wrote are not valid.
template <typename T>
[[nodiscard]] std::expected<std::size_t, error> encode( const T& msg, std::span<std::uint8_t> out )
{
	return detail::encode( detail::value_descriptor<T>::value, &msg, out );
}

/// Decodes the proto3 message in `in` into `msg` and returns `in.size()`. Resets
/// `msg` first. On error, `msg` holds the fields that `decode` read before the
/// error. A `std::string_view` or a `std::span<const std::uint8_t>` of `msg`
/// points into `in`. The items of each `std::span<const T>` repeated field are
/// in one block of `memory`. `in` and `memory` must stay valid while `msg` is
/// in use. When `memory` is too small, returns `error::ARENA_FULL`. `decode`
/// does not free the blocks of `memory`.
template <typename T>
[[nodiscard]] std::expected<std::size_t, error> decode( std::span<const std::uint8_t> in, T& msg, arena& memory )
{
	detail::check_decodable<T>();
	return detail::decode( detail::value_descriptor<T>::value, in, &msg, &memory );
}

/// `decode` for a message without a `std::span<const T>` repeated field, also
/// not in a nested message.
template <typename T>
[[nodiscard]] std::expected<std::size_t, error> decode( std::span<const std::uint8_t> in, T& msg )
{
	detail::check_decodable<T>();
	detail::check_no_arena<T>();
	return detail::decode( detail::value_descriptor<T>::value, in, &msg, nullptr );
}

} // namespace pb
