#pragma once

#include "messages.hpp"

#include <array>
#include <limits>
#include <stdint.h>
#include <string_view>

/// C++ values of the `vectors.hpp` cases that the encode and decode tests share.
namespace cases {

using namespace messages;

constexpr scalars scalars_max()
{
	scalars v{};
	v.i32 = std::numeric_limits<int32_t>::max();
	v.i64 = std::numeric_limits<int64_t>::max();
	v.u32 = std::numeric_limits<uint32_t>::max();
	v.u64 = std::numeric_limits<uint64_t>::max();
	v.s32 = std::numeric_limits<int32_t>::max();
	v.s64 = std::numeric_limits<int64_t>::max();
	v.flag = true;
	v.f = std::numeric_limits<float>::max();
	v.d = std::numeric_limits<double>::max();
	v.fx32 = std::numeric_limits<uint32_t>::max();
	v.fx64 = std::numeric_limits<uint64_t>::max();
	v.sfx32 = std::numeric_limits<int32_t>::max();
	v.sfx64 = std::numeric_limits<int64_t>::max();
	v.c = color::BLUE;
	v.i8 = std::numeric_limits<int8_t>::max();
	v.u16 = std::numeric_limits<uint16_t>::max();
	return v;
}

constexpr scalars scalars_negative()
{
	scalars v{};
	v.i32 = -1;
	v.i64 = -1;
	v.s32 = -1;
	v.s64 = -1;
	v.f = -0.0f;
	v.sfx32 = -1;
	v.sfx64 = -1;
	v.c = color::BLUE;
	v.i8 = -1;
	return v;
}

constexpr scalars scalars_enum_neg()
{
	scalars v{};
	v.c = color::NEG;
	return v;
}

constexpr std::array<uint8_t, 6> mac{ 1, 2, 3, 4, 5, 6 };
constexpr std::array<uint8_t, 3> short_data{ 1, 2, 3 };

constexpr std::array<char, 200> long_name = [] {
	std::array<char, 200> name{};
	name.fill( 'a' );
	return name;
}();

constexpr text text_long()
{
	return { std::string_view( long_name.data(), long_name.size() ), {}, mac };
}

constexpr std::array<int32_t, 8> full_ints{ 1, -2, 3, -4, 5, 300, -70000, 8 };
constexpr std::array<float, 8> full_floats{ 1.5f, -2.5f, 3, 4, 5, 6, 7, 8.25f };
constexpr std::array<std::string_view, 8> full_names{ "a", "", "ccc", "d", "e", "f", "g", "hh" };
constexpr std::array<inner, 8> full_items{ inner{ 1, "a" }, { 2, {} }, { 0, "c" }, { -4, "d" },
                                           { 5, {} },       { 6, {} }, { 7, {} },  { 8, "h" } };
constexpr std::array<int64_t, 8> full_zz{ 0, -1, 1, -2, 2, 1000000000000, -1000000000000, 9 };

template <typename R, typename A>
constexpr R repeated_of( const A& source )
{
	R result;
	for ( const auto& item : source )
		static_cast<void>( result.push_back( item ) );
	return result;
}

constexpr lists lists_full()
{
	return { repeated_of<decltype( lists::ints )>( full_ints ),
	         repeated_of<decltype( lists::floats )>( full_floats ),
	         repeated_of<decltype( lists::names )>( full_names ),
	         repeated_of<decltype( lists::items )>( full_items ),
	         repeated_of<decltype( lists::zz )>( full_zz ) };
}

} // namespace cases
