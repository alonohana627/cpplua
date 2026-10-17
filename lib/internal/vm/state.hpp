#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gc/heap.hpp"
#include "gc/object.hpp"
#include "value/value.hpp"
#include "vm/opcodes.hpp"

namespace lua2 {

struct Upvalue;

// Stack sizes, as in Lua (llimits.h, lstate.h, luaconf.h).
inline constexpr std::uint32_t extra_stack = 5; // EXTRA_STACK: slots past the end for the VM
inline constexpr std::uint32_t min_stack = 20;  // LUA_MINSTACK: free slots a C function gets
inline constexpr std::uint32_t basic_stack_size = 2 * min_stack;
inline constexpr std::uint32_t max_stack = 1000000; // LUAI_MAXSTACK (E6.7)
inline constexpr std::uint32_t error_stack_size = max_stack + 200;

// "Return all results", for CallInfo::wanted (Lua's LUA_MULTRET).
inline constexpr int all_results = -1;

// One active call (Lua's CallInfo). Frames form a doubly linked list that
// grows as needed and is reused, so a call allocates nothing in the common
// case. Positions in the stack are indices, so the stack can move.
struct CallInfo {
    std::uint32_t func = 0; // the function's slot; its registers start at func + 1
    std::uint32_t top = 0;  // one past the last register the function may use
    CallInfo* previous = nullptr;
    CallInfo* next = nullptr;
    const Instruction* saved_pc = nullptr; // where a Lua function continues
    const Value* constants = nullptr;      // its constants, for the VM to resume it fast
    int extra_args = 0;                    // a vararg function's extra arguments
    int wanted = 0;                        // results the caller wants, or all_results

    // Entered from C++ (Lua's CIST_FRESH): returning from it leaves execute().
    bool fresh = false;
    // Entered by a tail call (CIST_TAIL): tracebacks cannot name it.
    bool tail_call = false;
    // Running a finalizer called from here (CIST_FIN): the finalizer's frame
    // is named "metamethod '__gc'".
    bool finalizer = false;
};

// A thread of execution (Lua's lua_State): the stack, the frames, the open
// upvalues and to-be-closed variables, and the error being raised. Only
// the main thread exists, which the Heap owns; coroutines (stage 10) will
// make more.
//
// The stack stores tags and payloads in two arrays (docs/plan/vm.md). Both
// are allocated through the Heap, so they count toward its pacing.
class State : public GcObject {
public:
    explicit State(Heap& heap);
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&&) = delete;
    State& operator=(State&&) = delete;
    ~State();

    [[nodiscard]] Heap& heap() const { return *heap_; }

    [[nodiscard]] Value get(std::uint32_t index) const {
        return Value::from_parts(stack_tags[index], stack_values[index]);
    }

    void set(std::uint32_t index, Value v) { // NOLINT(readability-make-member-function-const)
        stack_tags[index] = v.tag();
        stack_values[index] = v.payload();
    }

    void push(Value v) { set(top++, v); }

    // Makes room for `n` more slots above top (luaD_checkstack): false when
    // the stack would pass max_stack, with the error ("stack overflow",
    // still without its position, or "error in error handling" when the
    // stack is already past the limit) in error_value.
    [[nodiscard]] bool check_stack(std::uint32_t n) {
        if (stack_last - top > n) [[likely]] {
            return true;
        }
        return grow_stack(n);
    }

    bool grow_stack(std::uint32_t n);

    // Gives the stack exactly `size` usable slots (luaD_reallocstack),
    // moving the open upvalues with it.
    void resize_stack(std::uint32_t size);

    // Gives back stack memory the frames no longer use (luaD_shrinkstack).
    void shrink_stack();

    // The next frame, from the list or new (luaE_extendCI).
    CallInfo* next_call_info() {
        if (ci->next != nullptr) [[likely]] {
            return ci = ci->next;
        }
        return extend_call_info();
    }

    // Makes sure a frame follows the running one in the list.
    void reserve_call_info() {
        if (ci->next == nullptr) {
            extend_call_info();
            ci = ci->previous;
        }
    }

    // Frees half of the unused frames (luaE_shrinkCI).
    void shrink_call_infos();

    // ---- Upvalues (lfunc.c) ----

    // The open upvalue for stack slot `level`, created if needed
    // (luaF_findupval).
    Upvalue* find_upvalue(std::uint32_t level);

    // Closes every open upvalue at `level` or above (luaF_closeupval).
    void close_upvalues(std::uint32_t level);

    // ---- Frames, for errors and tracebacks ----

    // Whether a frame runs a Lua function (Lua's isLua).
    [[nodiscard]] bool is_lua(const CallInfo* frame) const {
        return frame != &base_ci && stack_tags[frame->func] == Tag::LuaClosure;
    }

    // The frame at `level`: 0 is the running function, 1 the one that called
    // it, and so on; nullptr past the last (lua_getstack).
    [[nodiscard]] const CallInfo* frame_at(int level) const;

    // The error being raised (Lua keeps it on the stack top): a string for
    // runtime errors, any value for error().
    void set_error(Value v) { error_value = v; }

    void set_error(std::string_view message);

    // ---- The stack, public for the VM ----
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes): the VM's fast path

    Tag* stack_tags = nullptr;
    std::uint64_t* stack_values = nullptr;
    std::uint32_t stack_last = 0; // usable slots; extra_stack more exist past it
    std::uint32_t top = 0;        // first free slot

    CallInfo* ci = nullptr; // the running call
    CallInfo base_ci;       // the bottom frame, for C++ entering the VM

    Upvalue* open_upvalues = nullptr; // highest slot first
    State* next_with_upvalues = this; // the Heap's list of threads with open upvalues
    GcObject* gc_list = nullptr;      // the collector's gray lists

    // How many C++ calls into the VM are active (Lua's nCcalls).
    int c_calls = 0;

    // The error that stopped execute(), until a protected call takes it
    // (R1.1). A root of the collector.
    Value error_value;

    // The stack slots of the to-be-closed variables (M6), lowest first
    // (Lua's tbclist).
    std::vector<std::uint32_t> to_be_closed;

    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

private:
    CallInfo* extend_call_info();
    void free_stack();

    Heap* heap_;
    std::size_t call_info_count_ = 0;
};

} // namespace lua2
