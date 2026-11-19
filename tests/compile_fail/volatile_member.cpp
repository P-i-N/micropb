#include <cstddef>

#include <micropb/micropb.hpp>

// A volatile-qualified integer is not accepted: the codec reads and writes through `memcpy`, without `volatile`.
struct msg {
    volatile int a;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype(msg::a), 1, offsetof(msg, a)>> {};

std::size_t size_of_message(const msg& m) {
    return pb::encoded_size(m);
}
