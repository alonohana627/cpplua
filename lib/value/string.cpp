#include "value/string.hpp"

#include <array>
#include <string>
#include <vector>

#include "gc/heap.hpp"
#include "value/convert.hpp"

namespace lua2 {

namespace {

// The pieces of a concatenation: each string's bytes, or the text of each
// number, written into `numbers`. Both spans have room for every value.
std::span<const std::string_view> join_parts(std::span<const Value> values,
                                             std::span<NumberText> numbers,
                                             std::span<std::string_view> parts) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i].is_string()) {
            parts[i] = values[i].as_string()->view();
        } else {
            numbers[i] = number_text(values[i]).value_or(NumberText{});
            parts[i] = numbers[i].view();
        }
    }
    return parts.first(values.size());
}

} // namespace

std::expected<Value, RuntimeError> concatenate(Heap& heap, std::span<const Value> values) {
    if (values.size() == 1) {
        return values.front();
    }
    const auto joinable = [](Value v) { return v.is_string() || v.is_number(); };

    // Lua's luaV_concat joins from the right and stops at the first pair it
    // cannot join, so the error is about the rightmost operand that is
    // neither a string nor a number, except that the last pair names its
    // left operand when both are wrong (luaG_concaterror).
    for (std::size_t k = values.size(); k-- > 0;) {
        if (joinable(values[k])) {
            continue;
        }
        const Value culprit = (k == values.size() - 1 && k > 0 && !joinable(values[k - 1]))
                                  ? values[k - 1]
                                  : values[k];
        std::string message = "attempt to concatenate a ";
        message += type_name(culprit.type());
        message += " value";
        return std::unexpected(RuntimeError{std::move(message)});
    }

    // Numbers become text (V6). A typical chain is short, so its pieces fit
    // in arrays on the stack; only a long chain allocates for them.
    constexpr std::size_t on_stack = 8;
    if (values.size() <= on_stack) {
        std::array<NumberText, on_stack> numbers;
        std::array<std::string_view, on_stack> parts;
        return Value::string(heap.new_string(join_parts(values, numbers, parts)));
    }
    std::vector<NumberText> numbers(values.size());
    std::vector<std::string_view> parts(values.size());
    return Value::string(heap.new_string(join_parts(values, numbers, parts)));
}

std::expected<Value, RuntimeError> length(Value value) {
    if (value.is_string()) {
        return Value::integer(static_cast<std::int64_t>(value.as_string()->length()));
    }
    std::string message = "attempt to get length of a ";
    message += type_name(value.type());
    message += " value";
    return std::unexpected(RuntimeError{std::move(message)});
}

} // namespace lua2
