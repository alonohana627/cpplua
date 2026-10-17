#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <lua2/api.hpp>

namespace lua2 {

class Heap;

// A Lua interpreter: its own memory, garbage collector and main thread, and
// the globals with the standard library so far (the base library and
// debug.traceback). C++ functions, values and classes go in through it
// (docs/how_to_put_cpp_functions_and_classes_into_lua.md). Destroying it
// closes it like lua_close: pending to-be-closed variables and finalizers
// run first; every Ref must be gone by then.
class Interpreter {
public:
    Interpreter();
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;
    Interpreter(Interpreter&&) noexcept;
    Interpreter& operator=(Interpreter&&) noexcept;
    ~Interpreter();

    // ---- Running code ----

    // Compiles and runs a chunk (E1). Returns what it returns, each value
    // as `print` would show it, or the error message: a syntax error, or a
    // runtime error with its position ("file.lua:3: attempt to ..."), with a
    // stack traceback after it when `traceback` is true (R2.5).
    // `chunk_name` is how messages name the chunk, such as a file's path.
    std::expected<std::vector<std::string>, std::string>
    run(std::string_view source, std::string_view chunk_name, bool traceback = false);

    // Calls the global function `name` with `args` (converted to Lua), and
    // converts its first result to R (void for none); or the error message.
    template <class R = void, class... A>
    std::expected<R, std::string> call(std::string_view name, A&&... args) {
        return get(name).template call<R>(std::forward<A>(args)...);
    }

    // ---- Globals ----

    // The global `name` (nil if there is none).
    [[nodiscard]] Ref get(std::string_view name);

    // global `name` = value: a number, a string, a boolean, a Ref, nullptr
    // for nil, or an object of a bound class (copied).
    template <class V>
    void set(std::string_view name, V&& value) {
        ScopedStack stack{main_thread()};
        detail::push_result(stack, std::forward<V>(value));
        set_global_from_top(name);
    }

    // global `name` = a C++ function: any callable, converted as bind()
    // describes (or an `int(Context&)` used as it is).
    template <class F>
    void set_function(std::string_view name, F f) {
        set_global_function(name, bind(std::move(f)));
    }

    // ---- Values ----

    // A C++ callable as a Lua function value.
    template <class F>
    [[nodiscard]] Ref function(F f) {
        return make_function(bind(std::move(f)));
    }

    [[nodiscard]] Ref new_table();

    // The globals table itself (_G).
    [[nodiscard]] Ref globals();

    // ---- Classes ----

    // Makes C++ class T a Lua type called `name`: a global table for its
    // functions and methods, and a metatable for its objects. Add to it with
    // the returned builder: lua.new_class<Car>("Car").constructor<...>()
    // .method("accelerate", &Car::accelerate)...
    template <class T>
    Class<T> new_class(std::string_view name) {
        return Class<T>{*heap_, name};
    }

    // ---- The rest ----

    // Where `print` writes (stdout by default).
    void set_output(std::function<void(std::string_view)> output);

    // The garbage collector (E9.6): a full cycle; the mode, generational or
    // incremental (the default); the memory in use, in bytes.
    void collect();
    void set_generational(bool generational);
    [[nodiscard]] std::size_t memory_in_use() const;

    // The main thread, for a ScopedStack (the stack-level API).
    [[nodiscard]] State& main_thread();

private:
    void set_global_from_top(std::string_view name);
    void set_global_function(std::string_view name, Function f);
    Ref make_function(Function f);

    std::unique_ptr<Heap> heap_;
};

} // namespace lua2
