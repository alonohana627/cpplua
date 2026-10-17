#include "vm/state.hpp"

#include <algorithm>
#include <cstring>
#include <new>

#include "vm/function.hpp"

namespace lua2 {

State::State(Heap& heap) : stack_last(basic_stack_size), top(1), heap_(&heap) {
    // Lua's stack_init: a small stack, all nil, and the bottom frame, whose
    // function slot is the first slot.
    type = ObjectType::Thread;
    const std::uint32_t slots = basic_stack_size + extra_stack;
    stack_tags = static_cast<Tag*>(heap.allocate(slots * sizeof(Tag)));
    stack_values = static_cast<std::uint64_t*>(heap.allocate(slots * sizeof(std::uint64_t)));
    std::fill_n(stack_tags, slots, Tag::Nil);
    std::fill_n(stack_values, slots, 0);
    base_ci.func = 0; // the bottom frame's function slot holds nil; top is past it
    base_ci.wanted = 0;
    base_ci.top = top + min_stack;
    ci = &base_ci;
}

State::~State() {
    ci = &base_ci;
    while (base_ci.next != nullptr) {
        CallInfo* next = base_ci.next;
        base_ci.next = next->next;
        next->~CallInfo();
        heap_->free(next, sizeof(CallInfo));
    }
    free_stack();
}

void State::free_stack() {
    const std::uint32_t slots = stack_last + extra_stack;
    heap_->free(stack_tags, slots * sizeof(Tag));
    heap_->free(stack_values, slots * sizeof(std::uint64_t));
    stack_tags = nullptr;
    stack_values = nullptr;
}

// ---- The stack (ldo.c) ----

void State::resize_stack(std::uint32_t size) {
    // luaD_reallocstack: copy what fits, nil the rest, and move the open
    // upvalues to the new arrays (correctstack). Frames hold indices, so
    // they need nothing.
    const std::uint32_t old_slots = stack_last + extra_stack;
    const std::uint32_t new_slots = size + extra_stack;
    auto* tags = static_cast<Tag*>(heap_->allocate(new_slots * sizeof(Tag)));
    auto* values = static_cast<std::uint64_t*>(heap_->allocate(new_slots * sizeof(std::uint64_t)));
    const std::uint32_t kept = std::min(old_slots, new_slots);
    std::memcpy(tags, stack_tags, kept * sizeof(Tag));
    std::memcpy(values, stack_values, kept * sizeof(std::uint64_t));
    std::fill(tags + kept, tags + new_slots, Tag::Nil);
    std::fill(values + kept, values + new_slots, 0);
    for (Upvalue* upvalue = open_upvalues; upvalue != nullptr; upvalue = upvalue->next_open) {
        upvalue->tag = tags + upvalue->level;
        upvalue->payload = values + upvalue->level;
    }
    free_stack();
    stack_tags = tags;
    stack_values = values;
    stack_last = size;
}

bool State::grow_stack(std::uint32_t n) {
    // luaD_growstack: double the stack, or more if `n` needs it, up to
    // max_stack. Past that, "stack overflow" (E6.7), with a little extra
    // room so the error itself can be handled (ERRORSTACKSIZE).
    const std::uint32_t size = stack_last;
    if (size > max_stack) {
        // Already using the extra room: an error while handling an error.
        error_message = "error in error handling";
        return false;
    }
    if (n < max_stack) {
        std::uint32_t new_size = 2 * size;
        const std::uint32_t needed = top + n;
        new_size = std::min(new_size, max_stack);
        new_size = std::max(new_size, needed);
        if (new_size <= max_stack) {
            resize_stack(new_size);
            return true;
        }
    }
    resize_stack(error_stack_size);
    error_message = "stack overflow";
    return false;
}

void State::shrink_stack() {
    // luaD_shrinkstack: the part in use is what the frames may touch.
    std::uint32_t limit = top;
    for (const CallInfo* frame = ci; frame != nullptr; frame = frame->previous) {
        limit = std::max(limit, frame->top);
    }
    const std::uint32_t in_use = std::max(limit + 1, min_stack);
    const std::uint32_t max = in_use > max_stack / 3 ? max_stack : in_use * 3;
    if (in_use <= max_stack && stack_last > max) {
        resize_stack(in_use > max_stack / 2 ? max_stack : in_use * 2);
    }
    shrink_call_infos();
}

// ---- Frames (lstate.c) ----

CallInfo* State::extend_call_info() {
    // luaE_extendCI
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by the frame list
    auto* next = new (heap_->allocate(sizeof(CallInfo))) CallInfo{};
    ci->next = next;
    next->previous = ci;
    ++call_info_count_;
    return ci = next;
}

void State::shrink_call_infos() {
    // luaE_shrinkCI: free every other unused frame.
    CallInfo* frame = ci->next;
    if (frame == nullptr) {
        return;
    }
    CallInfo* next = nullptr;
    while ((next = frame->next) != nullptr) {
        CallInfo* after = next->next;
        frame->next = after;
        --call_info_count_;
        next->~CallInfo();
        heap_->free(next, sizeof(CallInfo));
        if (after == nullptr) {
            break;
        }
        after->previous = frame;
        frame = after;
    }
}

// ---- Upvalues (lfunc.c) ----

Upvalue* State::find_upvalue(std::uint32_t level) {
    // luaF_findupval: the list is sorted by slot, highest first.
    Upvalue** p = &open_upvalues;
    while (*p != nullptr && (*p)->level >= level) {
        if ((*p)->level == level) {
            return *p;
        }
        p = &(*p)->next_open;
    }
    // newupval: a new open upvalue, linked in after `p`.
    Upvalue* upvalue = heap_->new_upvalue();
    upvalue->tag = stack_tags + level;
    upvalue->payload = stack_values + level;
    upvalue->level = level;
    upvalue->next_open = *p;
    upvalue->previous_open = p;
    if (*p != nullptr) {
        (*p)->previous_open = &upvalue->next_open;
    }
    *p = upvalue;
    if (next_with_upvalues == this) { // not yet in the Heap's list
        next_with_upvalues = heap_->threads_with_upvalues_;
        heap_->threads_with_upvalues_ = this;
    }
    return upvalue;
}

void State::close_upvalues(std::uint32_t level) {
    // luaF_closeupval: the value moves from the stack into the upvalue.
    while (open_upvalues != nullptr && open_upvalues->level >= level) {
        Upvalue* upvalue = open_upvalues;
        upvalue->unlink();
        upvalue->closed_tag = *upvalue->tag;
        upvalue->closed_payload = *upvalue->payload;
        upvalue->tag = &upvalue->closed_tag;
        upvalue->payload = &upvalue->closed_payload;
        heap_->upvalue_closed(upvalue);
    }
}

} // namespace lua2
