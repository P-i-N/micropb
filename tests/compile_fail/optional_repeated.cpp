#include <micropb/meta_schema.hpp>

#include <cstdint>
#include <optional>

struct msg {
    std::optional<pb::repeated<std::int32_t, 4>> values;
};

static_assert(pb::build_tag_table<msg>().size() == 1);
