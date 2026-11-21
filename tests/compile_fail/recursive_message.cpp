#include <cstddef>
#include <cstdint>
#include <span>

#include <micropb/codec.hpp>

struct node
{
	std::int32_t id;
	std::span<const node> children;
};

struct a;

struct b
{
	std::span<const a> as;
};

struct a
{
	b inner;
};

template <>
struct pb::fields_of<node> : pb::field_list<node,
                                            pb::field_at<decltype( node::id ), 1, offsetof( node, id )>,
                                            pb::field_at<decltype( node::children ), 2, offsetof( node, children )>>
{};

template <>
struct pb::fields_of<b> : pb::field_list<b, pb::field_at<decltype( b::as ), 1, offsetof( b, as )>>
{};

template <>
struct pb::fields_of<a> : pb::field_list<a, pb::field_at<decltype( a::inner ), 1, offsetof( a, inner )>>
{};

const node direct{};
const a indirect{};
const std::size_t direct_size = pb::encoded_size( direct );
const std::size_t indirect_size = pb::encoded_size( indirect );
