// The public C++ API (include/cpplua/api.hpp, include/cpplua/interpreter.hpp)
// on top of the internals: Context is a view of a thread's stack, Ref an
// entry in the heap's table of references, bound functions C++ closures
// whose upvalue is a userdata holding the std::function, and classes
// metatables found through their tag
// (docs/how_to_put_cpp_functions_and_classes_into_lua.md).

#include <exception>
#include <format>
#include <initializer_list>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

#include <cpplua/api.hpp>
#include <cpplua/interpreter.hpp>

#include "gc/heap.hpp"
#include "std/auxlib.hpp"
#include "std/libs.hpp"
#include "value/convert.hpp"
#include "value/number.hpp"
#include "value/table.hpp"
#include "value/userdata.hpp"
#include "vm/execute.hpp"
#include "vm/function.hpp"
#include "vm/metamethods.hpp"
#include "vm/run.hpp"
#include "vm/state.hpp"

namespace cpplua {

namespace {

// NOLINTBEGIN(misc-const-correctness,readability-non-const-parameter): the CFunction signature

Value string_value(Heap& heap, std::string_view s) {
    return Value::string(heap.new_string(s));
}

// A table value's object, which may be changed.
Table* table_value(Value v) {
    return const_cast<Table*>(v.as_table()); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

// The C++ function behind every bound Function: its upvalue 1 is a
// userdata holding the std::function. C++ exceptions stop here and become
// Lua errors.
int call_bound(State& state) {
    const auto* closure = state.get(state.ci->func).as_c_closure();
    auto* function = static_cast<Function*>(closure->upvalues()[0].as_userdata()->data());
    Context ctx{state};
    int n = 0;
    try {
        n = (*function)(ctx);
    } catch (const std::bad_alloc&) {
        state.set_error(Value::string(state.heap().memory_error_string()));
        return c_function_error;
    } catch (const std::exception& e) {
        return raise_error(state, e.what());
    } catch (...) {
        return raise_error(state, "unknown C++ exception");
    }
    if (n < 0 || ctx.failed()) {
        return c_function_error;
    }
    // A raw function's count is checked: the VM takes the results from
    // the top, and must not read below them.
    if (n > ctx.top()) {
        return raise_error(state, std::format("C++ function returned {} results but pushed only {}",
                                              n, ctx.top()));
    }
    return n;
}

// A Function as a Lua value.
Value make_bound(Heap& heap, Function f) {
    Userdata* box = heap.new_userdata(sizeof(Function), 0);
    new (box->data()) Function(std::move(f)); // NOLINT(cppcoreguidelines-owning-memory)
    box->destructor = [](void* p) { static_cast<Function*>(p)->~Function(); };
    CClosure* closure = heap.new_c_closure(&call_bound, 1); // no collection between the two
    closure->upvalues()[0] = Value::userdata(box);
    return Value::c_closure(closure);
}

// Ref::get and Ref::set, run in protected mode: (t, key) -> t[key], and
// (t, key, value) -> t[key] = value.
int protected_index(State& state) {
    Value result;
    if (index_value(state, arg(state, 1), arg(state, 2), result) != Status::Ok) {
        return c_function_error;
    }
    push(state, result);
    return 1;
}

int protected_assign(State& state) {
    if (assign_value(state, arg(state, 1), arg(state, 2), arg(state, 3)) != Status::Ok) {
        return c_function_error;
    }
    return 0;
}

// ---- Classes ----

// The metatable of the class with this tag, or nullptr.
Table* class_metatable(Heap& heap, const void* tag) {
    const Value mt = heap.references().get(Value::light_userdata(tag)).value();
    return mt.is_table() ? std::bit_cast<Table*>(mt.payload()) : nullptr;
}

// The key, in the table of references, of the table that maps each class's
// tag to its class table (where its functions and methods go, whatever its
// metatable's __index is now).
constexpr char class_tables_key = 0;

Table& class_tables(Heap& heap) {
    const Value key = Value::light_userdata(&class_tables_key);
    const Value tables = heap.references().get(key).value();
    if (tables.is_table()) {
        return *std::bit_cast<Table*>(tables.payload());
    }
    Table* t = heap.new_table();
    heap.references().set(heap, key, Value::table(t));
    return *t;
}

bool call_pushing_one(State& state, std::initializer_list<Value> values);

// Indexes `fallback` (a class's own __index or __newindex, kept when it
// has properties too) as a metamethod would: a function is called, a table
// indexed.
int fallback_index(State& state, Value fallback, Value self, Value key) {
    if (fallback.is_function()) {
        return call_pushing_one(state, {fallback, self, key}) ? 1 : c_function_error;
    }
    Value result;
    if (index_value(state, fallback, key, result) != Status::Ok) {
        return c_function_error;
    }
    push(state, result);
    return 1;
}

// Calls `f(args...)` from a C++ function with one result, pushed.
bool call_pushing_one(State& state, std::initializer_list<Value> values) {
    const std::uint32_t func = state.top;
    for (const Value v : values) {
        state.push(v);
    }
    return call(state, func, 1) == Status::Ok;
}

// An object's own fields table (its user value), or nil.
Value object_fields(Value self) {
    if (!self.is_userdata() || self.as_userdata()->user_values().empty()) {
        return Value::nil();
    }
    return self.as_userdata()->user_values()[0];
}

// __index of objects with properties: methods (in the class table), then
// getters, then the object's own fields, then the class's own __index, if it
// was given one. Upvalues: the class table, the getters, that __index.
int class_index(State& state) {
    const auto* closure = state.get(state.ci->func).as_c_closure();
    const Value self = arg(state, 1);
    const Value key = arg(state, 2);
    const Value method = table_value(closure->upvalues()[0])->get(key).value();
    if (!method.is_nil()) {
        push(state, method);
        return 1;
    }
    const Value getter = table_value(closure->upvalues()[1])->get(key).value();
    if (!getter.is_nil()) {
        return call_pushing_one(state, {getter, self}) ? 1 : c_function_error;
    }
    const Value fields = object_fields(self);
    const Value field = fields.is_table() ? table_value(fields)->get(key).value() : Value::nil();
    if (field.is_nil() && !closure->upvalues()[2].is_nil()) {
        return fallback_index(state, closure->upvalues()[2], self, key);
    }
    push(state, field);
    return 1;
}

// __newindex: setters, else an error for read-only properties, else the
// class's own __newindex if it was given one, else the object's own fields
// if the class allows them. Upvalues: the setters, the getters, whether
// fields are allowed, that __newindex.
int class_newindex(State& state) {
    auto* closure = std::bit_cast<CClosure*>(state.get(state.ci->func).payload());
    const Value self = arg(state, 1);
    const Value key = arg(state, 2);
    const Value value = arg(state, 3);
    const Value setter = table_value(closure->upvalues()[0])->get(key).value();
    if (!setter.is_nil()) {
        const std::uint32_t func = state.top;
        state.push(setter);
        state.push(self);
        state.push(value);
        return call(state, func, 0) == Status::Ok ? 0 : c_function_error;
    }
    const std::string name = key.is_string() ? std::string{key.as_string()->view()} : "?";
    const std::string_view type = object_type_name(state.heap(), self);
    if (!table_value(closure->upvalues()[1])->get(key).value().is_nil()) {
        return raise_error(state, std::format("property '{}' of {} is read-only", name, type));
    }
    if (const Value fallback = closure->upvalues()[3]; !fallback.is_nil()) {
        if (fallback.is_function()) {
            const std::uint32_t func = state.top;
            for (const Value v : {fallback, self, key, value}) {
                state.push(v);
            }
            return call(state, func, 0) == Status::Ok ? 0 : c_function_error;
        }
        return assign_value(state, fallback, key, value) == Status::Ok ? 0 : c_function_error;
    }
    if (!closure->upvalues()[2].is_truthy() || !self.is_userdata() ||
        self.as_userdata()->user_values().empty()) {
        return raise_error(state, std::format("{} has no field '{}'", type, name));
    }
    if (const char* error = Table::check_key(key)) {
        return raise_error(state, error);
    }
    Userdata* u = self.as_userdata();
    Value fields = u->user_values()[0];
    if (!fields.is_table()) {
        fields = Value::table(state.heap().new_table());
        u->user_values()[0] = fields;
        state.heap().barrier(u, fields);
    }
    auto* table = std::bit_cast<Table*>(fields.payload());
    table->set(state.heap(), key, value);
    return 0;
}

// NOLINTEND(misc-const-correctness,readability-non-const-parameter)

} // namespace

// ==== Context ====

Context::Context(State& state) : state_(&state), base_(state.ci->func) {}

std::uint32_t Context::slot(int n) const {
    return base_ + static_cast<std::uint32_t>(n);
}

int Context::arg_count() const {
    return static_cast<int>(state_->top - base_) - 1;
}

int Context::top() const {
    return arg_count();
}

void Context::set_top(int n) {
    if (n < 0) { // lua_settop's index from the top
        n = std::max(top() + n + 1, 0);
    }
    const std::uint32_t target = slot(n) + 1;
    while (state_->top < target) {
        reserve();
        state_->push(Value::nil());
    }
    state_->top = target;
}

bool Context::ensure(int n) {
    return n <= 0 || state_->check_stack(static_cast<std::uint32_t>(n));
}

bool Context::valid(int n) const {
    return n >= 1 && n <= top();
}

void Context::reserve() {
    if (!state_->check_stack(1)) {
        throw std::length_error("stack overflow (too many values pushed)");
    }
}

bool Context::is_none(int n) const {
    return n < 1 || n > arg_count();
}

namespace {
Value value_at(const State& state, std::uint32_t slot, bool none) {
    return none ? Value::nil() : state.get(slot);
}
} // namespace

Type Context::type(int n) const {
    return value_at(*state_, slot(n), is_none(n)).type();
}

std::string Context::type_name(int n) const {
    if (is_none(n)) {
        return "no value";
    }
    return std::string{object_type_name(state_->heap(), state_->get(slot(n)))};
}

bool Context::to_boolean(int n) const {
    return value_at(*state_, slot(n), is_none(n)).is_truthy();
}

std::optional<double> Context::to_number(int n) const {
    Value v = value_at(*state_, slot(n), is_none(n));
    if (v.is_string()) {
        const auto number = string_to_number(v.as_string()->view());
        if (!number) {
            return std::nullopt;
        }
        v = *number;
    }
    return to_float(v);
}

std::optional<std::int64_t> Context::to_integer(int n) const {
    Value v = value_at(*state_, slot(n), is_none(n));
    if (v.is_string()) {
        const auto number = string_to_number(v.as_string()->view());
        if (!number) {
            return std::nullopt;
        }
        v = *number;
    }
    return cpplua::to_integer(v);
}

std::optional<std::string_view> Context::to_string(int n) {
    if (is_none(n)) {
        return std::nullopt;
    }
    const Value v = state_->get(slot(n));
    if (v.is_number()) { // converted in place, so the view stays valid
        state_->set(slot(n), string_value(state_->heap(), display_text(v)));
    } else if (!v.is_string()) {
        return std::nullopt;
    }
    return state_->get(slot(n)).as_string()->view();
}

void* Context::to_pointer(int n) const {
    const Value v = value_at(*state_, slot(n), is_none(n));
    return v.is_light_userdata() ? v.as_light_userdata() : nullptr;
}

void* Context::object_data(int n, const void* tag) const {
    const Value v = value_at(*state_, slot(n), is_none(n));
    if (!v.is_userdata() || v.as_userdata()->type_tag != tag) {
        return nullptr;
    }
    Userdata* u = v.as_userdata();
    return u->holds_pointer ? *static_cast<void**>(u->data()) : u->data();
}

Ref Context::ref(int n) const {
    // (A Ref needs no stack slot of its own: the value goes straight to the
    // table of references.)
    const Value v = value_at(*state_, slot(n), is_none(n));
    Ref r;
    if (!v.is_nil()) {
        static_cast<void>(state_->check_stack(1));
        state_->push(v);
        r = Ref::from_top(*state_);
    }
    return r;
}

void Context::push_nil() {
    reserve();
    state_->push(Value::nil());
}

void Context::push_boolean(bool b) {
    reserve();
    state_->push(Value::boolean(b));
}

void Context::push_integer(std::int64_t i) {
    reserve();
    state_->push(Value::integer(i));
}

void Context::push_number(double d) {
    reserve();
    state_->push(Value::number(d));
}

void Context::push_string(std::string_view s) {
    reserve();
    state_->push(string_value(state_->heap(), s));
}

void Context::push_pointer(void* p) {
    reserve();
    state_->push(Value::light_userdata(p));
}

void Context::push(const Ref& r) {
    reserve();
    r.push_to(*state_);
}

void Context::push_copy(int n) {
    reserve();
    state_->push(value_at(*state_, slot(n), is_none(n)));
}

void* Context::push_userdata(const void* tag, std::size_t size) {
    Heap& heap = state_->heap();
    if (class_metatable(heap, tag) == nullptr) {
        error("this C++ class has no Lua class (Interpreter::new_class)");
        return nullptr;
    }
    reserve();
    Userdata* u = heap.new_userdata(size, 1); // one user value: the object's own fields
    state_->push(Value::userdata(u));
    return u->data();
}

void Context::finish_object(const void* tag, void (*destructor)(void*)) {
    Heap& heap = state_->heap();
    Userdata* u = state_->get(state_->top - 1).as_userdata();
    u->type_tag = tag;
    u->destructor = destructor;
    heap.set_metatable(u, class_metatable(heap, tag)); // with __gc, from now on
}

bool Context::push_member(int owner, void* object, const void* tag) {
    Heap& heap = state_->heap();
    Table* mt = class_metatable(heap, tag);
    if (mt == nullptr) {
        error("this C++ class has no Lua class (Interpreter::new_class)");
        return false;
    }
    reserve();
    // User values: the object's own fields, and its owner, kept alive.
    Userdata* u = heap.new_userdata(sizeof(void*), 2);
    *static_cast<void**>(u->data()) = object;
    u->holds_pointer = true;
    u->type_tag = tag;
    u->user_values()[1] = state_->get(slot(owner));
    state_->push(Value::userdata(u));
    heap.set_metatable(u, mt);
    return true;
}

std::string Context::class_name(const void* tag) const {
    if (Table* mt = class_metatable(state_->heap(), tag)) {
        const Value name = mt->get_short_string(state_->heap().name_string()).value();
        if (name.is_string()) {
            return std::string{name.as_string()->view()};
        }
    }
    return "object";
}

int Context::error(std::string_view message) {
    failed_ = true;
    return raise_error(*state_, message);
}

int Context::raise(std::string_view message) {
    failed_ = true;
    state_->set_error(message);
    return function_error;
}

int Context::arg_error(int n, std::string_view extra) {
    failed_ = true;
    return cpplua::arg_error(*state_, n, extra);
}

int Context::type_error(int n, std::string_view expected) {
    failed_ = true;
    return arg_type_error(*state_, n, expected);
}

std::expected<void, std::string> Context::call(int func, int args, int results) {
    const std::uint32_t f = slot(func);
    state_->top = f + 1 + static_cast<std::uint32_t>(args);
    if (protected_call(*state_, f, results < 0 ? all_results : results, Value::nil()) !=
        Status::Ok) {
        std::string message = error_text(*state_, state_->get(f));
        state_->top = f;
        return std::unexpected(std::move(message));
    }
    return {};
}

bool Context::get_field(int t, std::string_view key) {
    reserve();
    Value result;
    const Value table = valid(t) ? state_->get(slot(t)) : Value::nil();
    if (index_value(*state_, table, string_value(state_->heap(), key), result) != Status::Ok) {
        failed_ = true;
        return false;
    }
    state_->push(result);
    return true;
}

Ref Context::field(int t, std::string_view key) {
    if (!get_field(t, key)) {
        return {};
    }
    return Ref::from_top(*state_);
}

bool Context::set_field(int t, std::string_view key) {
    if (top() < 1) {
        error("set_field: no value to assign");
        return false;
    }
    const Value value = state_->get(state_->top - 1);
    const Value table = valid(t) ? state_->get(slot(t)) : Value::nil();
    if (assign_value(*state_, table, string_value(state_->heap(), key), value) != Status::Ok) {
        failed_ = true;
        return false;
    }
    --state_->top;
    return true;
}

ScopedStack::ScopedStack(State& state) : Context(state, state.top - 1), saved_top_(state.top) {
    static_cast<void>(state.check_stack(min_stack));
}

ScopedStack::~ScopedStack() {
    state().top = saved_top_;
}

// ==== Ref ====

Ref Ref::from_top(State& state) {
    Ref r;
    const Value v = state.get(state.top - 1);
    --state.top;
    if (!v.is_nil()) {
        r.heap_ = &state.heap();
        r.key_ = r.heap_->reference(v);
    }
    return r;
}

Ref::Ref(const Ref& other) : heap_(other.heap_) {
    if (heap_ != nullptr) {
        key_ = heap_->reference(heap_->referenced(other.key_));
    }
}

Ref& Ref::operator=(const Ref& other) {
    if (this != &other) {
        Ref copy{other};
        *this = std::move(copy);
    }
    return *this;
}

Ref::Ref(Ref&& other) noexcept
    : heap_(std::exchange(other.heap_, nullptr)), key_(std::exchange(other.key_, 0)) {}

Ref& Ref::operator=(Ref&& other) noexcept {
    if (this != &other) {
        if (heap_ != nullptr) {
            heap_->release_reference(key_);
        }
        heap_ = std::exchange(other.heap_, nullptr);
        key_ = std::exchange(other.key_, 0);
    }
    return *this;
}

Ref::~Ref() {
    if (heap_ != nullptr && !heap_->is_closed()) {
        heap_->release_reference(key_);
    }
}

Type Ref::type() const {
    return heap_ == nullptr ? Type::Nil : heap_->referenced(key_).type();
}

void Ref::push_to(State& state) const {
    if (heap_ != nullptr && heap_ != &state.heap()) {
        throw std::invalid_argument("a Ref of another interpreter");
    }
    static_cast<void>(state.check_stack(1));
    state.push(heap_ == nullptr ? Value::nil() : heap_->referenced(key_));
}

State* Ref::state() const {
    // The running thread, so that a call from a C++ function that a
    // coroutine called runs in that coroutine.
    return heap_ == nullptr ? nullptr : heap_->running_thread();
}

std::expected<Ref, std::string> Ref::index(std::string_view key) const {
    if (heap_ == nullptr) {
        return std::unexpected(std::string{"attempt to index a nil value"});
    }
    ScopedStack stack{*state()};
    stack.state().push(Value::c_function(&protected_index));
    const int func = stack.top();
    push_to(*state());
    stack.push_string(key);
    if (auto called = stack.call(func, 2, 1); !called) {
        return std::unexpected(std::move(called.error()));
    }
    return stack.ref(func);
}

Ref Ref::get(std::string_view key) const {
    return index(key).value_or(Ref{});
}

std::expected<void, std::string> Ref::set_from_top(std::string_view key) const {
    State& state = *this->state();
    const Value value = state.get(state.top - 1);
    --state.top;
    ScopedStack stack{state};
    state.push(Value::c_function(&protected_assign));
    const int func = stack.top();
    push_to(state);
    stack.push_string(key);
    state.push(value);
    return stack.call(func, 3, 0);
}

// ==== Classes ====

namespace detail {

ClassBase::ClassBase(Heap& heap, std::string_view name, const void* tag) : heap_(&heap), tag_(tag) {
    // The class table (also the methods' table), the global `name`.
    const Table* cls = heap.new_table();
    heap.globals().set(heap, string_value(heap, name), Value::table(cls));
    class_tables(heap).set(heap, Value::light_userdata(tag), Value::table(cls));
    // The objects' metatable, found by the class's tag.
    Table* mt = heap.new_table();
    heap.references().set(heap, Value::light_userdata(tag), Value::table(mt));
    mt->set(heap, string_value(heap, "__name"), string_value(heap, name));
    mt->set(heap, string_value(heap, "__index"), Value::table(cls));
    // The metatable is in the references, so the collector keeps it, and
    // the class table through it.
}

namespace {

Table* metatable_of_class(Heap& heap, const void* tag) {
    return class_metatable(heap, tag);
}

// The class table, where the class's functions and methods are.
Table* class_table(Heap& heap, const void* tag) {
    return std::bit_cast<Table*>(
        class_tables(heap).get(Value::light_userdata(tag)).value().payload());
}

// One of the closures accessors() installs, or nullptr.
CClosure* own_closure(Value v, CFunction function) {
    if (!v.is_c_closure()) {
        return nullptr;
    }
    auto* closure = std::bit_cast<CClosure*>(v.payload());
    return closure->function == function ? closure : nullptr;
}

// Switches the metatable to the __index and __newindex functions that know
// properties and fields (once): they look in the class table first, and
// keep an __index or __newindex the class was given (meta) as the last
// resort. Returns the __newindex closure.
CClosure* accessors(Heap& heap, Table* mt, const void* tag) {
    const Value index_key = string_value(heap, "__index");
    const Value newindex_key = string_value(heap, "__newindex");
    const Value index = mt->get(index_key).value();
    const Value newindex = mt->get(newindex_key).value();
    if (CClosure* put = own_closure(newindex, &class_newindex);
        put != nullptr && own_closure(index, &class_index) != nullptr) {
        return put;
    }
    const Table* cls = class_table(heap, tag);
    const Table* getters = heap.new_table();
    const Table* setters = heap.new_table();
    CClosure* get = heap.new_c_closure(&class_index, 3);
    get->upvalues()[0] = Value::table(cls);
    get->upvalues()[1] = Value::table(getters);
    // A different __index, given by meta, stays as the fallback.
    get->upvalues()[2] = index.is_table() && index.as_table() == cls ? Value::nil() : index;
    CClosure* put = heap.new_c_closure(&class_newindex, 4);
    put->upvalues()[0] = Value::table(setters);
    put->upvalues()[1] = Value::table(getters);
    put->upvalues()[2] = Value::boolean(false);
    put->upvalues()[3] = newindex;
    mt->set(heap, index_key, Value::c_closure(get));
    mt->set(heap, newindex_key, Value::c_closure(put));
    return put;
}

} // namespace

void ClassBase::add_function(std::string_view name, Function f) {
    class_table(*heap_, tag_)
        ->set(*heap_, string_value(*heap_, name), make_bound(*heap_, std::move(f)));
}

void ClassBase::add_getter(std::string_view name, Function f) {
    CClosure* put = accessors(*heap_, metatable_of_class(*heap_, tag_), tag_);
    auto* getters = std::bit_cast<Table*>(put->upvalues()[1].payload());
    getters->set(*heap_, string_value(*heap_, name), make_bound(*heap_, std::move(f)));
}

void ClassBase::add_setter(std::string_view name, Function f) {
    CClosure* put = accessors(*heap_, metatable_of_class(*heap_, tag_), tag_);
    auto* setters = std::bit_cast<Table*>(put->upvalues()[0].payload());
    setters->set(*heap_, string_value(*heap_, name), make_bound(*heap_, std::move(f)));
}

void ClassBase::add_meta(std::string_view name, Function f) {
    Table* mt = metatable_of_class(*heap_, tag_);
    const Value function = make_bound(*heap_, std::move(f));
    // With properties, the class's __index and __newindex are the ones
    // that know them: a new one becomes their last resort instead.
    if (name == "__index" || name == "__newindex") {
        const Value current = mt->get(string_value(*heap_, name)).value();
        CClosure* own = own_closure(current, name == "__index" ? &class_index : &class_newindex);
        if (own != nullptr) {
            own->upvalues()[name == "__index" ? 2 : 3] = function;
            heap_->barrier(own, function);
            return;
        }
    }
    mt->set(*heap_, string_value(*heap_, name), function);
}

void ClassBase::allow_fields() {
    CClosure* put = accessors(*heap_, metatable_of_class(*heap_, tag_), tag_);
    put->upvalues()[2] = Value::boolean(true);
}

} // namespace detail

// ==== Interpreter ====

Interpreter::Interpreter() : heap_(std::make_unique<Heap>()) {
    open_libraries(*heap_);
}

Interpreter::Interpreter(Interpreter&&) noexcept = default;
Interpreter& Interpreter::operator=(Interpreter&&) noexcept = default;
Interpreter::~Interpreter() = default;

std::expected<std::vector<std::string>, std::string>
Interpreter::run(std::string_view source, std::string_view chunk_name, bool traceback) {
    return run_text(heap_->main_thread(), source, chunk_name,
                    traceback ? ErrorReport::Traceback : ErrorReport::Message);
}

Ref Interpreter::get(std::string_view name) {
    return globals().get(name);
}

Ref Interpreter::globals() {
    State& state = *heap_->running_thread();
    static_cast<void>(state.check_stack(1));
    state.push(Value::table(&heap_->globals()));
    return Ref::from_top(state);
}

Ref Interpreter::new_table() {
    State& state = *heap_->running_thread();
    static_cast<void>(state.check_stack(1));
    state.push(Value::table(heap_->new_table()));
    return Ref::from_top(state);
}

std::expected<void, std::string> Interpreter::set_global_function(std::string_view name,
                                                                  Function f) {
    // Through _G's __newindex, as `name = f` in Lua would.
    const Ref g = globals();
    State& state = *g.state();
    static_cast<void>(state.check_stack(1));
    state.push(make_bound(*heap_, std::move(f)));
    return g.set_from_top(name);
}

Ref Interpreter::make_function(Function f) {
    State& state = *heap_->running_thread();
    static_cast<void>(state.check_stack(1));
    state.push(make_bound(*heap_, std::move(f)));
    return Ref::from_top(state);
}

void Interpreter::set_output(std::function<void(std::string_view)> output) {
    heap_->set_output(std::move(output));
}

void Interpreter::set_error_output(std::function<void(std::string_view)> output) {
    heap_->set_error_output(std::move(output));
}

void Interpreter::collect() {
    heap_->collect();
}

void Interpreter::set_generational(bool generational) {
    heap_->set_mode(generational ? GcMode::Generational : GcMode::Incremental);
}

std::size_t Interpreter::memory_in_use() const {
    return heap_->total_bytes();
}

void Interpreter::set_memory_limit(std::size_t bytes) {
    heap_->set_memory_limit(bytes);
}

void Interpreter::request_interrupt() {
    cpplua::request_interrupt(heap_->main_thread());
}

State& Interpreter::main_thread() {
    return heap_->main_thread();
}

} // namespace cpplua
