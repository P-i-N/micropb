#include <cstdint>

#include <micropb/meta_schema.hpp>

struct msg {
    std::uint32_t a : 3;
};

static_assert(pb::build_tag_table<msg>().size() == 1);
