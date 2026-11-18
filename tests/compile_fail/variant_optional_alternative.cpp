#include <micropb/meta_schema.hpp>

#include <cstdint>
#include <optional>
#include <variant>

struct msg {
    std::variant<std::monostate, std::optional<std::int32_t>> v;
};

static_assert(pb::build_tag_table<msg>().size() == 1);
