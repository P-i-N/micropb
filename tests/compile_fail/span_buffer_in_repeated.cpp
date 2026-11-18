#include <micropb/meta_schema.hpp>

#include <cstdint>

struct msg {
    pb::repeated<pb::span_buffer<std::uint8_t>, 2> values;
};

static_assert(pb::build_tag_table<msg>().size() == 1);
