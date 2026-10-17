// The garbage collector: a port of Lua 5.4's lgc.c (docs/final_docs/vm.md).
// The functions keep Lua's names in their comments, and its structure, so
// the two can be read side by side. Weak tables and finalizers come with
// tables and metatables (stages 6 and 7); their parts are left out.

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <limits>

#include "gc/heap.hpp"
#include "value/table.hpp"
#include "vm/function.hpp"
#include "vm/proto.hpp"
#include "vm/state.hpp"

namespace lua2 {

namespace {

// Lua's GCSWEEPMAX: objects swept per sweep step.
constexpr int sweep_max = 100;

// Lua's WORK2MEM: the bytes one unit of work (visiting a slot, sweeping an
// object) stands for.
constexpr std::ptrdiff_t work_to_memory = 16;

// Lua's PAUSEADJ: the pause is a percentage.
constexpr std::ptrdiff_t pause_adjust = 100;

constexpr std::ptrdiff_t max_memory = std::numeric_limits<std::ptrdiff_t>::max();

// Lua keeps most parameters divided by 4 in a byte (setgcparam), so they
// act in steps of 4.
constexpr std::ptrdiff_t stored(int parameter) {
    return static_cast<std::ptrdiff_t>(parameter / 4) * 4;
}

} // namespace

// ---- Generic functions ----

void Heap::make_white(GcObject* o) const {
    // makewhite: erase the colors, then set the current white.
    o->marked = static_cast<std::uint8_t>(cleared(o->marked, gc_bits::colors) | current_white());
}

GcObject** Heap::gc_list_of(GcObject* o) {
    // getgclist: the objects that can be gray have a gc_list field.
    switch (o->type) {
    case ObjectType::Proto: return &object_cast<Proto>(o)->gc_list;
    case ObjectType::LuaClosure: return &object_cast<LuaClosure>(o)->gc_list;
    case ObjectType::Thread: return &object_cast<State>(o)->gc_list;
    case ObjectType::Table: return &object_cast<Table>(o)->gc_list;
    case ObjectType::ShortString:
    case ObjectType::LongString:
    case ObjectType::Upvalue: break;
    }
    std::abort(); // never in a gray list
}

void Heap::link_gray(GcObject* o, GcObject*& list) {
    // linkgclist: puts the object at the head of a gray list.
    *gc_list_of(o) = list;
    list = o;
    set_gray(o);
}

void Heap::forward_barrier(GcObject* parent, GcObject* child) {
    // luaC_barrier_: a black object now points to a white one.
    if (phase_ <= Phase::Atomic) {
        // While marking, the invariant must hold: mark the child now.
        really_mark(child);
        if (is_old(parent)) {
            // In generational mode the child must become old as well, but
            // through OLD0, since it may still point to young objects.
            set_age(child, gc_bits::age_old0);
        }
    } else if (kind_ == GcMode::Incremental) {
        // While sweeping, whiten the parent instead (sweep it now), so
        // that it does not trigger more barriers. Generational sweeps
        // cannot tell such whites from dead objects.
        make_white(parent);
    }
}

void Heap::backward_barrier(GcObject* parent) {
    // luaC_barrierback_: the black parent goes back to gray, to be
    // traversed again in the atomic phase.
    if (age_of(parent) == gc_bits::age_touched2) {
        set_gray(parent); // already in a gray list
    } else {
        link_gray(parent, gray_again_);
    }
    if (is_old(parent)) {
        set_age(parent, gc_bits::age_touched1);
    }
}

void Heap::upvalue_closed(Upvalue* upvalue) {
    // The end of luaF_closeupval: a closed upvalue cannot be gray.
    if (!is_white(upvalue)) {
        set_black(upvalue);
        barrier(upvalue, upvalue->value());
    }
}

// ---- Marking ----

void Heap::mark_value(Tag tag, std::uint64_t payload) {
    // markvalue
    if (tag >= first_collectable_tag) {
        auto* o = std::bit_cast<GcObject*>(payload);
        if (is_white(o)) {
            really_mark(o);
        }
    }
}

void Heap::mark_object(GcObject* o) {
    // markobjectN: the object may be absent.
    if (o != nullptr && is_white(o)) {
        really_mark(o);
    }
}

void Heap::really_mark(GcObject* o) {
    // reallymarkobject: strings and closed upvalues have nothing left to
    // visit and turn black now. Open upvalues stay gray: their value lives
    // in a stack, which is visited again. Everything else goes to the gray
    // list, to be traversed later.
    switch (o->type) {
    case ObjectType::ShortString:
    case ObjectType::LongString: set_black(o); break;
    case ObjectType::Upvalue: {
        auto* upvalue = object_cast<Upvalue>(o);
        if (upvalue->is_open()) {
            set_gray(upvalue);
        } else {
            set_black(upvalue);
        }
        mark_value(*upvalue->tag, *upvalue->payload);
        break;
    }
    case ObjectType::Proto:
    case ObjectType::LuaClosure:
    case ObjectType::Thread:
    case ObjectType::Table: link_gray(o, gray_); break;
    }
}

std::size_t Heap::remark_upvalues() {
    // remarkupvals: for each thread not marked (or without open upvalues),
    // mark the values of its open upvalues that were visited, as a barrier
    // would. The thread leaves the list either way.
    std::size_t work = 0;
    State** p = &threads_with_upvalues_;
    while (*p != nullptr) {
        State* thread = *p;
        ++work;
        if (!is_white(thread) && thread->open_upvalues != nullptr) {
            p = &thread->next_with_upvalues; // keep it
        } else {
            *p = thread->next_with_upvalues;
            thread->next_with_upvalues = thread; // out of the list
            for (const Upvalue* upvalue = thread->open_upvalues; upvalue != nullptr;
                 upvalue = upvalue->next_open) {
                ++work;
                if (!is_white(upvalue)) {
                    mark_value(*upvalue->tag, *upvalue->payload);
                }
            }
        }
    }
    return work;
}

void Heap::restart_collection() {
    // restartcollection: empty the gray lists and mark the roots.
    gray_ = nullptr;
    gray_again_ = nullptr;
    mark_object(main_thread_);
    mark_object(globals_); // (Lua marks the registry, which holds them)
}

// ---- Traversing ----

void Heap::generation_link(GcObject* o) {
    // genlink: an old object touched in this cycle goes back to grayagain;
    // one touched in the previous cycle becomes plain old.
    if (age_of(o) == gc_bits::age_touched1) {
        link_gray(o, gray_again_);
    } else if (age_of(o) == gc_bits::age_touched2) {
        set_age(o, gc_bits::age_old);
    }
}

std::size_t Heap::traverse_proto(Proto* p) {
    // traverseproto
    mark_object(mutable_object(p->source));
    for (const Value k : p->constants) {
        mark_value(k.tag(), k.payload());
    }
    for (const auto& upvalue : p->upvalues) {
        mark_object(mutable_object(upvalue.name));
    }
    for (Proto* nested : p->protos) {
        mark_object(nested);
    }
    for (const auto& local : p->locals) {
        mark_object(mutable_object(local.name));
    }
    return 1 + p->constants.size() + p->upvalues.size() + p->protos.size() + p->locals.size();
}

std::size_t Heap::traverse_closure(LuaClosure* cl) {
    // traverseLclosure: its prototype and upvalues (either may still be
    // missing while the closure is being made).
    mark_object(cl->proto);
    for (Upvalue* upvalue : cl->upvalues()) {
        mark_object(upvalue);
    }
    return 1 + cl->upvalue_count();
}

std::size_t Heap::traverse_thread(State* thread) {
    // traversethread: the live part of the stack, and the open upvalues.
    // Threads have no barriers, so while marking incrementally a thread is
    // traversed again in the atomic phase; then the dead part of its stack
    // is cleared, so that every slot holds a valid value.
    if (is_old(thread) || phase_ == Phase::Propagate) {
        link_gray(thread, gray_again_);
    }
    if (thread->stack_tags == nullptr) {
        return 1; // the stack is not built yet
    }
    for (std::uint32_t i = 0; i < thread->top; ++i) {
        mark_value(thread->stack_tags[i], thread->stack_values[i]);
    }
    for (Upvalue* upvalue = thread->open_upvalues; upvalue != nullptr;
         upvalue = upvalue->next_open) {
        mark_object(upvalue); // open upvalues cannot be collected
    }
    if (phase_ == Phase::Atomic) {
        thread->shrink_stack();
        for (std::uint32_t i = thread->top; i < thread->stack_last + extra_stack; ++i) {
            thread->stack_tags[i] = Tag::Nil;
        }
        // remark_upvalues may have taken the thread out of the list.
        if (thread->next_with_upvalues == thread && thread->open_upvalues != nullptr) {
            thread->next_with_upvalues = threads_with_upvalues_;
            threads_with_upvalues_ = thread;
        }
    }
    return 1 + thread->stack_last + extra_stack;
}

std::size_t Heap::traverse_table(Table* table) {
    // traversestrongtable (weak tables come with metatables, stage 7).
    for (const Table::Entry& entry : table->entries()) {
        if (entry.key != nullptr) {
            mark_object(mutable_object(entry.key));
            mark_value(entry.tag, entry.payload);
        }
    }
    generation_link(table);
    return 1 + table->entries().size();
}

std::size_t Heap::propagate_mark() {
    // propagatemark: traverse one gray object, which turns black.
    GcObject* o = gray_;
    set_black(o);
    gray_ = *gc_list_of(o);
    switch (o->type) {
    case ObjectType::Proto: return traverse_proto(object_cast<Proto>(o));
    case ObjectType::LuaClosure: return traverse_closure(object_cast<LuaClosure>(o));
    case ObjectType::Thread: return traverse_thread(object_cast<State>(o));
    case ObjectType::Table: return traverse_table(object_cast<Table>(o));
    case ObjectType::ShortString:
    case ObjectType::LongString:
    case ObjectType::Upvalue: break;
    }
    std::abort(); // never in a gray list
}

std::size_t Heap::propagate_all() {
    std::size_t work = 0;
    while (gray_ != nullptr) {
        work += propagate_mark();
    }
    return work;
}

std::size_t Heap::atomic() {
    // atomic: finish marking in one go. Everything the program changed
    // while marking was incremental is in grayagain.
    std::size_t work = 0;
    GcObject* again = gray_again_;
    gray_again_ = nullptr;
    phase_ = Phase::Atomic;
    mark_object(main_thread_); // the running thread
    mark_object(globals_);     // the registry may have changed
    work += propagate_all();
    work += remark_upvalues();
    work += propagate_all();
    gray_ = again;
    work += propagate_all();
    // (Weak tables are cleared here, and finalizers separated: stages 6
    // and 7.)
    current_white_ = other_white(); // what was white is now dead
    ++cycles_;
    return work;
}

// ---- Sweeping ----

GcObject** Heap::sweep_list(GcObject** p, int count_in, int* count_out) {
    // sweeplist: free the dead (the other white), whiten the survivors.
    const std::uint8_t dead = other_white();
    const std::uint8_t white = current_white();
    int i = 0;
    for (; *p != nullptr && i < count_in; ++i) {
        GcObject* current = *p;
        const std::uint8_t marked = current->marked;
        if ((marked & dead) != 0) {
            *p = current->next;
            free_object(current);
        } else {
            current->marked = static_cast<std::uint8_t>(cleared(marked, gc_bits::all) | white);
            p = &current->next;
        }
    }
    if (count_out != nullptr) {
        *count_out = i;
    }
    return *p == nullptr ? nullptr : p;
}

GcObject** Heap::sweep_to_live(GcObject** p) {
    // sweeptolive: sweep until a live object, so the sweep pointer points
    // into the list and objects made from now on are not swept.
    GcObject* const* old = p;
    do {
        p = sweep_list(p, 1, nullptr);
    } while (p == old);
    return p;
}

void Heap::enter_sweep() {
    // entersweep
    phase_ = Phase::SweepAll;
    sweep_ = sweep_to_live(&all_);
}

std::size_t Heap::sweep_step(Phase next_phase, GcObject** next_list) {
    // sweepstep
    if (sweep_ != nullptr) {
        const std::ptrdiff_t old_debt = debt_;
        int count = 0;
        sweep_ = sweep_list(sweep_, sweep_max, &count);
        estimate_ += debt_ - old_debt; // what was freed
        return static_cast<std::size_t>(count);
    }
    phase_ = next_phase;
    sweep_ = next_list;
    return 0;
}

void Heap::check_sizes() {
    // checkSizes: shrink the string table when it is mostly empty.
    if (short_count_ < buckets_.size() / 4) {
        const std::ptrdiff_t old_debt = debt_;
        resize_string_table(buckets_.size() / 2);
        estimate_ += debt_ - old_debt;
    }
}

// ---- Incremental mode ----

std::size_t Heap::single_step() {
    // singlestep: one piece of the cycle, and how much work it was.
    collecting_ = true;
    std::size_t work = 0;
    switch (phase_) {
    case Phase::Pause:
        restart_collection();
        phase_ = Phase::Propagate;
        work = 1;
        break;
    case Phase::Propagate:
        if (gray_ == nullptr) {
            phase_ = Phase::EnterAtomic;
        } else {
            work = propagate_mark();
        }
        break;
    case Phase::EnterAtomic:
        work = atomic();
        enter_sweep();
        estimate_ = static_cast<std::ptrdiff_t>(total_bytes());
        break;
    case Phase::SweepAll: work = sweep_step(Phase::SweepEnd, nullptr); break;
    case Phase::SweepEnd:
        check_sizes();
        phase_ = Phase::CallFinalizers;
        break;
    case Phase::CallFinalizers: // (finalizers: stage 7)
        phase_ = Phase::Pause;
        break;
    case Phase::Atomic: std::abort(); // only inside atomic()
    }
    collecting_ = false;
    return work;
}

void Heap::run_until(Phase phase) {
    // luaC_runtilstate
    while (phase_ != phase) {
        single_step();
    }
}

void Heap::set_pause() {
    // setpause: the next cycle starts when memory reaches estimate * pause.
    const std::ptrdiff_t pause = stored(parameters_.pause);
    const std::ptrdiff_t estimate = std::max<std::ptrdiff_t>(estimate_ / pause_adjust, 1);
    const std::ptrdiff_t threshold = pause < max_memory / estimate ? estimate * pause : max_memory;
    set_debt(std::min<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(total_bytes()) - threshold, 0));
}

void Heap::incremental_step() {
    // incstep: turn the debt into units of work, do that much (at least a
    // step's size), and set the debt that decides the next step.
    // Lua's stepmul | 1, which avoids a division by zero: stored() is a
    // multiple of 4, so that is stored() + 1.
    const std::ptrdiff_t step_multiplier = stored(parameters_.step_multiplier) + 1;
    std::ptrdiff_t debt = (debt_ / work_to_memory) * step_multiplier;
    const std::ptrdiff_t step_size =
        parameters_.step_size < std::numeric_limits<std::ptrdiff_t>::digits - 1
            ? (static_cast<std::ptrdiff_t>(std::size_t{1}
                                           << static_cast<unsigned>(parameters_.step_size)) /
               work_to_memory) *
                  step_multiplier
            : max_memory;
    do {
        debt -= static_cast<std::ptrdiff_t>(single_step());
    } while (debt > -step_size && phase_ != Phase::Pause);
    if (phase_ == Phase::Pause) {
        set_pause();
    } else {
        set_debt((debt / step_multiplier) * work_to_memory);
    }
}

void Heap::full_incremental() {
    // fullinc: if marking was under way there may be black objects, so
    // sweep everything back to white first (nothing dies: the white has
    // not changed).
    if (phase_ <= Phase::Atomic) {
        enter_sweep();
    }
    run_until(Phase::Pause);
    run_until(Phase::Propagate); // start a new cycle
    phase_ = Phase::EnterAtomic; // and go straight to the atomic phase
    run_until(Phase::CallFinalizers);
    run_until(Phase::Pause);
    set_pause();
}

// ---- Generational mode ----

void Heap::sweep_to_old(GcObject** p) {
    // sweep2old: free the dead; everything alive becomes old. Threads go
    // to grayagain (they are traversed every cycle); open upvalues stay
    // gray; everything else turns black.
    while (*p != nullptr) {
        GcObject* current = *p;
        if (is_white(current)) {
            *p = current->next;
            free_object(current);
            continue;
        }
        set_age(current, gc_bits::age_old);
        if (current->type == ObjectType::Thread) {
            link_gray(current, gray_again_);
        } else if (current->type == ObjectType::Upvalue &&
                   object_cast<Upvalue>(current)->is_open()) {
            set_gray(current);
        } else {
            set_black(current);
        }
        p = &current->next;
    }
}

GcObject** Heap::sweep_generation(GcObject** p, GcObject* limit, GcObject** first_old1) {
    // sweepgen: no new whites appear during a generational collection, so
    // every white is dead. Survivors age: new ones become survivals (and
    // white again), the others keep their color and move on.
    static constexpr std::array<std::uint8_t, 7> next_age{
        gc_bits::age_survival, // from new
        gc_bits::age_old1,     // from survival
        gc_bits::age_old1,     // from old0
        gc_bits::age_old,      // from old1
        gc_bits::age_old,      // from old (unchanged)
        gc_bits::age_touched1, // from touched1 (unchanged)
        gc_bits::age_touched2, // from touched2 (unchanged)
    };
    const std::uint8_t white = current_white();
    while (*p != limit) {
        GcObject* current = *p;
        if (is_white(current)) {
            *p = current->next;
            free_object(current);
            continue;
        }
        if (age_of(current) == gc_bits::age_new) {
            current->marked = static_cast<std::uint8_t>(cleared(current->marked, gc_bits::all) |
                                                        gc_bits::age_survival | white);
        } else {
            set_age(current, next_age.at(age_of(current)));
            if (age_of(current) == gc_bits::age_old1 && *first_old1 == nullptr) {
                *first_old1 = current;
            }
        }
        p = &current->next;
    }
    return p;
}

void Heap::white_list(GcObject* p) {
    // whitelist: in incremental mode every object is new.
    const std::uint8_t white = current_white();
    for (; p != nullptr; p = p->next) {
        p->marked = static_cast<std::uint8_t>(cleared(p->marked, gc_bits::all) | white);
    }
}

GcObject** Heap::correct_gray_list(GcObject** p) {
    // correctgraylist: after a generational sweep, keep in the list only
    // what the next cycle must see: TOUCHED1 objects (now TOUCHED2) and
    // threads. White objects are just removed; TOUCHED2 ones become old.
    while (*p != nullptr) {
        GcObject* current = *p;
        GcObject** next = gc_list_of(current);
        if (is_white(current)) {
            *p = *next;
        } else if (age_of(current) == gc_bits::age_touched1) {
            set_black(current);
            set_age(current, gc_bits::age_touched2);
            p = next;
        } else if (current->type == ObjectType::Thread) {
            p = next;
        } else {
            if (age_of(current) == gc_bits::age_touched2) {
                set_age(current, gc_bits::age_old);
            }
            set_black(current);
            *p = *next;
        }
    }
    return p;
}

void Heap::correct_gray_lists() {
    // correctgraylists (there are no weak lists yet).
    correct_gray_list(&gray_again_);
}

void Heap::mark_old(GcObject* from, GcObject* to) {
    // markold: OLD1 objects become old; the black ones are traversed again,
    // since they may point to objects that are still young.
    for (GcObject* p = from; p != to; p = p->next) {
        if (age_of(p) == gc_bits::age_old1) {
            set_age(p, gc_bits::age_old);
            if (is_black(p)) {
                really_mark(p);
            }
        }
    }
}

void Heap::finish_generation_cycle() {
    // finishgencycle
    correct_gray_lists();
    check_sizes();
    phase_ = Phase::Propagate; // skip restart
}

void Heap::young_collection() {
    // youngcollection: a minor collection. Mark the OLD1 objects, run the
    // atomic phase, then sweep only the young part of the list.
    if (first_old1_ != nullptr) {
        mark_old(first_old1_, really_old_);
        first_old1_ = nullptr;
    }
    atomic();
    phase_ = Phase::SweepAll;
    GcObject** survival = sweep_generation(&all_, survival_, &first_old1_);
    sweep_generation(survival, old1_, &first_old1_);
    really_old_ = old1_;
    old1_ = *survival; // last cycle's survivals are old now
    survival_ = all_;  // and this cycle's new objects survivals
    finish_generation_cycle();
}

void Heap::atomic_to_generational() {
    // atomic2gen: after a full mark, everything alive becomes old.
    gray_ = nullptr;
    gray_again_ = nullptr;
    phase_ = Phase::SweepAll;
    sweep_to_old(&all_);
    really_old_ = old1_ = survival_ = all_;
    first_old1_ = nullptr;
    kind_ = GcMode::Generational;
    last_atomic_ = 0;
    estimate_ = static_cast<std::ptrdiff_t>(total_bytes()); // the base for major collections
    finish_generation_cycle();
}

void Heap::set_minor_debt() {
    // setminordebt: the next minor collection comes after memory grows by
    // minor_multiplier percent.
    set_debt(-(static_cast<std::ptrdiff_t>(total_bytes() / 100) * parameters_.minor_multiplier));
}

std::size_t Heap::enter_generational() {
    // entergen: finish a whole cycle, then make everything old.
    run_until(Phase::Pause);
    run_until(Phase::Propagate);
    const std::size_t objects = atomic();
    atomic_to_generational();
    set_minor_debt();
    return objects;
}

void Heap::enter_incremental() {
    // enterinc: everything white and new again.
    white_list(all_);
    really_old_ = old1_ = survival_ = nullptr;
    phase_ = Phase::Pause;
    kind_ = GcMode::Incremental;
    last_atomic_ = 0;
}

void Heap::set_mode(GcMode mode) {
    // luaC_changemode
    if (mode != kind_) {
        if (mode == GcMode::Generational) {
            enter_generational();
        } else {
            enter_incremental();
        }
    }
    last_atomic_ = 0;
}

std::size_t Heap::full_generational() {
    // fullgen
    enter_incremental();
    return enter_generational();
}

void Heap::step_generational_full() {
    // stepgenfull: after a bad collection (one that freed little), do full
    // incremental cycles until one is good again (sees fewer objects than
    // the last by an eighth), then go back to generational mode.
    const std::size_t last = last_atomic_;
    if (kind_ == GcMode::Generational) {
        enter_incremental();
    }
    run_until(Phase::Propagate);
    const std::size_t objects = atomic();
    if (objects < last + (last >> 3U)) {
        atomic_to_generational();
        set_minor_debt();
    } else {
        estimate_ = static_cast<std::ptrdiff_t>(total_bytes());
        enter_sweep();
        run_until(Phase::Pause);
        set_pause();
        last_atomic_ = objects;
    }
}

void Heap::generational_step() {
    // genstep: usually a minor collection. When memory has grown more than
    // major_multiplier percent since the last major collection, a major
    // one; if that frees less than half the growth, it was a bad
    // collection, and the next steps are full ones (step_generational_full).
    if (last_atomic_ != 0) {
        step_generational_full();
        return;
    }
    const std::ptrdiff_t major_base = estimate_;
    const std::ptrdiff_t major_increase = (major_base / 100) * stored(parameters_.major_multiplier);
    const auto total = static_cast<std::ptrdiff_t>(total_bytes());
    if (debt_ > 0 && total > major_base + major_increase) {
        const std::size_t objects = full_generational();
        if (static_cast<std::ptrdiff_t>(total_bytes()) >= major_base + (major_increase / 2)) {
            last_atomic_ = objects; // a bad collection
            set_pause();
        }
    } else {
        young_collection();
        set_minor_debt();
        estimate_ = major_base; // keep the base of major collections
    }
}

// ---- Control ----

void Heap::step() {
    // luaC_step
    if (collecting_) {
        return;
    }
    if (stopped_) {
        set_debt(-2000); // not running: do not ask again at every check
        return;
    }
    if (stress_ != GcStress::Off) {
        stress_step();
        return;
    }
    if (mode() == GcMode::Generational) {
        generational_step();
    } else {
        incremental_step();
    }
}

void Heap::stress_step() {
    if (stress_ == GcStress::FullCycles) {
        collect();
    } else if (mode() == GcMode::Generational) {
        generational_step();
    } else {
        // The smallest step: one single step at a time.
        single_step();
        if (phase_ == Phase::Pause) {
            set_pause();
        }
    }
    set_debt(1); // so the next check collects again
}

void Heap::collect() {
    // luaC_fullgc
    if (collecting_) {
        return;
    }
    if (kind_ == GcMode::Incremental) {
        full_incremental();
    } else {
        full_generational();
    }
    if (stress_ != GcStress::Off) {
        set_debt(1);
    }
}

void Heap::stop() {
    stopped_ = true;
}

void Heap::restart() {
    // lua_gc(LUA_GCRESTART): debt 0, so a step comes soon.
    set_debt(0);
    stopped_ = false;
}

void Heap::set_stress(GcStress stress) {
    stress_ = stress;
    if (stress != GcStress::Off) {
        set_debt(1);
    }
}

} // namespace lua2
