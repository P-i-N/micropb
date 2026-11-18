#include <micropb/meta_schema.hpp>

#include <cstdint>
#include <optional>

struct msg {
    std::optional<std::optional<std::int32_t>> v;
};

static_assert(pb::build_tag_table<msg>().size() == 1);
