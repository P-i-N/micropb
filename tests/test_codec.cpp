#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

#include <micropb/codec.hpp>

namespace {

namespace d = pb::detail;
using K = pb::field_kind;

struct plain {
    std::uint8_t a;
    std::uint32_t b;
    double c;
};

struct nothing {};

struct holder {
    std::uint32_t id;
    [[= pb::tag{5}]] plain child;
    std::optional<float> maybe;
    pb::repeated<float, 4> floats;
    pb::repeated<std::uint8_t, 16> blob;
    std::array<std::uint8_t, 6> mac;
    std::span<const std::int32_t> view;
    std::string_view name;
    nothing none;
    std::variant<std::monostate, plain, float> choice;
};

enum class small : std::uint8_t { ZERO };
enum class level : std::int32_t { ZERO };

constexpr const d::message_desc& p = d::message_descriptor<plain>::value;
constexpr const d::message_desc& h = d::message_descriptor<holder>::value;

////////////////////////////////////////////////////////////////////////////////

// `plain`: three scalars at the offsets 0, 4, 8, with the numbers 1, 2, 3.
static_assert(p.count == 3 && p.fields[0].offset == 0 && p.fields[1].offset == 4 && p.fields[2].offset == 8);
static_assert(p.fields[2].number == 3 && p.fields[2].type->kind == K::SCALAR && p.fields[0].alternative == 0);
static_assert(p.fields[1].type == &d::value_descriptor<std::uint32_t>::value);

// `holder`: 11 entries, with the numbers 1 5 6 7 8 9 10 11 12 13 14.
static_assert(h.count == 11 && h.fields[1].number == 5 && h.fields[8].number == 12 && h.fields[10].number == 14);
static_assert(h.fields[1].type->kind == K::MESSAGE && h.fields[1].type->message == &p);
static_assert(h.fields[2].type->kind == K::OPTIONAL &&
              h.fields[2].type->optional->value == &d::value_descriptor<float>::value);
static_assert(h.fields[3].type->kind == K::REPEATED &&
              h.fields[3].type->sequence->storage == d::sequence_storage::REPEATED &&
              h.fields[3].type->sequence->capacity == 4 && h.fields[3].type->sequence->element_size == 4 &&
              h.fields[3].type->sequence->element == &d::value_descriptor<float>::value);
static_assert(h.fields[4].type->kind == K::BYTES && h.fields[4].type->sequence->capacity == 16);
static_assert(h.fields[5].type->kind == K::FIXED_BYTES && h.fields[5].type->size == 6);
static_assert(h.fields[6].type->kind == K::REPEATED &&
              h.fields[6].type->sequence->storage == d::sequence_storage::ACCESSOR &&
              h.fields[6].type->sequence->element == &d::value_descriptor<std::int32_t>::value);
static_assert(h.fields[7].type->kind == K::STRING);
static_assert(h.fields[8].type->kind == K::MESSAGE && h.fields[8].type->message->count == 0);
static_assert(h.fields[9].alternative == 1 && h.fields[10].alternative == 2 &&
              h.fields[9].offset == h.fields[10].offset && h.fields[9].type == h.fields[10].type &&
              h.fields[9].type->kind == K::ONEOF &&
              h.fields[9].type->variant->alternatives[1] == &d::value_descriptor<plain>::value &&
              h.fields[9].type->variant->alternatives[2] == &d::value_descriptor<float>::value);

static_assert(d::value_descriptor<std::span<const std::uint8_t>>::value.kind == K::BYTES &&
              d::value_descriptor<std::span<const std::uint8_t>>::value.sequence == nullptr);
static_assert(d::value_descriptor<std::array<float, 3>>::value.sequence->storage == d::sequence_storage::ARRAY &&
              d::value_descriptor<std::array<float, 3>>::value.sequence->capacity == 3);

static_assert(d::scalar_code_of<bool>() == (0 | d::scalar_bool));
static_assert(d::scalar_code_of<std::int8_t>() == (0 | d::scalar_signed));
static_assert(d::scalar_code_of<std::uint16_t>() == 1 && d::scalar_code_of<std::uint64_t>() == 3);
static_assert(d::scalar_code_of<pb::sint32>() == (2 | d::scalar_signed | d::scalar_zigzag));
static_assert(d::scalar_code_of<float>() == (2 | d::scalar_fixed) &&
              d::scalar_code_of<pb::sfixed64>() == (3 | d::scalar_fixed));
static_assert(d::scalar_code_of<small>() == (0 | d::scalar_enum));
static_assert(d::scalar_code_of<level>() == (2 | d::scalar_enum | d::scalar_signed));

static_assert(sizeof(d::field_desc) <= (sizeof(void*) == 4 ? 12 : 16));
static_assert(sizeof(d::value_desc) <= 2 * sizeof(void*));

////////////////////////////////////////////////////////////////////////////////

bool test_optional_accessor() {
    const d::optional_desc& desc = *d::value_descriptor<std::optional<plain>>::value.optional;
    std::optional<plain> value;
    if (desc.access(&value, d::optional_op::GET) != nullptr)
        return false;

    auto* first = static_cast<plain*>(desc.access(&value, d::optional_op::ENGAGE));
    if (first == nullptr || !value || value->b != 0)
        return false;

    first->b = 7;
    if (desc.access(&value, d::optional_op::ENGAGE) != first || value->b != 7)
        return false;
    if (desc.access(&value, d::optional_op::GET) != first)
        return false;

    return desc.access(&value, d::optional_op::RESET) == nullptr && !value;
}

bool test_variant_accessor() {
    using choice = std::variant<std::monostate, plain, float>;
    const d::variant_desc& desc = *d::value_descriptor<choice>::value.variant;
    choice value;
    if (desc.access(&value, 1, false) != nullptr)
        return false;

    auto* first = static_cast<plain*>(desc.access(&value, 1, true));
    if (first == nullptr || value.index() != 1 || first->b != 0)
        return false;

    first->b = 7;
    if (desc.access(&value, 1, true) != first || std::get<plain>(value).b != 7)
        return false;
    if (desc.access(&value, 2, false) != nullptr || value.index() != 1)
        return false;

    return desc.access(&value, 0, true) != nullptr && value.index() == 0;
}

bool test_sequence_accessor() {
    constexpr std::array<std::int32_t, 3> items{1, 2, 3};
    std::span<const std::int32_t> view(items);
    const d::sequence_desc& view_desc = *d::value_descriptor<std::span<const std::int32_t>>::value.sequence;
    const d::sequence_view a = view_desc.access(&view);
    if (a.count != 3 || a.capacity != 3 || a.data != items.data() || a.stored_count != nullptr)
        return false;

    std::array<std::int32_t, 4> storage{};
    pb::span_buffer<std::int32_t> buffer(storage);
    static_cast<void>(buffer.push_back(1));
    static_cast<void>(buffer.push_back(2));
    const d::sequence_desc& buffer_desc = *d::value_descriptor<pb::span_buffer<std::int32_t>>::value.sequence;
    const d::sequence_view b = buffer_desc.access(&buffer);
    if (b.count != 2 || b.capacity != 4 || b.data != storage.data() || b.stored_count == nullptr ||
        *b.stored_count != 2)
        return false;

    *b.stored_count = 3;
    return buffer.size() == 3 && view_desc.element_size == 4 && buffer_desc.storage == d::sequence_storage::ACCESSOR;
}

} // namespace

int main() {
    const bool ok = test_optional_accessor() && test_variant_accessor() && test_sequence_accessor();
    return ok ? 0 : 1;
}
