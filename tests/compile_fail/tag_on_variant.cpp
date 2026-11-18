#include <cstdint>
#include <variant>

#include <micropb/meta_schema.hpp>

struct msg {
    [[=pb::tag{3}]] std::variant<std::monostate, std::int32_t> v;
};

static_assert(pb::build_tag_table<msg>().size() == 1);
