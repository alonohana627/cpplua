#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lua2 {

class Heap;

// A Lua interpreter: its own memory, garbage collector and main thread,
// and the globals with the standard library's `print` (the rest of the
// library, and a way to exchange values with C++, come with later stages).
class Interpreter {
public:
    Interpreter();
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;
    Interpreter(Interpreter&&) noexcept;
    Interpreter& operator=(Interpreter&&) noexcept;
    ~Interpreter();

    // Compiles and runs a chunk (E1). Returns what it returns, each value
    // as `print` would show it, or the error message: a syntax error, or a
    // runtime error with its position ("file.lua:3: attempt to ...").
    // `chunk_name` is how messages name the chunk, such as a file's path.
    std::expected<std::vector<std::string>, std::string> run(std::string_view source,
                                                             std::string_view chunk_name);

    // The garbage collector (E9.6): a full cycle; the mode, generational or
    // incremental (the default); the memory in use, in bytes.
    void collect();
    void set_generational(bool generational);
    [[nodiscard]] std::size_t memory_in_use() const;

private:
    std::unique_ptr<Heap> heap_;
};

} // namespace lua2
