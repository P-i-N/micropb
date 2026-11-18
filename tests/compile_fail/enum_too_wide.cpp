#include <micropb/meta_schema.hpp>

#include <cstdint>

enum class wide : std::int64_t { ZERO };

struct msg {
    wide e;
};

static_assert(pb::build_tag_table<msg>().size() == 1);
