#include <cstdint>

#include <micropb/meta_schema.hpp>

struct msg {
    std::int32_t a;
    [[=pb::tag{1}]] std::int32_t b;
};

static_assert(pb::build_tag_table<msg>().size() == 2);
