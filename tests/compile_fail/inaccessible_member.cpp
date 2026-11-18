#include <micropb/meta_schema.hpp>

#include <cstdint>

struct msg {
    std::int32_t a;

private:
    std::int32_t b;
};

static_assert(pb::build_tag_table<msg>().size() == 1);
