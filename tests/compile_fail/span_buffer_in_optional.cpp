#include <cstdint>
#include <optional>

#include <micropb/meta_schema.hpp>

struct inner {
    pb::span_buffer<std::int32_t> values;
};

struct msg {
    std::optional<inner> i;
};

static_assert(pb::build_tag_table<msg>().size() == 1);
