#include <micropb/meta_schema.hpp>

#include <cstdint>
#include <variant>

struct msg {
    std::variant<std::monostate, pb::span_buffer<std::int32_t>> v;
};

static_assert(pb::build_tag_table<msg>().size() == 1);
