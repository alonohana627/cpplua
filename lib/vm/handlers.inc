// The body of every instruction, written once (docs/plan/vm.md). execute.cpp
// includes this file inside one of its dispatch styles, which define:
//
//   VM_HANDLER(Name)       starts the body of OpCode::Name
//   VM_COLD_HANDLER(Name)  starts a rarely run continuation, entered with
//                          VM_COLD(Name): an instruction's slow path, kept
//                          out of the hot body so that the body needs no
//                          stack frame
//   VM_NEXT()              goes on with the instruction at pc
//   VM_RETRY()             runs the current instruction again
//
// Each body sees the VM's state: state (the thread), pc (the next
// instruction), tags and vals (the registers of the running function: the
// two halves of its stack slots), k (its constants) and i (the instruction
// being run). A body that calls or returns changes them all, then goes on
// with VM_NEXT(). Errors return Status::Error. Lua's lvm.c is the
// reference: comments name its functions and macros.

// NOLINTBEGIN(cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-goto)

// Enters a Lua function (luaD_precall): FUNC is the stack slot of a Lua
// closure (CLOSURE, its payload), its arguments are above it up to TOP
// (exclusive). A stack too small, or no free frame, takes the cold path,
// which makes room and runs the call again.
#define VM_ENTER_LUA(FUNC, CLOSURE, TOP, WANTED)                                                            \
    do {                                                                                           \
        const std::uint32_t enter_func_ = (FUNC);                                                  \
        std::uint32_t enter_top_ = (TOP);                                                          \
        const Proto* enter_proto_ = std::bit_cast<const LuaClosure*>(CLOSURE)->proto;              \
        const std::uint32_t enter_size_ = enter_proto_->max_stack_size;                            \
        CallInfo* enter_ci_ = state->ci->next;                                                         \
        if (state->stack_last - enter_top_ <= enter_size_ || enter_ci_ == nullptr) [[unlikely]] {     \
            state->top = enter_top_;                                                                   \
            VM_COLD(GrowAndRetry);                                                                 \
        }                                                                                          \
        state->ci = enter_ci_;                                                                         \
        enter_ci_->func = enter_func_;                                                             \
        enter_ci_->top = enter_func_ + 1 + enter_size_;                                            \
        enter_ci_->wanted = (WANTED);                                                              \
        enter_ci_->fresh = false;                                                                  \
        k = enter_proto_->constants.data();                                                        \
        enter_ci_->constants = k;                                                                  \
        tags = state->stack_tags + enter_func_ + 1;                                                    \
        vals = state->stack_values + enter_func_ + 1;                                                  \
        /* missing arguments are nil (E3.1) */                                                     \
        const std::uint32_t enter_params_ = enter_func_ + 1 + enter_proto_->parameter_count;       \
        for (; enter_top_ < enter_params_; ++enter_top_) {                                         \
            state->stack_tags[enter_top_] = Tag::Nil;                                                  \
            state->stack_values[enter_top_] = 0;                                                       \
        }                                                                                          \
        state->top = enter_top_;                                                                       \
        pc = enter_proto_->code.data();                                                            \
        VM_NEXT();                                                                                 \
    } while (false)

// Goes back to the caller after a return (the 'ret' label of luaV_execute):
// out of the VM if the returning frame was entered from C++.
#define VM_RETURN_TO_CALLER(RETURNING)                                                             \
    do {                                                                                           \
        if ((RETURNING)->fresh) {                                                                  \
            return Status::Ok;                                                                     \
        }                                                                                          \
        const CallInfo* caller_ = state->ci;                                                           \
        tags = state->stack_tags + caller_->func + 1;                                                  \
        vals = state->stack_values + caller_->func + 1;                                                \
        k = caller_->constants;                                                                    \
        pc = caller_->saved_pc;                                                                    \
        VM_NEXT();                                                                                 \
    } while (false)

// The end of OP_RETURN (luaD_poscall), after any upvalues are closed.
#define VM_FINISH_RETURN()                                                                         \
    do {                                                                                           \
        CallInfo* ret_ci_ = state->ci;                                                                 \
        const std::uint32_t ret_first_ = ret_ci_->func + 1 + u32(get_a(i));                        \
        int ret_n_ = get_b(i) - 1;                                                                 \
        if (ret_n_ < 0) {                                                                          \
            ret_n_ = static_cast<int>(state->top - ret_first_); /* up to the top */                    \
        }                                                                                          \
        if (get_c(i) != 0) { /* a vararg function: back to the real function slot */              \
            ret_ci_->func -= u32(ret_ci_->extra_args + get_c(i));                                  \
        }                                                                                          \
        state->top = ret_first_ + u32(ret_n_);                                                         \
        move_results(state, ret_ci_->func, ret_n_, ret_ci_->wanted);                                   \
        state->ci = ret_ci_->previous;                                                                 \
        VM_RETURN_TO_CALLER(ret_ci_);                                                              \
    } while (false)

// checkGC: a collection step, if one is due, with the stack's live part
// ending at slot LIMIT. The stack may shrink, so the registers move.
#define VM_CHECK_GC(LIMIT)                                                                         \
    do {                                                                                           \
        if (state->heap().wants_step()) [[unlikely]] {                                                 \
            const std::uint32_t base_ = base_index(state, tags);                                       \
            state->ci->saved_pc = pc;                                                                  \
            state->top = (LIMIT);                                                                      \
            collect_step(state);                                                                       \
            tags = state->stack_tags + base_;                                                          \
            vals = state->stack_values + base_;                                                        \
        }                                                                                          \
    } while (false)

// docondjump: the next instruction is a jump, taken when cond == k.
#define VM_COND_JUMP(COND)                                                                         \
    do {                                                                                           \
        if ((COND) != get_k(i)) {                                                                  \
            ++pc;                                                                                  \
        } else {                                                                                   \
            pc += get_sj(*pc) + 1;                                                                 \
        }                                                                                          \
        VM_NEXT();                                                                                 \
    } while (false)

// ==== Cold paths ====

VM_COLD_HANDLER(GrowAndRetry) {
    // CALL, TAILCALL or TFORCALL found the stack too small for the callee
    // (checkstackGCp: collect if due, then grow), or no free frame in the
    // list (luaE_extendCI). Then the instruction runs again: everything it
    // did before is safe to repeat.
    const std::uint32_t base = base_index(state, tags);
    const int offset = get_op(i) == OpCode::TForCall ? 4 : 0;
    const std::uint32_t func = base + u32(get_a(i) + offset);
    const Proto* p = std::bit_cast<const LuaClosure*>(state->stack_values[func])->proto;
    // One more than the frame needs: the check before the retry wants
    // strictly more room than the frame (stack_last - top > size).
    const std::uint32_t needed = p->max_stack_size + 1U;
    if (state->stack_last - state->top < needed && !grow_for_call(state, needed)) {
        return stack_overflow(state, pc);
    }
    state->reserve_call_info();
    tags = state->stack_tags + base;
    vals = state->stack_values + base;
    VM_RETRY();
}

VM_COLD_HANDLER(CallOther) {
    // CALL, TAILCALL or TFORCALL of something that is not a Lua function: a
    // C++ function (precallC), or the error (luaG_callerror, E6.3).
    const OpCode op = get_op(i);
    const int a = get_a(i) + (op == OpCode::TForCall ? 4 : 0);
    if (tags[a] != Tag::CFunction) {
        return call_error(state, pc, register_value(tags, vals, a));
    }
    const std::uint32_t base = base_index(state, tags);
    const std::uint32_t func = base + u32(a);
    std::uint32_t top = 0;
    int wanted = all_results;
    if (op == OpCode::TForCall) {
        top = func + 3;
        wanted = get_c(i);
    } else {
        top = get_b(i) != 0 ? func + u32(get_b(i)) : state->top;
        if (op == OpCode::Call) {
            wanted = get_c(i) - 1;
        }
    }
    if (!call_c(state, func, top, wanted)) {
        return stack_overflow(state, pc);
    }
    tags = state->stack_tags + base; // the stack may have moved
    vals = state->stack_values + base;
    if (op != OpCode::TailCall) {
        VM_NEXT();
    }
    // A tail call: this function returns what the C++ function returned
    // (luaD_poscall after luaD_pretailcall).
    CallInfo* ci = state->ci;
    const int n = static_cast<int>(state->top - func);
    if (get_c(i) != 0) { // a vararg function: back to the real function slot
        ci->func -= u32(ci->extra_args + get_c(i));
    }
    move_results(state, ci->func, n, ci->wanted);
    state->ci = ci->previous;
    VM_RETURN_TO_CALLER(ci);
}

VM_COLD_HANDLER(SetUpvalBarrier) {
    const int a = get_a(i);
    Upvalue* upvalue = closure_of(vals)->upvalue(static_cast<std::size_t>(get_b(i)));
    state->heap().barrier(upvalue, register_value(tags, vals, a));
    VM_NEXT();
}

VM_COLD_HANDLER(DivideByZero) {
    return raise(state, pc, "attempt to divide by zero");
}

VM_COLD_HANDLER(ModuloByZero) {
    return raise(state, pc, "attempt to perform 'n%0'");
}

VM_COLD_HANDLER(FloatArith) {
    // MOD, MODK, POW and POWK on floats, which call the C library.
    const int b = get_b(i);
    const bool constant = get_op(i) == OpCode::ModK || get_op(i) == OpCode::PowK;
    const Tag tc = constant ? k[get_c(i)].tag() : tags[get_c(i)];
    const std::uint64_t* pc_value =
        constant ? k[get_c(i)].payload_address() : &vals[get_c(i)];
    const bool power = get_op(i) == OpCode::Pow || get_op(i) == OpCode::PowK;
    if (float_arith(tags, vals, get_a(i), tags[b], &vals[b], tc, pc_value,
                    power ? float_power : float_modulo)) {
        ++pc; // skip the MMBIN
    }
    VM_NEXT();
}

VM_COLD_HANDLER(Unary) {
    // UNM and BNOT on anything but plain numbers.
    const int event = get_op(i) == OpCode::Unm ? tm_unm : tm_bnot;
    if (unary_slow(state, pc, tags, vals, get_a(i), get_b(i), event) != Status::Ok) {
        return Status::Error;
    }
    VM_NEXT();
}

VM_COLD_HANDLER(LengthError) {
    const int b = get_b(i);
    return type_error(state, pc, register_value(tags, vals, b), "get length of",
                      register_info_here(state, pc, b));
}

VM_COLD_HANDLER(Compare) {
    // EQ between numbers of different kinds or long strings, EQK likewise,
    // LT and LE between anything but two integers or two floats.
    const int a = get_a(i);
    const Value left = register_value(tags, vals, a);
    bool cond = false;
    switch (get_op(i)) {
    case OpCode::Eq: cond = equals(left, register_value(tags, vals, get_b(i))); break;
    case OpCode::EqK: cond = equals(left, k[get_b(i)]); break;
    case OpCode::Lt:
    case OpCode::Le:
        if (!order_slow(state, pc, left, register_value(tags, vals, get_b(i)),
                        get_op(i) == OpCode::Le, cond)) {
            return Status::Error;
        }
        break;
    default: { // LTI, LEI, GTI, GEI with something that is not a number
        const OpCode op = get_op(i);
        const bool flip = op == OpCode::GtI || op == OpCode::GeI;
        const bool less_or_equal = op == OpCode::LeI || op == OpCode::GeI;
        if (!order_immediate_slow(state, pc, left, get_sb(i), get_c(i) != 0, flip, less_or_equal,
                                  cond)) {
            return Status::Error;
        }
        break;
    }
    }
    VM_COND_JUMP(cond);
}

VM_COLD_HANDLER(ReturnClose) {
    // OP_RETURN in a function with upvalues to close (k set).
    state->ci->saved_pc = pc;
    state->close_upvalues(state->ci->func + 1); // (and to-be-closed variables: stage 7)
    VM_FINISH_RETURN();
}

// ==== Instructions ====

// ---- Loads ----

VM_HANDLER(Move) {
    const int a = get_a(i);
    const int b = get_b(i);
    tags[a] = tags[b];
    vals[a] = vals[b];
    VM_NEXT();
}

VM_HANDLER(LoadI) {
    const int a = get_a(i);
    set_integer(tags, vals, a, get_sbx(i));
    VM_NEXT();
}

VM_HANDLER(LoadF) {
    const int a = get_a(i);
    set_float(tags, vals, a, static_cast<double>(get_sbx(i)));
    VM_NEXT();
}

VM_HANDLER(LoadK) {
    const int a = get_a(i);
    const Value& v = k[get_bx(i)];
    tags[a] = v.tag();
    vals[a] = v.payload();
    VM_NEXT();
}

VM_HANDLER(LoadKX) {
    const int a = get_a(i);
    const Value& v = k[get_ax(*pc)];
    ++pc;
    tags[a] = v.tag();
    vals[a] = v.payload();
    VM_NEXT();
}

VM_HANDLER(LoadFalse) {
    const int a = get_a(i);
    tags[a] = Tag::False;
    vals[a] = 0;
    VM_NEXT();
}

VM_HANDLER(LFalseSkip) {
    const int a = get_a(i);
    tags[a] = Tag::False;
    vals[a] = 0;
    ++pc;
    VM_NEXT();
}

VM_HANDLER(LoadTrue) {
    const int a = get_a(i);
    tags[a] = Tag::True;
    vals[a] = 0;
    VM_NEXT();
}

VM_HANDLER(LoadNil) {
    const int a = get_a(i);
    int b = get_b(i);
    Tag* t = tags + a; // NOLINT(misc-const-correctness): written through
    std::uint64_t* v = vals + a;
    do { // usually one or two registers: no call to memset
        *t++ = Tag::Nil;
        *v++ = 0;
    } while (b-- > 0);
    VM_NEXT();
}

// ---- Upvalues ----

VM_HANDLER(GetUpval) {
    const int a = get_a(i);
    const Upvalue* upvalue = closure_of(vals)->upvalue(static_cast<std::size_t>(get_b(i)));
    tags[a] = *upvalue->tag;
    vals[a] = *upvalue->payload;
    VM_NEXT();
}

VM_HANDLER(SetUpval) {
    const int a = get_a(i);
    const Upvalue* upvalue = closure_of(vals)->upvalue(static_cast<std::size_t>(get_b(i)));
    const Tag tag = tags[a];
    *upvalue->tag = tag;
    *upvalue->payload = vals[a];
    // luaC_barrier: a black upvalue must not point to a white object.
    if (tag >= first_collectable_tag && is_black(upvalue)) [[unlikely]] {
        VM_COLD(SetUpvalBarrier);
    }
    VM_NEXT();
}

// ---- Tables ----
//
// Until stage 6 the only table is the globals table, and keys are strings:
// globals (GETTABUP, SETTABUP) and `_ENV.name` work; anything else that is
// not a string key is an error, and indexing anything but a table fails as
// in Lua.

VM_HANDLER(GetTabUp) {
    const Upvalue* upvalue = closure_of(vals)->upvalue(static_cast<std::size_t>(get_b(i)));
    if (*upvalue->tag != Tag::Table) [[unlikely]] {
        return index_error_upvalue(state, pc, vals, get_b(i));
    }
    const auto* table = std::bit_cast<const Table*>(*upvalue->payload);
    set_register(tags, vals, get_a(i), table->get(string_constant(k, get_c(i))));
    VM_NEXT();
}

VM_HANDLER(GetTable) {
    const int b = get_b(i);
    const int c = get_c(i);
    if (tags[b] != Tag::Table) [[unlikely]] {
        return index_error_register(state, pc, tags, vals, b);
    }
    if (tags[c] != Tag::ShortString && tags[c] != Tag::LongString) [[unlikely]] {
        return raise(state, pc, not_string_key);
    }
    const auto* table = std::bit_cast<const Table*>(vals[b]);
    set_register(tags, vals, get_a(i), table->get(std::bit_cast<const String*>(vals[c])));
    VM_NEXT();
}

VM_HANDLER(GetI) {
    if (tags[get_b(i)] == Tag::Table) {
        return raise(state, pc, not_string_key);
    }
    return index_error_register(state, pc, tags, vals, get_b(i));
}

VM_HANDLER(GetField) {
    const int b = get_b(i);
    if (tags[b] != Tag::Table) [[unlikely]] {
        return index_error_register(state, pc, tags, vals, b);
    }
    const auto* table = std::bit_cast<const Table*>(vals[b]);
    set_register(tags, vals, get_a(i), table->get(string_constant(k, get_c(i))));
    VM_NEXT();
}

VM_HANDLER(SetTabUp) {
    const Upvalue* upvalue = closure_of(vals)->upvalue(static_cast<std::size_t>(get_a(i)));
    if (*upvalue->tag != Tag::Table) [[unlikely]] {
        return index_error_upvalue(state, pc, vals, get_a(i));
    }
    auto* table = std::bit_cast<Table*>(*upvalue->payload);
    table->set(state->heap(), string_constant(k, get_b(i)), rk_value(tags, vals, k, i));
    VM_NEXT();
}

VM_HANDLER(SetTable) {
    const int a = get_a(i);
    const int b = get_b(i);
    if (tags[a] != Tag::Table) [[unlikely]] {
        return index_error_register(state, pc, tags, vals, a);
    }
    if (tags[b] != Tag::ShortString && tags[b] != Tag::LongString) [[unlikely]] {
        return raise(state, pc, not_string_key);
    }
    auto* table = std::bit_cast<Table*>(vals[a]);
    table->set(state->heap(), std::bit_cast<const String*>(vals[b]), rk_value(tags, vals, k, i));
    VM_NEXT();
}

VM_HANDLER(SetI) {
    if (tags[get_a(i)] == Tag::Table) {
        return raise(state, pc, not_string_key);
    }
    return index_error_register(state, pc, tags, vals, get_a(i));
}

VM_HANDLER(SetField) {
    const int a = get_a(i);
    if (tags[a] != Tag::Table) [[unlikely]] {
        return index_error_register(state, pc, tags, vals, a);
    }
    auto* table = std::bit_cast<Table*>(vals[a]);
    table->set(state->heap(), string_constant(k, get_b(i)), rk_value(tags, vals, k, i));
    VM_NEXT();
}

VM_HANDLER(NewTable) {
    return raise(state, pc, "tables are not supported yet");
}

VM_HANDLER(Self) {
    // R[A+1] = R[B]; R[A] = R[B][key]: the method and its object.
    const int a = get_a(i);
    const int b = get_b(i);
    tags[a + 1] = tags[b];
    vals[a + 1] = vals[b];
    if (tags[b] != Tag::Table) [[unlikely]] {
        return index_error_register(state, pc, tags, vals, b);
    }
    const Value key = rk_value(tags, vals, k, i);
    if (!key.is_string()) [[unlikely]] {
        return raise(state, pc, not_string_key);
    }
    const auto* table = std::bit_cast<const Table*>(vals[b]);
    set_register(tags, vals, a, table->get(key.as_string()));
    VM_NEXT();
}

// ---- Arithmetic with an immediate or constant operand ----
//
// The fast paths compute and skip the MMBIN* instruction that follows;
// otherwise that instruction runs, and does the rest (op_arith and friends).

VM_HANDLER(AddI) {
    const int a = get_a(i);
    const int b = get_b(i);
    const std::int64_t immediate = get_sc(i);
    if (tags[b] == Tag::Integer) [[likely]] {
        set_integer(tags, vals, a, int_add(as_int(vals[b]), immediate));
        ++pc;
    } else if (tags[b] == Tag::Float) {
        set_float(tags, vals, a, load_float(&vals[b]) + static_cast<double>(immediate));
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(AddK) {
    const int b = get_b(i);
    const Value& c = k[get_c(i)];
    if (arith(tags, vals, get_a(i), tags[b], &vals[b], c.tag(), c.payload_address(), int_add,
              add_floats)) {
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(SubK) {
    const int b = get_b(i);
    const Value& c = k[get_c(i)];
    if (arith(tags, vals, get_a(i), tags[b], &vals[b], c.tag(), c.payload_address(), int_subtract,
              subtract_floats)) {
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(MulK) {
    const int b = get_b(i);
    const Value& c = k[get_c(i)];
    if (arith(tags, vals, get_a(i), tags[b], &vals[b], c.tag(), c.payload_address(), int_multiply,
              multiply_floats)) {
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(ModK) {
    const int b = get_b(i);
    const Value& c = k[get_c(i)];
    if (tags[b] == Tag::Integer && c.tag() == Tag::Integer) [[likely]] {
        const std::int64_t divisor = load_int(c.payload_address());
        if (divisor == 0) [[unlikely]] {
            VM_COLD(ModuloByZero);
        }
        set_integer(tags, vals, get_a(i), int_modulo(as_int(vals[b]), divisor));
        ++pc;
        VM_NEXT();
    }
    VM_COLD(FloatArith);
}

VM_HANDLER(PowK) {
    VM_COLD(FloatArith);
}

VM_HANDLER(DivK) {
    const int b = get_b(i);
    const Value& c = k[get_c(i)];
    if (float_arith(tags, vals, get_a(i), tags[b], &vals[b], c.tag(), c.payload_address(),
                    divide_floats)) {
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(IDivK) {
    const int b = get_b(i);
    const Value& c = k[get_c(i)];
    if (tags[b] == Tag::Integer && c.tag() == Tag::Integer) [[likely]] {
        const std::int64_t divisor = load_int(c.payload_address());
        if (divisor == 0) [[unlikely]] {
            VM_COLD(DivideByZero);
        }
        set_integer(tags, vals, get_a(i), int_floor_divide(as_int(vals[b]), divisor));
        ++pc;
    } else if (float_arith(tags, vals, get_a(i), tags[b], &vals[b], c.tag(), c.payload_address(),
                           float_floor_divide)) {
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(BAndK) {
    const int b = get_b(i);
    std::int64_t x = 0;
    if (to_integer_strict(tags[b], vals[b], x)) {
        set_integer(tags, vals, get_a(i), int_and(x, load_int(k[get_c(i)].payload_address())));
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(BOrK) {
    const int b = get_b(i);
    std::int64_t x = 0;
    if (to_integer_strict(tags[b], vals[b], x)) {
        set_integer(tags, vals, get_a(i), int_or(x, load_int(k[get_c(i)].payload_address())));
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(BXorK) {
    const int b = get_b(i);
    std::int64_t x = 0;
    if (to_integer_strict(tags[b], vals[b], x)) {
        set_integer(tags, vals, get_a(i), int_xor(x, load_int(k[get_c(i)].payload_address())));
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(ShrI) {
    const int b = get_b(i);
    std::int64_t x = 0;
    if (to_integer_strict(tags[b], vals[b], x)) {
        set_integer(tags, vals, get_a(i), int_shift_left(x, -std::int64_t{get_sc(i)}));
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(ShlI) {
    const int b = get_b(i);
    std::int64_t x = 0;
    if (to_integer_strict(tags[b], vals[b], x)) {
        set_integer(tags, vals, get_a(i), int_shift_left(get_sc(i), x));
        ++pc;
    }
    VM_NEXT();
}

// ---- Arithmetic on two registers ----

VM_HANDLER(Add) {
    const int b = get_b(i);
    const int c = get_c(i);
    if (arith(tags, vals, get_a(i), tags[b], &vals[b], tags[c], &vals[c], int_add, add_floats)) {
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(Sub) {
    const int b = get_b(i);
    const int c = get_c(i);
    if (arith(tags, vals, get_a(i), tags[b], &vals[b], tags[c], &vals[c], int_subtract,
              subtract_floats)) {
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(Mul) {
    const int b = get_b(i);
    const int c = get_c(i);
    if (arith(tags, vals, get_a(i), tags[b], &vals[b], tags[c], &vals[c], int_multiply,
              multiply_floats)) {
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(Mod) {
    const int b = get_b(i);
    const int c = get_c(i);
    if (tags[b] == Tag::Integer && tags[c] == Tag::Integer) [[likely]] {
        if (vals[c] == 0) [[unlikely]] {
            VM_COLD(ModuloByZero);
        }
        set_integer(tags, vals, get_a(i), int_modulo(as_int(vals[b]), as_int(vals[c])));
        ++pc;
        VM_NEXT();
    }
    VM_COLD(FloatArith);
}

VM_HANDLER(Pow) {
    VM_COLD(FloatArith);
}

VM_HANDLER(Div) {
    const int b = get_b(i);
    const int c = get_c(i);
    if (float_arith(tags, vals, get_a(i), tags[b], &vals[b], tags[c], &vals[c], divide_floats)) {
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(IDiv) {
    const int b = get_b(i);
    const int c = get_c(i);
    if (tags[b] == Tag::Integer && tags[c] == Tag::Integer) [[likely]] {
        if (vals[c] == 0) [[unlikely]] {
            VM_COLD(DivideByZero);
        }
        set_integer(tags, vals, get_a(i), int_floor_divide(as_int(vals[b]), as_int(vals[c])));
        ++pc;
    } else if (float_arith(tags, vals, get_a(i), tags[b], &vals[b], tags[c], &vals[c],
                           float_floor_divide)) {
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(BAnd) {
    const int b = get_b(i);
    const int c = get_c(i);
    std::int64_t x = 0;
    std::int64_t y = 0;
    if (to_integer_strict(tags[b], vals[b], x) && to_integer_strict(tags[c], vals[c], y)) {
        set_integer(tags, vals, get_a(i), int_and(x, y));
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(BOr) {
    const int b = get_b(i);
    const int c = get_c(i);
    std::int64_t x = 0;
    std::int64_t y = 0;
    if (to_integer_strict(tags[b], vals[b], x) && to_integer_strict(tags[c], vals[c], y)) {
        set_integer(tags, vals, get_a(i), int_or(x, y));
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(BXor) {
    const int b = get_b(i);
    const int c = get_c(i);
    std::int64_t x = 0;
    std::int64_t y = 0;
    if (to_integer_strict(tags[b], vals[b], x) && to_integer_strict(tags[c], vals[c], y)) {
        set_integer(tags, vals, get_a(i), int_xor(x, y));
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(Shl) {
    const int b = get_b(i);
    const int c = get_c(i);
    std::int64_t x = 0;
    std::int64_t y = 0;
    if (to_integer_strict(tags[b], vals[b], x) && to_integer_strict(tags[c], vals[c], y)) {
        set_integer(tags, vals, get_a(i), int_shift_left(x, y));
        ++pc;
    }
    VM_NEXT();
}

VM_HANDLER(Shr) {
    const int b = get_b(i);
    const int c = get_c(i);
    std::int64_t x = 0;
    std::int64_t y = 0;
    if (to_integer_strict(tags[b], vals[b], x) && to_integer_strict(tags[c], vals[c], y)) {
        set_integer(tags, vals, get_a(i), int_shift_right(x, y));
        ++pc;
    }
    VM_NEXT();
}

// ---- The slow paths of arithmetic (luaT_trybinTM) ----

VM_HANDLER(MmBin) {
    const int a = get_a(i);
    const int b = get_b(i);
    const Operand p1{.value = register_value(tags, vals, a), .reg = a};
    const Operand p2{.value = register_value(tags, vals, b), .reg = b};
    if (binary_slow(state, pc, tags, vals, p1, p2, get_c(i)) != Status::Ok) {
        return Status::Error;
    }
    VM_NEXT();
}

VM_HANDLER(MmBinI) {
    const int a = get_a(i);
    Operand p1{.value = register_value(tags, vals, a), .reg = a};
    Operand p2{.value = Value::integer(get_sb(i)), .reg = no_register};
    if (get_k(i)) {
        std::swap(p1, p2); // the operands were exchanged (luaT_trybinassocTM)
    }
    if (binary_slow(state, pc, tags, vals, p1, p2, get_c(i)) != Status::Ok) {
        return Status::Error;
    }
    VM_NEXT();
}

VM_HANDLER(MmBinK) {
    const int a = get_a(i);
    Operand p1{.value = register_value(tags, vals, a), .reg = a};
    Operand p2{.value = k[get_b(i)], .reg = no_register};
    if (get_k(i)) {
        std::swap(p1, p2);
    }
    if (binary_slow(state, pc, tags, vals, p1, p2, get_c(i)) != Status::Ok) {
        return Status::Error;
    }
    VM_NEXT();
}

// ---- Unary operators ----

VM_HANDLER(Unm) {
    const int a = get_a(i);
    const int b = get_b(i);
    if (tags[b] == Tag::Integer) {
        set_integer(tags, vals, a, int_negate(as_int(vals[b])));
    } else if (tags[b] == Tag::Float) {
        set_float(tags, vals, a, -load_float(&vals[b]));
    } else {
        VM_COLD(Unary);
    }
    VM_NEXT();
}

VM_HANDLER(BNot) {
    const int a = get_a(i);
    const int b = get_b(i);
    std::int64_t x = 0;
    if (!to_integer_strict(tags[b], vals[b], x)) {
        VM_COLD(Unary);
    }
    set_integer(tags, vals, a, int_not(x));
    VM_NEXT();
}

VM_HANDLER(Not) {
    const int a = get_a(i);
    const Tag t = tags[get_b(i)];
    tags[a] = t == Tag::Nil || t == Tag::False ? Tag::True : Tag::False;
    vals[a] = 0;
    VM_NEXT();
}

VM_HANDLER(Len) {
    const int a = get_a(i);
    const int b = get_b(i);
    if (tags[b] != Tag::ShortString && tags[b] != Tag::LongString) [[unlikely]] {
        VM_COLD(LengthError);
    }
    const auto* s = std::bit_cast<const String*>(vals[b]);
    set_integer(tags, vals, a, static_cast<std::int64_t>(s->length()));
    VM_NEXT();
}

VM_HANDLER(Concat) {
    const int a = get_a(i);
    const int n = get_b(i);
    if (concat_registers(state, pc, tags, vals, a, n) != Status::Ok) {
        return Status::Error;
    }
    VM_CHECK_GC(base_index(state, tags) + u32(a) + 1);
    VM_NEXT();
}

// ---- Upvalues and to-be-closed variables ----

VM_HANDLER(Close) {
    state->close_upvalues(base_index(state, tags) + u32(get_a(i)));
    VM_NEXT();
}

VM_HANDLER(Tbc) {
    // luaF_newtbcupval: false and nil need no closing; anything else needs
    // a __close metamethod, and metatables come in stage 7.
    const int a = get_a(i);
    if (tags[a] == Tag::Nil || tags[a] == Tag::False) {
        VM_NEXT();
    }
    return not_closable(state, pc, a);
}

// ---- Jumps and tests ----

VM_HANDLER(Jmp) {
    pc += get_sj(i);
    VM_NEXT();
}

VM_HANDLER(Eq) {
    // luaV_equalobj without metamethods (they come in stage 7): same tags
    // compare their payloads, except floats and long strings.
    const int a = get_a(i);
    const int b = get_b(i);
    const Tag ta = tags[a];
    const Tag tb = tags[b];
    bool cond = false;
    if (ta == tb) {
        if (ta <= Tag::True) {
            cond = true; // nil, false, true: the tag is the value
        } else if (ta == Tag::Float) {
            cond = load_float(&vals[a]) == load_float(&vals[b]); // nan, -0.0 as floats compare
        } else if (ta != Tag::LongString) {
            cond = vals[a] == vals[b]; // integers, interned strings, functions
        } else {
            VM_COLD(Compare); // long strings compare their bytes
        }
    } else if ((ta == Tag::Integer || ta == Tag::Float) && (tb == Tag::Integer || tb == Tag::Float)) {
        VM_COLD(Compare); // an integer and a float
    }
    VM_COND_JUMP(cond);
}

VM_HANDLER(Lt) {
    const int a = get_a(i);
    const int b = get_b(i);
    const unsigned pair = tag_pair(tags[a], tags[b]);
    bool cond = false;
    if (pair == integer_integer) [[likely]] {
        cond = as_int(vals[a]) < as_int(vals[b]);
    } else if (pair == float_float) {
        cond = load_float(&vals[a]) < load_float(&vals[b]);
    } else {
        VM_COLD(Compare);
    }
    VM_COND_JUMP(cond);
}

VM_HANDLER(Le) {
    const int a = get_a(i);
    const int b = get_b(i);
    const unsigned pair = tag_pair(tags[a], tags[b]);
    bool cond = false;
    if (pair == integer_integer) [[likely]] {
        cond = as_int(vals[a]) <= as_int(vals[b]);
    } else if (pair == float_float) {
        cond = load_float(&vals[a]) <= load_float(&vals[b]);
    } else {
        VM_COLD(Compare);
    }
    VM_COND_JUMP(cond);
}

VM_HANDLER(EqK) {
    // A constant is nil, a boolean, a number or a string; raw equality.
    const int a = get_a(i);
    const Value& c = k[get_b(i)];
    const Tag ta = tags[a];
    bool cond = false;
    if (ta == c.tag()) {
        if (ta <= Tag::True) {
            cond = true;
        } else if (ta == Tag::Float) {
            cond = load_float(&vals[a]) == load_float(c.payload_address());
        } else if (ta != Tag::LongString) {
            cond = vals[a] == c.payload();
        } else {
            VM_COLD(Compare);
        }
    } else if ((ta == Tag::Integer || ta == Tag::Float) && c.is_number()) {
        VM_COLD(Compare);
    }
    VM_COND_JUMP(cond);
}

VM_HANDLER(EqI) {
    const int a = get_a(i);
    const int immediate = get_sb(i);
    bool cond = false;
    if (tags[a] == Tag::Integer) {
        cond = as_int(vals[a]) == immediate;
    } else if (tags[a] == Tag::Float) {
        cond = load_float(&vals[a]) == static_cast<double>(immediate);
    }
    VM_COND_JUMP(cond);
}

VM_HANDLER(LtI) {
    const int a = get_a(i);
    const int immediate = get_sb(i);
    bool cond = false;
    if (tags[a] == Tag::Integer) [[likely]] {
        cond = as_int(vals[a]) < immediate;
    } else if (tags[a] == Tag::Float) {
        cond = load_float(&vals[a]) < static_cast<double>(immediate);
    } else {
        VM_COLD(Compare);
    }
    VM_COND_JUMP(cond);
}

VM_HANDLER(LeI) {
    const int a = get_a(i);
    const int immediate = get_sb(i);
    bool cond = false;
    if (tags[a] == Tag::Integer) [[likely]] {
        cond = as_int(vals[a]) <= immediate;
    } else if (tags[a] == Tag::Float) {
        cond = load_float(&vals[a]) <= static_cast<double>(immediate);
    } else {
        VM_COLD(Compare);
    }
    VM_COND_JUMP(cond);
}

VM_HANDLER(GtI) {
    const int a = get_a(i);
    const int immediate = get_sb(i);
    bool cond = false;
    if (tags[a] == Tag::Integer) [[likely]] {
        cond = as_int(vals[a]) > immediate;
    } else if (tags[a] == Tag::Float) {
        cond = load_float(&vals[a]) > static_cast<double>(immediate);
    } else {
        VM_COLD(Compare);
    }
    VM_COND_JUMP(cond);
}

VM_HANDLER(GeI) {
    const int a = get_a(i);
    const int immediate = get_sb(i);
    bool cond = false;
    if (tags[a] == Tag::Integer) [[likely]] {
        cond = as_int(vals[a]) >= immediate;
    } else if (tags[a] == Tag::Float) {
        cond = load_float(&vals[a]) >= static_cast<double>(immediate);
    } else {
        VM_COLD(Compare);
    }
    VM_COND_JUMP(cond);
}

VM_HANDLER(Test) {
    const Tag t = tags[get_a(i)];
    const bool cond = t != Tag::Nil && t != Tag::False;
    VM_COND_JUMP(cond);
}

VM_HANDLER(TestSet) {
    const int b = get_b(i);
    const bool falsy = tags[b] == Tag::Nil || tags[b] == Tag::False;
    if (falsy == get_k(i)) {
        ++pc;
    } else {
        const int a = get_a(i);
        tags[a] = tags[b];
        vals[a] = vals[b];
        pc += get_sj(*pc) + 1;
    }
    VM_NEXT();
}

// ---- Calls and returns ----

VM_HANDLER(Call) {
    const int a = get_a(i);
    const int b = get_b(i);
    const std::uint32_t func = base_index(state, tags) + u32(a);
    state->ci->saved_pc = pc;
    if (tags[a] != Tag::LuaClosure) [[unlikely]] {
        VM_COLD(CallOther);
    }
    // B is the number of arguments plus one, or 0 when the previous
    // instruction left them up to the top.
    VM_ENTER_LUA(func, vals[a], b != 0 ? func + u32(b) : state->top, get_c(i) - 1);
}

VM_HANDLER(TailCall) {
    // A call that reuses the frame (E6.5, luaD_pretailcall).
    const int a = get_a(i);
    int b = get_b(i);
    const int fixed_params1 = get_c(i);
    CallInfo* ci = state->ci;
    const std::uint32_t base = ci->func + 1;
    const std::uint32_t func = base + u32(a);
    if (b != 0) {
        state->top = func + u32(b);
    } else {
        b = static_cast<int>(state->top - func);
    }
    ci->saved_pc = pc;
    if (get_k(i)) {
        state->close_upvalues(base); // safe to repeat if the call is retried
    }
    if (tags[a] != Tag::LuaClosure) [[unlikely]] {
        VM_COLD(CallOther);
    }
    const Proto* p = std::bit_cast<const LuaClosure*>(vals[a])->proto;
    const int size = p->max_stack_size;
    const int delta = fixed_params1 != 0 ? ci->extra_args + fixed_params1 : 0;
    if (static_cast<std::int64_t>(state->stack_last) - state->top <= size - delta) [[unlikely]] {
        VM_COLD(GrowAndRetry);
    }
    ci->func -= u32(delta);
    const std::uint32_t new_func = ci->func;
    for (int j = 0; j < b; ++j) { // the function and its arguments move down
        state->stack_tags[new_func + u32(j)] = state->stack_tags[func + u32(j)];
        state->stack_values[new_func + u32(j)] = state->stack_values[func + u32(j)];
    }
    int narg1 = b;
    for (; std::cmp_less_equal(narg1, p->parameter_count); ++narg1) {
        state->stack_tags[new_func + u32(narg1)] = Tag::Nil;
        state->stack_values[new_func + u32(narg1)] = 0;
    }
    ci->top = new_func + 1 + u32(size);
    state->top = new_func + u32(narg1);
    tags = state->stack_tags + new_func + 1;
    vals = state->stack_values + new_func + 1;
    k = p->constants.data();
    ci->constants = k;
    pc = p->code.data();
    VM_NEXT();
}

VM_HANDLER(Return) {
    if (get_k(i)) {
        VM_COLD(ReturnClose);
    }
    VM_FINISH_RETURN();
}

VM_HANDLER(Return0) {
    const CallInfo* ci = state->ci;
    state->top = ci->func;
    for (int wanted = ci->wanted; wanted > 0; --wanted) { // all results are nil
        state->stack_tags[state->top] = Tag::Nil;
        state->stack_values[state->top] = 0;
        ++state->top;
    }
    state->ci = ci->previous;
    VM_RETURN_TO_CALLER(ci);
}

VM_HANDLER(Return1) {
    const CallInfo* ci = state->ci;
    const int wanted = ci->wanted;
    if (wanted == 0) {
        state->top = ci->func;
    } else {
        const int a = get_a(i);
        tags[-1] = tags[a]; // the function's slot
        vals[-1] = vals[a];
        state->top = ci->func + 1;
        for (int missing = wanted; missing > 1; --missing) {
            state->stack_tags[state->top] = Tag::Nil;
            state->stack_values[state->top] = 0;
            ++state->top;
        }
    }
    state->ci = ci->previous;
    VM_RETURN_TO_CALLER(ci);
}

// ---- Loops ----

VM_HANDLER(ForLoop) {
    const int a = get_a(i);
    if (tags[a + 2] == Tag::Integer) [[likely]] {
        // An integer loop counts down the iterations left (E5.4).
        const std::uint64_t count = vals[a + 1];
        if (count > 0) {
            vals[a + 1] = count - 1;
            const std::uint64_t index = vals[a] + vals[a + 2]; // wraps, as Lua's intop
            vals[a] = index;
            tags[a + 3] = Tag::Integer;
            vals[a + 3] = index;
            pc -= get_bx(i);
        }
    } else if (float_for_loop(tags, vals, a)) {
        pc -= get_bx(i);
    }
    VM_NEXT();
}

VM_HANDLER(ForPrep) {
    switch (for_prepare(state, pc, tags, vals, get_a(i))) {
    case ForPrep::Run: break;
    case ForPrep::Skip: pc += get_bx(i) + 1; break;
    case ForPrep::Error: return Status::Error;
    }
    VM_NEXT();
}

VM_HANDLER(TForPrep) {
    // The closing value (the fourth) is to be closed (stage 7): only nil
    // and false are closable yet.
    const int a = get_a(i);
    if (tags[a + 3] != Tag::Nil && tags[a + 3] != Tag::False) [[unlikely]] {
        return not_closable(state, pc, a + 3);
    }
    pc += get_bx(i);
    VM_NEXT();
}

VM_HANDLER(TForCall) {
    // Calls the iterator with the state and the control value, its results
    // going to R[A+4] onward; the TFORLOOP after this continues (E5.7).
    const int a = get_a(i);
    for (int j = 0; j < 3; ++j) {
        tags[a + 4 + j] = tags[a + j];
        vals[a + 4 + j] = vals[a + j];
    }
    const std::uint32_t func = base_index(state, tags) + u32(a + 4);
    state->ci->saved_pc = pc;
    if (tags[a + 4] != Tag::LuaClosure) [[unlikely]] {
        VM_COLD(CallOther);
    }
    VM_ENTER_LUA(func, vals[a + 4], func + 3, get_c(i));
}

VM_HANDLER(TForLoop) {
    const int a = get_a(i);
    if (tags[a + 4] != Tag::Nil) { // continue the loop
        tags[a + 2] = tags[a + 4];
        vals[a + 2] = vals[a + 4];
        pc -= get_bx(i);
    }
    VM_NEXT();
}

VM_HANDLER(SetList) {
    return raise(state, pc, "tables are not supported yet");
}

// ---- Functions ----

VM_HANDLER(Closure) {
    // pushclosure: a new closure of a nested prototype, with its upvalues
    // found in this function's registers or upvalues (E6.1, E7).
    const int a = get_a(i);
    const LuaClosure* enclosing = closure_of(vals);
    Proto* p = enclosing->proto->protos[static_cast<std::size_t>(get_bx(i))];
    const std::uint32_t base = base_index(state, tags);
    LuaClosure* closure = state->heap().new_closure(p, p->upvalues.size());
    tags[a] = Tag::LuaClosure; // anchored in the stack before any other allocation
    vals[a] = std::bit_cast<std::uint64_t>(closure);
    const auto upvalues = closure->upvalues();
    for (std::size_t j = 0; j < upvalues.size(); ++j) {
        const Proto::UpvalueInfo& info = p->upvalues[j];
        upvalues[j] = info.in_stack ? state->find_upvalue(base + info.index)
                                    : enclosing->upvalue(info.index);
        state->heap().object_barrier(closure, upvalues[j]);
    }
    VM_CHECK_GC(base + u32(a) + 1);
    VM_NEXT();
}

VM_HANDLER(VarArg) {
    // luaT_getvarargs: the extra arguments sit below the frame.
    const int a = get_a(i);
    int wanted = get_c(i) - 1;
    const CallInfo* ci = state->ci;
    const int extra = ci->extra_args;
    const std::uint32_t where = ci->func + 1 + u32(a);
    if (wanted < 0) { // all of them
        wanted = extra;
        state->top = ci->top;
        if (state->stack_last - state->top <= u32(extra)) [[unlikely]] {
            state->ci->saved_pc = pc;
            if (!grow_for_call(state, u32(extra))) {
                return stack_overflow(state, pc);
            }
        }
        state->top = where + u32(extra);
    }
    const std::uint32_t from = ci->func - u32(extra);
    int j = 0;
    for (; j < wanted && j < extra; ++j) {
        state->stack_tags[where + u32(j)] = state->stack_tags[from + u32(j)];
        state->stack_values[where + u32(j)] = state->stack_values[from + u32(j)];
    }
    for (; j < wanted; ++j) {
        state->stack_tags[where + u32(j)] = Tag::Nil;
        state->stack_values[where + u32(j)] = 0;
    }
    tags = state->stack_tags + ci->func + 1; // the stack may have moved
    vals = state->stack_values + ci->func + 1;
    VM_NEXT();
}

VM_HANDLER(VarArgPrep) {
    // luaT_adjustvarargs: the function and its fixed parameters move above
    // the extra arguments, which stay where they are for VARARG.
    const int fixed = get_a(i);
    CallInfo* ci = state->ci;
    const Proto* p = closure_of(vals)->proto;
    const int actual = static_cast<int>(state->top - ci->func) - 1;
    ci->extra_args = actual - fixed;
    state->ci->saved_pc = pc;
    if (!state->check_stack(static_cast<std::uint32_t>(p->max_stack_size) + 1)) [[unlikely]] {
        return stack_overflow(state, pc);
    }
    state->stack_tags[state->top] = state->stack_tags[ci->func];
    state->stack_values[state->top] = state->stack_values[ci->func];
    ++state->top;
    for (int j = 1; j <= fixed; ++j) {
        state->stack_tags[state->top] = state->stack_tags[ci->func + u32(j)];
        state->stack_values[state->top] = state->stack_values[ci->func + u32(j)];
        ++state->top;
        state->stack_tags[ci->func + u32(j)] = Tag::Nil; // for the collector
        state->stack_values[ci->func + u32(j)] = 0;
    }
    ci->func += u32(actual) + 1;
    ci->top += u32(actual) + 1;
    tags = state->stack_tags + ci->func + 1;
    vals = state->stack_values + ci->func + 1;
    VM_NEXT();
}

VM_HANDLER(ExtraArg) {
    return raise(state, pc, "invalid instruction");
}

#undef VM_ENTER_LUA
#undef VM_RETURN_TO_CALLER
#undef VM_FINISH_RETURN
#undef VM_CHECK_GC
#undef VM_COND_JUMP

// NOLINTEND(cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-goto)
