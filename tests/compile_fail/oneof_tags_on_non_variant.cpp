#include <micropb/meta_schema.hpp>

#include <cstdint>

struct msg {
    [[=pb::oneof_tags<3>{}]] std::int32_t a;
};

static_assert(pb::build_tag_table<msg>().size() == 1);
