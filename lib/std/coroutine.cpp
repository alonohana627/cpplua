// The coroutine library: a port of Lua 5.4.9's lcorolib.c
// (docs/final_docs/coroutines.md). The work is in the VM (resume, yield and
// close_thread in vm/execute.cpp); these functions move values between the
// threads.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "gc/heap.hpp"
#include "std/auxlib.hpp"
#include "std/libs.hpp"
#include "value/table.hpp"
#include "vm/execute.hpp"
#include "vm/function.hpp"
#include "vm/state.hpp"

namespace cpplua {

namespace {

// NOLINTBEGIN(misc-const-correctness,readability-non-const-parameter): the CFunction signature

// getco
State* check_thread(State& state, int n) {
    const Value v = arg(state, n);
    if (!v.is_thread()) {
        arg_type_error(state, n, "thread");
        return nullptr;
    }
    return v.as_thread();
}

// lua_xmove: the top n values of `from` go to the top of `to`.
void move_values(State& from, State& to, int n) {
    if (&from == &to) {
        return;
    }
    const std::uint32_t first = from.top - static_cast<std::uint32_t>(n);
    for (std::uint32_t slot = first; slot < from.top; ++slot) {
        to.push(from.get(slot));
    }
    from.top = first;
}

// auxresume: resumes `co` with the top `nargs` values of the running thread
// as its arguments. The results (or the error object) end on the running
// thread's top; returns how many, or -1 after an error (one value).
int resume_with(State& state, State& co, int nargs) {
    if (!co.check_stack(static_cast<std::uint32_t>(nargs))) {
        state.top -= static_cast<std::uint32_t>(nargs);
        push_string(state, "too many arguments to resume");
        return -1;
    }
    move_values(state, co, nargs);
    int results = 0;
    const Status status = resume(co, state, nargs, results);
    if (status != Status::Error) {
        if (!state.check_stack(static_cast<std::uint32_t>(results) + 1)) {
            co.top -= static_cast<std::uint32_t>(results); // remove the results anyway
            push_string(state, "too many results to resume");
            return -1;
        }
        move_values(co, state, results);
        return results;
    }
    move_values(co, state, 1); // the error object (already there when co is state)
    return -1;
}

int coro_resume(State& state) {
    State* co = check_thread(state, 1);
    if (co == nullptr) {
        return c_function_error;
    }
    const int r = resume_with(state, *co, arg_count(state) - 1);
    // [co, results...] or [co, error]: true or false goes before them.
    const int n = r < 0 ? 1 : r;
    const std::uint32_t first = state.top - static_cast<std::uint32_t>(n);
    for (std::uint32_t slot = state.top; slot > first; --slot) {
        state.set(slot, state.get(slot - 1));
    }
    state.set(first, Value::boolean(r >= 0));
    ++state.top;
    return n + 1;
}

int coro_wrap_step(State& state) {
    // auxwrap
    State* co = state.get(state.ci->func).as_c_closure()->upvalues()[0].as_thread();
    const int r = resume_with(state, *co, arg_count(state));
    if (r >= 0) {
        return r;
    }
    Value error = state.get(state.top - 1);
    if (co->status == ThreadStatus::Dead) { // an error in the coroutine: close it
        close_thread(*co, state);
        error = co->error_value;
    }
    if (error.is_string()) { // with the position of the caller
        error = Value::string(
            state.heap().new_string(where(state, 1) + std::string{error.as_string()->view()}));
    }
    state.set_error(error);
    return c_function_error;
}

// cocreate
State* new_coroutine(State& state) {
    if (!check_function(state, 1)) {
        return nullptr;
    }
    State* co = state.heap().new_thread();
    push(state, Value::thread(co)); // anchored
    co->push(arg(state, 1));
    return co;
}

int coro_create(State& state) {
    return new_coroutine(state) == nullptr ? c_function_error : 1;
}

int coro_wrap(State& state) {
    State* co = new_coroutine(state);
    if (co == nullptr) {
        return c_function_error;
    }
    CClosure* closure = state.heap().new_c_closure(&coro_wrap_step, 1);
    closure->upvalues()[0] = Value::thread(co);
    push(state, Value::c_closure(closure));
    return 1;
}

int coro_yield(State& state) {
    return yield_values(state, arg_count(state));
}

enum class CoStatus : std::uint8_t { Running, Dead, Suspended, Normal };

constexpr std::array<std::string_view, 4> status_names{"running", "dead", "suspended", "normal"};

// auxstatus
CoStatus status_of(const State& state, const State& co) {
    if (&state == &co) {
        return CoStatus::Running;
    }
    switch (co.status) {
    case ThreadStatus::Yield: return CoStatus::Suspended;
    case ThreadStatus::Ok:
        if (co.ci != &co.base_ci) {
            return CoStatus::Normal; // it has frames: it resumed another
        }
        if (co.top == co.base_ci.func + 1) {
            return CoStatus::Dead; // nothing on its stack
        }
        return CoStatus::Suspended; // not started yet
    case ThreadStatus::Dead: break;
    }
    return CoStatus::Dead;
}

int coro_status(State& state) {
    const State* co = check_thread(state, 1);
    if (co == nullptr) {
        return c_function_error;
    }
    push_string(state, status_names.at(static_cast<std::size_t>(status_of(state, *co))));
    return 1;
}

int coro_isyieldable(State& state) {
    const State* co = &state;
    if (!arg_is_none(state, 1)) {
        co = check_thread(state, 1);
        if (co == nullptr) {
            return c_function_error;
        }
    }
    push(state, Value::boolean(is_yieldable(*co)));
    return 1;
}

int coro_running(State& state) {
    push(state, Value::thread(&state));
    push(state, Value::boolean(&state == &state.heap().main_thread()));
    return 2;
}

int coro_close(State& state) {
    State* co = check_thread(state, 1);
    if (co == nullptr) {
        return c_function_error;
    }
    const CoStatus status = status_of(state, *co);
    if (status != CoStatus::Dead && status != CoStatus::Suspended) {
        return raise_error(state,
                           "cannot close a " +
                               std::string{status_names.at(static_cast<std::size_t>(status))} +
                               " coroutine");
    }
    if (close_thread(*co, state) == Status::Ok) {
        push(state, Value::boolean(true));
        return 1;
    }
    push(state, Value::boolean(false));
    push(state, co->error_value);
    return 2;
}

// NOLINTEND(misc-const-correctness,readability-non-const-parameter)

constexpr std::array coroutine_functions{
    LibraryFunction{.name = "create", .function = &coro_create},
    LibraryFunction{.name = "resume", .function = &coro_resume},
    LibraryFunction{.name = "running", .function = &coro_running},
    LibraryFunction{.name = "status", .function = &coro_status},
    LibraryFunction{.name = "wrap", .function = &coro_wrap},
    LibraryFunction{.name = "yield", .function = &coro_yield},
    LibraryFunction{.name = "isyieldable", .function = &coro_isyieldable},
    LibraryFunction{.name = "close", .function = &coro_close},
};

} // namespace

void open_coroutine(Heap& heap) {
    open_library(heap, "coroutine", coroutine_functions);
}

} // namespace cpplua
