#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <micropb/meta_schema.hpp>
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

// A scalar code tells the interpreter how to load and store a scalar. The low
// two bits are log2 of the size in bytes. The other bits are flags.
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
	else if constexpr ( detail::is_zigzag<T> )
		return size | scalar_signed | scalar_zigzag;
	else if constexpr ( wire_type_of<T>() != wire_type::VARINT )
		return size | scalar_fixed;
	else
		return size | ( std::is_signed_v<T> ? scalar_signed : 0 );
}

struct message_desc;
struct sequence_desc;
struct optional_desc;
struct variant_desc;

/// How to read and write a value of one type. All members, elements, optional
/// values and alternatives of the same type share one descriptor.
struct value_desc
{
	field_kind kind;
	/// SCALAR: the scalar code.
	std::uint8_t scalar;
	union
	{
		/// FIXED_BYTES: the number of bytes. 0 for SCALAR and STRING.
		std::uint32_t size;
		/// MESSAGE.
		const message_desc* message;
		/// REPEATED and BYTES. Null for the view `std::span<const std::uint8_t>`.
		const sequence_desc* sequence;
		/// OPTIONAL.
		const optional_desc* optional;
		/// ONEOF.
		const variant_desc* variant;
	};
};

/// One entry of the tag table: a member of a message, or one alternative of a
/// `std::variant` member.
struct field_desc
{
	std::uint32_t number;
	/// Offset of the member in the message, in bytes.
	std::uint16_t offset;
	/// 0 for a plain member, else the index of the alternative.
	std::uint16_t alternative;
	/// Type of the member. For an alternative, this is the `std::variant`.
	const value_desc* type;
};

/// The fields of a message type, sorted by field number.
struct message_desc
{
	const field_desc* fields;
	std::uint16_t count;
};

/// The items of a sequence at this moment.
struct sequence_view
{
	void* data;
	std::size_t count;
	std::size_t capacity;
	/// The count that `decode` changes. Null for a type that is encode-only.
	std::size_t* stored_count;
};

using sequence_accessor = sequence_view ( * )( void* sequence );

enum class sequence_storage : std::uint8_t
{
	/// `pb::repeated`: the items are at offset 0, the count is at `count_offset`.
	REPEATED,
	/// `std::array`: the items are at offset 0, the count is `capacity`.
	ARRAY,
	/// `std::span<const T>` and `pb::span_buffer<T>`: `access` gives the items.
	ACCESSOR,
};

struct sequence_desc
{
	const value_desc* element;
	sequence_accessor access;
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

static_assert( sizeof( field_desc ) <= ( sizeof( void* ) == 4 ? 12 : 16 ) );

template <typename T>
struct value_descriptor
{
	static const value_desc value;
};

template <typename T>
struct message_descriptor
{
	static const message_desc value;
};

////////////////////////////////////////////////////////////////////////////////

/// The count member of `pb::repeated` and `pb::span_buffer` is private. The
/// codec reads and writes it through this friend of the two types.
struct sequence_access
{
	template <typename S>
	static constexpr std::size_t items_offset = offsetof( S, _items );
	template <typename S>
	static constexpr std::size_t count_offset = offsetof( S, _count );

	template <typename T>
	static std::size_t* count_of( span_buffer<T>& buffer )
	{
		return &buffer._count;
	}
};

/// Accessor of `std::span<const T>` and `pb::span_buffer<T>`. These types keep
/// their items in a different object, so the interpreter cannot find the items
/// from an offset.
template <typename S>
sequence_view access_sequence( void* sequence )
{
	S& items = *static_cast<S*>( sequence );
	if constexpr ( is_span_buffer<S> )
	{
		return { items.begin(), items.size(), items.capacity(), sequence_access::count_of( items ) };
	}
	else
	{
		const void* data = items.data();
		return { const_cast<void*>( data ), items.size(), items.size(), nullptr };
	}
}

/// A value type with a `const` member cannot be made again in its place. Such
/// a type is encode-only (`check_decodable`), and `ENGAGE` does nothing for it.
template <typename O>
void* access_optional( void* optional, optional_op op )
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

/// The address of alternative `I` of `value` when it is active, else null.
/// With `select`, makes it active first.
template <std::size_t I, typename V>
void* alternative_at( V& value, bool select )
{
	// An alternative with a `const` member is encode-only: no `emplace`.
	if ( select && value.index() != I )
	{
		if constexpr ( requires { value.template emplace<I>(); } )
			value.template emplace<I>();
	}
	return std::get_if<I>( &value );
}

template <typename V, std::size_t... I>
void* access_alternative( V& value, std::size_t index, bool select, std::index_sequence<I...> )
{
	void* result = nullptr;
	( void )( ( index == I && ( result = alternative_at<I>( value, select ), true ) ) || ... );
	return result;
}

template <typename V>
void* access_variant( void* variant, std::size_t index, bool select )
{
	return access_alternative(
	  *static_cast<V*>( variant ), index, select, std::make_index_sequence<std::variant_size_v<V>>{} );
}

////////////////////////////////////////////////////////////////////////////////

template <typename S>
consteval sequence_desc describe_sequence()
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
		return { item, access_sequence<S>, item_size, 0, 0, sequence_storage::ACCESSOR };
	}
}

template <typename S>
inline constexpr sequence_desc sequence_descriptor = describe_sequence<S>();

template <typename O>
inline constexpr optional_desc optional_descriptor{ &value_descriptor<typename O::value_type>::value,
                                                    access_optional<O> };

/// The descriptor of the alternative `A` of a `std::variant`. Null for `std::monostate`.
template <typename A>
consteval const value_desc* describe_alternative()
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

/// The field table of a message type: one entry for each entry of its field
/// list, sorted by field number.
template <typename L>
struct field_table;

template <typename T, typename... M, std::uint32_t... Number, std::size_t... Offset, std::size_t... Alternative>
struct field_table<field_list<T, field_at<M, Number, Offset, Alternative>...>>
{
	static consteval std::array<field_desc, sizeof...( M )> make()
	{
		list_traits<field_list<T, field_at<M, Number, Offset, Alternative>...>>::check();

		std::array<field_desc, sizeof...( M )> fields{ field_desc{ Number,
		                                                           static_cast<std::uint16_t>( Offset ),
		                                                           static_cast<std::uint16_t>( Alternative ),
		                                                           &value_descriptor<std::remove_const_t<M>>::value }... };
		std::sort(
		  fields.begin(), fields.end(), []( const field_desc& a, const field_desc& b ) { return a.number < b.number; } );
		return fields;
	}

	static constexpr std::array<field_desc, sizeof...( M )> value = make();
};

template <typename T>
consteval value_desc describe_value()
{
	constexpr field_kind kind = kind_of<T>();

	if constexpr ( kind == field_kind::SCALAR )
		return { .kind = kind, .scalar = scalar_code_of<T>(), .size = 0 };
	else if constexpr ( kind == field_kind::STRING )
		return { .kind = kind, .scalar = 0, .size = 0 };
	else if constexpr ( kind == field_kind::FIXED_BYTES )
		return { .kind = kind, .scalar = 0, .size = static_cast<std::uint32_t>( sequence_traits<T>::capacity ) };
	else if constexpr ( std::is_same_v<T, std::span<const std::uint8_t>> )
		// Only this exact type is the bytes view that the interpreter copies as
		// one object. A span with a static extent has a different layout: it
		// goes through its accessor, as each other sequence.
		return { .kind = kind, .scalar = 0, .sequence = nullptr };
	else if constexpr ( kind == field_kind::BYTES || kind == field_kind::REPEATED )
		return { .kind = kind, .scalar = 0, .sequence = &sequence_descriptor<T> };
	else if constexpr ( kind == field_kind::MESSAGE )
		return { .kind = kind, .scalar = 0, .message = &message_descriptor<T>::value };
	else if constexpr ( kind == field_kind::OPTIONAL )
		return { .kind = kind, .scalar = 0, .optional = &optional_descriptor<T> };
	else
		return { .kind = kind, .scalar = 0, .variant = &variant_descriptor<T> };
}

template <typename T>
constexpr value_desc value_descriptor<T>::value = describe_value<T>();

/// The field list of `T` must name `T`: a list of a different type has the offsets of that type.
template <typename T>
consteval message_desc describe_message()
{
	using list = field_list_of<T>;
	static_assert( std::is_same_v<typename list_traits<list>::message, T>,
	               "pb: field_list_type: pb::fields_of<T> must derive from pb::field_list<T, ...> with the same T" );
	return { field_table<list>::value.data(), static_cast<std::uint16_t>( field_table<list>::value.size() ) };
}

template <typename T>
constexpr message_desc message_descriptor<T>::value = describe_message<T>();

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

/// The items of the sequence at `object`. The interpreter writes through the
/// result only when it decodes.
[[nodiscard]] inline sequence_view view_of( const sequence_desc& sequence, void* object )
{
	switch ( sequence.storage )
	{
		case sequence_storage::REPEATED: {
			auto* count = static_cast<std::size_t*>( member_at( object, sequence.count_offset ) );
			return { object, *count, sequence.capacity, count };
		}
		case sequence_storage::ARRAY: return { object, sequence.capacity, sequence.capacity, nullptr };
		case sequence_storage::ACCESSOR: break;
	}
	return sequence.access( object );
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

inline void emit_message( writer& sink, const message_desc& message, const void* object );

// The three entry points of the interpreter are never inlined and never cloned.
// When the compiler puts the interpreter into its caller, it makes a copy of
// the interpreter for each message type, which is what the tables prevent. It
// also examines the paths of other message types against the type that it
// knows at the call, and gives warnings about accesses that never occur.

/// Encoded size of the fields of the message at `object`, without key and
/// length prefix. A writer without a span only counts, so the size and the
/// bytes come from one code path.
[[nodiscard, gnu::noinline, gnu::noclone]] inline std::size_t encoded_size( const message_desc& message,
                                                                            const void* object )
{
	writer sink;
	emit_message( sink, message, object );
	return sink.size();
}

/// Writes one field. `always` is `true` for explicit presence: the field is
/// written also when its value is the default value.
inline void emit_value( writer& sink, std::uint32_t number, const value_desc& type, const void* value, bool always )
{
	switch ( type.kind )
	{
		case field_kind::SCALAR: {
			// Zero bits mean the default value. `-0.0` has a sign bit, so it is written.
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
		case field_kind::MESSAGE: {
			sink.write_key( number, wire_type::LENGTH_DELIMITED );
			if ( !sink.counts_only() )
			{
				// The length prefix comes before the fields, so measure them first.
				sink.write_varint( encoded_size( *type.message, value ) );
				emit_message( sink, *type.message, value );
			}
			else
			{
				// No byte is written, so the order is free: count the fields, then
				// the length prefix. This walks the message only one time.
				const std::size_t start = sink.size();
				emit_message( sink, *type.message, value );
				sink.advance( varint_size( sink.size() - start ) );
			}
			break;
		}
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

			// Scalars are one packed field: measure the values, then write them.
			writer measure;
			if ( element.scalar & scalar_fixed )
			{
				measure.advance( items.count * sequence.element_size );
			}
			else
			{
				for ( std::size_t i = 0; i < items.count; ++i )
					measure.write_varint( load_scalar( member_at( items.data, i * sequence.element_size ), element.scalar ) );
			}

			sink.write_key( number, wire_type::LENGTH_DELIMITED );
			sink.write_varint( measure.size() );
			for ( std::size_t i = 0; i < items.count; ++i )
			{
				const void* item = member_at( items.data, i * sequence.element_size );
				emit_scalar( sink, element.scalar, load_scalar( item, element.scalar ) );
			}
			break;
		}
		case field_kind::ONEOF:
			// `emit_message` writes the active alternative of a oneof.
			break;
	}
}

/// Writes the fields of the message at `object` in ascending field number.
inline void emit_message( writer& sink, const message_desc& message, const void* object )
{
	for ( std::size_t i = 0; i < message.count; ++i )
	{
		const field_desc& field = message.fields[i];
		const void* member = member_at( object, field.offset );
		if ( field.alternative == 0 )
		{
			emit_value( sink, field.number, *field.type, member, false );
			continue;
		}

		const variant_desc& variant = *field.type->variant;
		if ( const void* active = variant.access( const_cast<void*>( member ), field.alternative, false ) )
			emit_value( sink, field.number, *variant.alternatives[field.alternative], active, true );
	}
}

[[nodiscard, gnu::noinline, gnu::noclone]] inline std::expected<std::size_t, error> encode(
  const message_desc& message, const void* object, std::span<std::uint8_t> out )
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

inline void reset_message( const message_desc& message, void* object );

/// Sets the value at `value` to its initial state. A sequence keeps its
/// storage and only gets the count 0.
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
			if ( std::size_t* count = view_of( *type.sequence, value ).stored_count )
				*count = 0;
			break;
		case field_kind::MESSAGE: reset_message( *type.message, value ); break;
		case field_kind::OPTIONAL: static_cast<void>( type.optional->access( value, optional_op::RESET ) ); break;
		case field_kind::ONEOF: static_cast<void>( type.variant->access( value, 0, true ) ); break;
	}
}

/// Resets each member of the message at `object`, also in a nested message.
inline void reset_message( const message_desc& message, void* object )
{
	for ( std::size_t i = 0; i < message.count; ++i )
	{
		// A `std::variant` member has one entry for each alternative. Reset it one time.
		const field_desc& field = message.fields[i];
		if ( field.alternative <= 1 )
			reset_value( *field.type, member_at( object, field.offset ) );
	}
}

inline void decode_message( reader& in, const message_desc& message, void* object );
inline void decode_value( reader& in, wire_type wire, const value_desc& type, void* value );

/// Reads one element from `in` into the next free item of the sequence at
/// `value`. A full sequence is an error, before the element is read. The item
/// starts from its initial state: records of a repeated field do not merge.
inline void decode_element( reader& in, wire_type wire, const sequence_desc& sequence, void* value )
{
	const sequence_view items = view_of( sequence, value );
	if ( items.count == items.capacity )
		return in.fail( error::CAPACITY_EXCEEDED );

	void* item = member_at( items.data, items.count * sequence.element_size );
	reset_value( *sequence.element, item );
	decode_value( in, wire, *sequence.element, item );
	if ( !in.failed() )
		*items.stored_count = items.count + 1;
}

/// Reads one record of a repeated field. A scalar element can be a packed
/// record or a single value, also mixed in one message.
inline void decode_repeated( reader& in, wire_type wire, const sequence_desc& sequence, void* value )
{
	const value_desc& element = *sequence.element;
	if ( element.kind == field_kind::SCALAR && wire == wire_type::LENGTH_DELIMITED )
	{
		reader packed( in.read_length_delimited() );
		while ( !packed.empty() )
			decode_element( packed, wire_of( element ), sequence, value );
		in.absorb( packed );
	}
	else if ( wire == wire_of( element ) )
	{
		decode_element( in, wire, sequence, value );
	}
	else
	{
		in.skip( wire );
	}
}

/// Reads the value of one field into `value`. A field with the wrong wire type
/// is skipped, as an unknown field. A read that fails does not change a scalar,
/// a string or a bytes field.
inline void decode_value( reader& in, wire_type wire, const value_desc& type, void* value )
{
	if ( type.kind == field_kind::REPEATED )
		return decode_repeated( in, wire, *type.sequence, value );
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
		const optional_desc& optional = *type.optional;
		return decode_value( in, wire, *optional.value, optional.access( value, optional_op::ENGAGE ) );
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

			const sequence_view items = view_of( *type.sequence, value );
			if ( bytes.size() > items.capacity )
				return in.fail( error::CAPACITY_EXCEEDED );
			if ( !bytes.empty() )
				std::memcpy( items.data, bytes.data(), bytes.size() );
			*items.stored_count = bytes.size();
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
			// The message is not reset: a second occurrence merges into the first.
			reader part( bytes );
			decode_message( part, *type.message, value );
			in.absorb( part );
			break;
		}
		default: break;
	}
}

/// Reads all fields of `in` into the message at `object`, until the end of
/// the input or the first error. Does not reset the message: a field that
/// occurs again changes the value that is there.
inline void decode_message( reader& in, const message_desc& message, void* object )
{
	// Fields usually arrive in ascending order, and a repeated field arrives
	// many times. The search starts at the entry of the last match.
	std::size_t last = 0;
	while ( !in.empty() )
	{
		const field_key key = in.read_key();
		if ( in.failed() )
			return;

		const field_desc* field = nullptr;
		for ( std::size_t tried = 0; tried < message.count && field == nullptr; ++tried )
		{
			const std::size_t index = last + tried < message.count ? last + tried : last + tried - message.count;
			if ( message.fields[index].number == key.number )
			{
				field = &message.fields[index];
				last = index;
			}
		}
		if ( field == nullptr )
		{
			in.skip( key.type );
			continue;
		}

		void* member = member_at( object, field->offset );
		if ( field->alternative == 0 )
		{
			decode_value( in, key.type, *field->type, member );
			continue;
		}

		// A different active alternative gives way to a value-initialized one.
		// The same alternative stays, so a message merges. A wrong wire type
		// is skipped and keeps the variant.
		const variant_desc& variant = *field->type->variant;
		const value_desc& alternative = *variant.alternatives[field->alternative];
		if ( key.type != wire_of( alternative ) )
			in.skip( key.type );
		else
			decode_value( in, key.type, alternative, variant.access( member, field->alternative, true ) );
	}
}

[[nodiscard, gnu::noinline, gnu::noclone]] inline std::expected<std::size_t, error> decode(
  const message_desc& message, std::span<const std::uint8_t> in, void* object )
{
	reset_message( message, object );

	reader source( in );
	decode_message( source, message, object );
	if ( source.failed() )
		return std::unexpected( source.status().error() );
	return in.size();
}

} // namespace detail

////////////////////////////////////////////////////////////////////////////////

/// Size in bytes of the encoding of `msg`.
template <typename T>
[[nodiscard]] std::size_t encoded_size( const T& msg )
{
	return detail::encoded_size( detail::message_descriptor<T>::value, &msg );
}

/// Encodes `msg` into `out` in proto3 wire format and returns the number of
/// bytes written. Does not write past `out`. When `out` is too small, returns
/// `error::BUFFER_TOO_SMALL`; the bytes of `out` that it wrote are not valid.
template <typename T>
[[nodiscard]] std::expected<std::size_t, error> encode( const T& msg, std::span<std::uint8_t> out )
{
	return detail::encode( detail::message_descriptor<T>::value, &msg, out );
}

/// Decodes the proto3 message in `in` into `msg` and returns `in.size()`. Resets
/// `msg` first. A `pb::span_buffer` keeps its storage. On error, `msg` holds the
/// fields that were read before the error. A `std::string_view` or a
/// `std::span` of `msg` points into `in`.
template <typename T>
[[nodiscard]] std::expected<std::size_t, error> decode( std::span<const std::uint8_t> in, T& msg )
{
	detail::check_decodable<T>();
	return detail::decode( detail::message_descriptor<T>::value, in, &msg );
}

} // namespace pb
