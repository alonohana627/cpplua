#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

#include "gc/object.hpp"
#include "gc/pool.hpp"
#include "value/string.hpp"
#include "value/value.hpp"
#include "vm/metamethods.hpp"

namespace lua2 {

struct Proto;     // vm/proto.hpp
class LuaClosure; // vm/function.hpp
struct Upvalue;   // vm/function.hpp
class State;      // vm/state.hpp
class Table;      // value/table.hpp
class Userdata;   // value/userdata.hpp
class CClosure;   // vm/function.hpp

// The collector's two modes (E9.5, manual §2.5).
enum class GcMode : std::uint8_t {
    Incremental,  // tri-color marking in small steps (the default)
    Generational, // frequent minor collections of young objects, occasional major ones
};

// How the collector is tuned (E9.5), with Lua 5.4's defaults (lgc.h).
struct GcParameters {
    int pause = 200;            // LUAI_GCPAUSE: wait for memory to grow this % before a cycle
    int step_multiplier = 100;  // LUAI_GCMUL: work per step, relative to allocation
    int step_size = 13;         // LUAI_GCSTEPSIZE: log2 of the bytes between steps (8 KB)
    int minor_multiplier = 20;  // LUAI_GENMINORMUL: memory growth (%) between minor collections
    int major_multiplier = 100; // LUAI_GENMAJORMUL: memory growth (%) before a major one
};

// A testing aid (docs/plan/vm.md): collect at every place the program may
// collect, the only reliable way to find a missing root or barrier.
enum class GcStress : std::uint8_t {
    Off,
    FullCycles, // a whole cycle at every check, like Lua's HARDMEMTESTS
    Steps,      // one basic step at every check: exercises the incremental phases and
                // minor collections, with the cycle spread over many checks
};

// Everything the interpreter shares (Lua's global_State): the memory of all
// collectable objects, the table of interned strings, the main thread, and
// the garbage collector, a port of Lua 5.4's lgc.c (docs/final_docs/vm.md).
//
// Objects come from a size-class pool (gc/pool.hpp) and are linked into one
// list; the collector marks what is reachable from the main thread and frees
// the rest. It is non-moving (E9.3). Collection happens only at the checks
// the VM makes (check_gc), never in the middle of an allocation, so C++
// code can hold objects between checks without anchoring them.
class Heap {
public:
    // A seed chosen at random, different each run, as in Lua: the hashes,
    // and so the order of `pairs`, change from run to run.
    static std::uint32_t random_seed();

    // `seed` decides every string hash; fix it to get the same hashes every
    // run (tests, debugging).
    explicit Heap(std::uint32_t seed = random_seed());

    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;
    Heap(Heap&&) = delete;
    Heap& operator=(Heap&&) = delete;
    ~Heap();

    // ---- Strings ----

    // A string holding `bytes`: the existing one for a short string made
    // before, a new one otherwise.
    const String* new_string(std::string_view bytes);

    // A string holding `parts` one after the other, as Lua's luaV_concat
    // builds them: a short result is put together in a buffer on the stack
    // and interned; a long one is written straight into the new string. No
    // other allocation either way.
    const String* new_string(std::span<const std::string_view> parts);

    [[nodiscard]] std::uint32_t seed() const { return seed_; }

    // How many strings exist, and how many of them are interned.
    [[nodiscard]] std::size_t string_count() const { return string_count_; }

    [[nodiscard]] std::size_t short_string_count() const { return short_count_; }

    // ---- Other objects ----

    // An empty prototype, for the compiler.
    Proto* new_proto();

    // A closure of `proto` with room for `upvalue_count` upvalues, all null.
    LuaClosure* new_closure(Proto* proto, std::size_t upvalue_count);

    // A closed upvalue holding nil.
    Upvalue* new_upvalue();

    // An empty table.
    Table* new_table();

    // A userdata of `size` bytes (uninitialized) with `user_values` nil
    // slots, no metatable and no destructor (lua_newuserdatauv).
    Userdata* new_userdata(std::size_t size, std::size_t user_values);

    // A C++ closure of `function` with `upvalue_count` nil upvalues
    // (lua_pushcclosure).
    CClosure* new_c_closure(CFunction function, std::size_t upvalue_count);

    // Keeps the newest object alive forever, outside the collector's view
    // (Lua's luaC_fix), for strings the interpreter itself needs.
    void fix(const GcObject* fixed);

    // The main thread: its stack is where the collector starts marking.
    [[nodiscard]] State& main_thread() { return *main_thread_; }

    // The thread running now: the main thread, or the coroutine it resumed
    // (and so on). Finalizers run in it, as in Lua.
    [[nodiscard]] State* running_thread() const { return running_; }

    void set_running_thread(State* thread) { running_ = thread; }

    // A new coroutine with an empty stack (lua_newthread).
    State* new_thread();

    // The globals table, every chunk's _ENV (E1.2, E2.3). A root of the
    // collector, like Lua's registry that holds it.
    [[nodiscard]] Table& globals() { return *globals_; }

    // The loaded modules (Lua's registry._LOADED: "_G" is the globals),
    // where tracebacks look for the names of global functions. A root.
    [[nodiscard]] Table& loaded() { return *loaded_; }

    // ---- Metatables (stage 7) ----

    // The interned "__index", "__add", ... (Lua's tmname), never collected.
    [[nodiscard]] const String* event_string(Event e) const {
        return event_strings_[static_cast<std::size_t>(e)];
    }

    // "__name", for luaT_objtypename.
    [[nodiscard]] const String* name_string() const { return name_string_; }

    // The metatable of every value of a type that is not a table (Lua's
    // G(L)->mt): none until the string library (stage 9). Roots.
    [[nodiscard]] Table* type_metatable(Type type) const {
        return type_metatables_[static_cast<std::size_t>(type)];
    }

    void set_type_metatable(Type type, Table* mt) {
        type_metatables_[static_cast<std::size_t>(type)] = mt;
    }

    // Gives table `t` the metatable `mt` (or none), with the barrier, and
    // marks `t` for finalization if `mt` has __gc now (lua_setmetatable,
    // luaC_checkfinalizer, M7.1).
    void set_metatable(Table* t, Table* mt);

    // The same for a userdata.
    void set_metatable(Userdata* u, Table* mt);

    // The registry (LUA_REGISTRYINDEX, debug.getregistry): [1] the main
    // thread, [2] the globals, _LOADED the loaded modules, and what the
    // libraries keep there (_PRELOAD, _HOOKKEY).
    [[nodiscard]] Table& registry() { return *registry_; }

    // The registry's table of references (luaL_ref): C++ keeps Lua values
    // alive by storing them here under integer keys. A root. It also maps
    // each bound C++ class's tag (a light userdata) to its metatable.
    [[nodiscard]] Table& references() { return *references_; }

    // Stores `v` there and returns its key (luaL_ref); a key is reused once
    // released (luaL_unref).
    std::int64_t reference(Value v);
    void release_reference(std::int64_t key);
    [[nodiscard]] Value referenced(std::int64_t key);

    // Ends the interpreter's life the way lua_close does: closes what is
    // still open in the main thread, then runs the finalizer of every
    // object marked for one (M7.2). Freeing comes in the destructor, which
    // calls this first if nobody did.
    void close();

    [[nodiscard]] bool is_closed() const { return closed_; }

    // Where `print` writes (lua_writestring): stdout by default, flushed at
    // each line as Lua does; tests capture it.
    void set_output(std::function<void(std::string_view)> output) { output_ = std::move(output); }

    void write_output(std::string_view text);

    // Flushes the standard output when it is where output goes (io.flush).
    void flush_output();

    // Where warnings go (lua_writestringerror): stderr by default.
    void set_error_output(std::function<void(std::string_view)> output) {
        error_output_ = std::move(output);
    }

    void write_error(std::string_view text);

    // Whether warn() shows warnings: off until "@on", as in the lua
    // program (lauxlib's warnfoff).
    [[nodiscard]] bool warnings_on() const { return warnings_on_; }

    void set_warnings_on(bool on) { warnings_on_ = on; }

    // ---- Memory ----

    // Raw memory counted toward the collector's pacing, like Lua's luaM_
    // functions: the stack, call frames, arrays inside prototypes.
    void* allocate(std::size_t size);
    void free(void* block, std::size_t size);

    // Memory held elsewhere (std::vector inside a prototype) that should
    // count toward the pacing too. Negative to give it back.
    void count_bytes(std::ptrdiff_t delta) { debt_ += delta; }

    // Bytes in use (Lua's gettotalbytes): what collectgarbage("count")
    // reports, times 1024.
    [[nodiscard]] std::size_t total_bytes() const {
        return static_cast<std::size_t>(total_bytes_ + debt_);
    }

    // ---- Collection ----

    // Whether a step is due: the debt (bytes allocated beyond the
    // collector's allowance) is positive. The VM tests this inline at each
    // place it may collect, then calls step() with its stack in order.
    [[nodiscard]] bool wants_step() const { return debt_ > 0; }

    // Does some collection work (Lua's luaC_step): an incremental step, or
    // a minor (sometimes major) collection in generational mode.
    void step();

    // A full collection cycle (collectgarbage("collect"), Lua's
    // luaC_fullgc).
    void collect();

    // Switches mode (collectgarbage("incremental") and ("generational"),
    // Lua's luaC_changemode).
    void set_mode(GcMode mode);

    // The mode the program asked for. (Generational mode runs incremental
    // cycles for a while after a bad major collection; it is still the mode.)
    [[nodiscard]] GcMode mode() const {
        return kind_ == GcMode::Generational || last_atomic_ != 0 ? GcMode::Generational
                                                                  : GcMode::Incremental;
    }

    // collectgarbage("stop"), ("restart") and ("isrunning").
    void stop();
    void restart();

    [[nodiscard]] bool is_running() const { return !stopped_; }

    // A finalizer is running: no collection may start (Lua's GCSTPGC), and
    // collectgarbage() refuses.
    [[nodiscard]] bool in_finalizer() const { return in_finalizer_; }

    // collectgarbage("step", n): a basic step when n is 0, else as if n KB
    // were allocated; true when a cycle ended (lua_gc's LUA_GCSTEP).
    bool step_kilobytes(std::int64_t kilobytes);

    [[nodiscard]] GcParameters& parameters() { return parameters_; }

    void set_stress(GcStress stress);

    [[nodiscard]] GcStress stress() const { return stress_; }

    // How many cycles have finished (atomic phases run), for tests.
    [[nodiscard]] std::size_t cycles() const { return cycles_; }

    // ---- Barriers ----
    //
    // Incremental collection keeps one rule while it marks: a black object
    // never points to a white one. Storing a white object into a black one
    // breaks it, so every store into an object that may be black goes
    // through a barrier.

    // Forward barrier (luaC_barrier): marks the child.
    void barrier(GcObject* parent, Value child) {
        if (child.is_collectable()) {
            object_barrier(parent, child.as_object());
        }
    }

    void object_barrier(GcObject* parent, GcObject* child) {
        if (is_black(parent) && is_white(child)) [[unlikely]] {
            forward_barrier(parent, child);
        }
    }

    // Backward barrier (luaC_barrierback): makes the parent gray again, for
    // objects that change often (tables, stage 6).
    void barrier_back(GcObject* parent, Value child) {
        if (child.is_collectable() && is_black(parent) && is_white(child.as_object()))
            [[unlikely]] {
            backward_barrier(parent);
        }
    }

    // ---- For the VM ----

    // An upvalue moves out of the stack (lfunc.c's luaF_closeupval): closed
    // upvalues are never gray, and their value needs the barrier.
    void upvalue_closed(Upvalue* upvalue);

    // Moves `o` to the objects marked for finalization if `mt` has __gc
    // (luaC_checkfinalizer).
    void check_finalizer(GcObject* o, Table* mt);

private:
    friend class State;

    // The collector's states (Lua's GCS*), in Lua's order.
    enum class Phase : std::uint8_t {
        Propagate,
        EnterAtomic,
        Atomic,
        SweepAll,
        SweepFinalizable,
        SweepToBeFinalized,
        SweepEnd,
        CallFinalizers,
        Pause,
    };

    // ---- Allocation (heap.cpp) ----

    template <typename T>
    T* link_new(void* memory, ObjectType type);
    String* allocate_string(std::size_t length, std::uint32_t hash, bool is_short);
    static char* bytes_of(String* s);
    const String* intern(std::string_view bytes);
    void resize_string_table(std::size_t size);
    void remove_string(String* s);
    void set_debt(std::ptrdiff_t debt);
    void free_object(GcObject* o);

    // ---- Marking (collector.cpp) ----

    [[nodiscard]] std::uint8_t current_white() const {
        return static_cast<std::uint8_t>(current_white_ & gc_bits::whites);
    }

    [[nodiscard]] std::uint8_t other_white() const {
        return static_cast<std::uint8_t>(current_white_ ^ gc_bits::whites);
    }

    [[nodiscard]] bool is_dead(const GcObject* o) const { return (o->marked & other_white()) != 0; }

    void make_white(GcObject* o) const;
    static GcObject** gc_list_of(GcObject* o);
    static void link_gray(GcObject* o, GcObject*& list);
    void mark_value(Tag tag, std::uint64_t payload);
    void mark_object(GcObject* o);
    void really_mark(GcObject* o);
    void restart_collection();
    void generation_link(GcObject* o);
    std::size_t traverse_proto(Proto* p);
    std::size_t traverse_closure(LuaClosure* cl);
    std::size_t traverse_thread(State* thread);
    std::size_t traverse_table(Table* table);
    std::size_t traverse_c_closure(CClosure* cl);
    std::size_t traverse_userdata(Userdata* u);
    void traverse_strong_table(Table* table);
    void traverse_weak_value(Table* table);
    bool traverse_ephemeron(Table* table, bool inverse);
    void mark_key(const void* node);
    bool is_cleared(Tag tag, std::uint64_t payload);
    void converge_ephemerons();
    void clear_by_keys(GcObject* list);
    void clear_by_values(GcObject* list, GcObject* until);
    void mark_type_metatables();
    std::size_t mark_being_finalized();
    void separate_to_be_finalized(bool all);
    void correct_pointers(GcObject* o);
    void clear_gray_lists();
    std::size_t propagate_mark();
    std::size_t propagate_all();
    std::size_t remark_upvalues();
    std::size_t atomic();
    void forward_barrier(GcObject* parent, GcObject* child);
    void backward_barrier(GcObject* parent);

    // ---- Sweeping ----

    GcObject** sweep_list(GcObject** p, int count_in, int* count_out);
    GcObject** sweep_to_live(GcObject** p);
    void enter_sweep();
    std::size_t sweep_step(Phase next_phase, GcObject** next_list);
    void check_sizes();

    // ---- Incremental mode ----

    std::size_t single_step();
    void run_until(Phase phase);
    void incremental_step();
    void full_incremental();
    void set_pause();

    // ---- Generational mode ----

    void sweep_to_old(GcObject** p);
    GcObject** sweep_generation(GcObject** p, GcObject* limit, GcObject** first_old1);
    void white_list(GcObject* p);
    static GcObject** correct_gray_list(GcObject** p);
    void correct_gray_lists();
    void mark_old(GcObject* from, GcObject* to);
    void finish_generation_cycle();
    void young_collection();
    void atomic_to_generational();
    void set_minor_debt();
    std::size_t enter_generational();
    void enter_incremental();
    std::size_t full_generational();
    void step_generational_full();
    void generational_step();

    void stress_step();

    // ---- Finalizers ----

    GcObject* next_to_finalize();
    void call_finalizer();
    std::size_t run_some_finalizers(int n);
    void call_all_pending_finalizers();
    [[nodiscard]] bool can_run_finalizers();

    // ---- State ----

    Pool pool_;
    std::uint32_t seed_;

    // Memory accounting, as in Lua: the real total is total_bytes_ +
    // debt_; a positive debt means a collection step is due.
    std::ptrdiff_t total_bytes_ = 0;
    std::ptrdiff_t debt_ = 0;
    std::ptrdiff_t estimate_ = 0; // memory in use after the last cycle (GCestimate)
    std::size_t last_atomic_ = 0; // generational: objects seen by a bad collection

    // Interned short strings (Lua's strt): an array of buckets, a power of
    // two long, each a chain through String::next_in_bucket_.
    std::vector<String*> buckets_;
    std::size_t string_count_ = 0;
    std::size_t short_count_ = 0;

    State* main_thread_ = nullptr;
    State* running_ = nullptr;
    Table* globals_ = nullptr;
    Table* loaded_ = nullptr;
    Table* references_ = nullptr;
    Table* registry_ = nullptr;
    std::vector<std::int64_t> free_references_;
    std::int64_t next_reference_ = 1;
    std::array<Table*, 8> type_metatables_{};
    std::array<const String*, event_count> event_strings_{};
    const String* name_string_ = nullptr;
    std::function<void(std::string_view)> output_;
    std::function<void(std::string_view)> error_output_;
    bool warnings_on_ = false;

    // The object lists (Lua's allgc and fixedgc), newest first. In
    // generational mode, `survival`, `old1` and `really_old` point into
    // allgc where those generations start, and first_old1 to the first OLD1
    // object.
    GcObject* all_ = nullptr;
    GcObject* fixed_ = nullptr;
    GcObject* survival_ = nullptr;
    GcObject* old1_ = nullptr;
    GcObject* really_old_ = nullptr;
    GcObject* first_old1_ = nullptr;
    GcObject** sweep_ = nullptr; // where the sweep phase continues

    // Objects marked for finalization (Lua's finobj, with its generational
    // pointers like the ones above), and those found unreachable, waiting
    // for their finalizers (tobefnz), oldest first.
    GcObject* finalizable_ = nullptr;
    GcObject* finalizable_survival_ = nullptr;
    GcObject* finalizable_old1_ = nullptr;
    GcObject* finalizable_really_old_ = nullptr;
    GcObject* to_be_finalized_ = nullptr;

    // Gray objects to traverse; grayagain holds what must be traversed again
    // in the atomic phase (threads, touched old objects).
    GcObject* gray_ = nullptr;
    GcObject* gray_again_ = nullptr;

    // Weak tables to clear in the atomic phase: weak values, ephemerons
    // (weak keys), and all-weak ones, plus tables with white keys (Lua's
    // weak, ephemeron and allweak).
    GcObject* weak_ = nullptr;
    GcObject* ephemeron_ = nullptr;
    GcObject* all_weak_ = nullptr;

    State* threads_with_upvalues_ = nullptr; // Lua's twups

    GcParameters parameters_;
    std::uint8_t current_white_ = gc_bits::white0;
    Phase phase_ = Phase::Pause;
    GcMode kind_ = GcMode::Incremental; // the mode running now (Lua's gckind)
    GcStress stress_ = GcStress::Off;
    bool stopped_ = false;    // by the program (collectgarbage("stop"))
    bool collecting_ = false; // a step is running: no reentry
    bool in_finalizer_ = false;
    bool closing_ = false; // no new objects marked for finalization (GCSTPCLS)
    bool closed_ = false;
    std::size_t cycles_ = 0;
};

} // namespace lua2
