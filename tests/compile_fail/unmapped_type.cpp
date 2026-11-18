#include <micropb/meta_schema.hpp>

struct msg {
    char* p;
};

static_assert(pb::build_tag_table<msg>().size() == 1);
