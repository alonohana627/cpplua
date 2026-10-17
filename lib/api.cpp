// The public C++ API (include/lua2/api.hpp, include/lua2/interpreter.hpp)
// on top of the internals: Context is a view of a thread's stack, Ref an
// entry in the heap's table of references, bound functions C++ closures
// whose upvalue is a userdata holding the std::function, and classes
// metatables found through their tag
// (docs/how_to_put_cpp_functions_and_classes_into_lua.md).

#include <exception>
#include <format>
#include <initializer_list>
#include <new>
#include <string>
#include <utility>

#include <lua2/api.hpp>
#include <lua2/interpreter.hpp>

#include "gc/heap.hpp"
#include "std/auxlib.hpp"
#include "std/base.hpp"
#include "value/convert.hpp"
#include "value/number.hpp"
#include "value/table.hpp"
#include "value/userdata.hpp"
#include "vm/execute.hpp"
#include "vm/function.hpp"
#include "vm/metamethods.hpp"
#include "vm/run.hpp"
#include "vm/state.hpp"

namespace lua2 {

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
    try {
        const int n = (*function)(ctx);
        return n < 0 || ctx.failed() ? c_function_error : n;
    } catch (const std::exception& e) {
        return raise_error(state, e.what());
    } catch (...) {
        return raise_error(state, "unknown C++ exception");
    }
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

// An object's own fields table (its user value), or nil.
Value object_fields(Value self) {
    if (!self.is_userdata() || self.as_userdata()->user_values().empty()) {
        return Value::nil();
    }
    return self.as_userdata()->user_values()[0];
}

// Calls `f(args...)` from a C++ function with one result, pushed.
bool call_pushing_one(State& state, std::initializer_list<Value> values) {
    const std::uint32_t func = state.top;
    for (const Value v : values) {
        state.push(v);
    }
    return call(state, func, 1) == Status::Ok;
}

// __index of objects with properties: methods (in the class table), then
// getters, then the object's own fields. Upvalues: the class table, the
// getters.
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
    push(state, fields.is_table() ? table_value(fields)->get(key).value() : Value::nil());
    return 1;
}

// __newindex: setters, else an error for read-only properties, else the
// object's own fields if the class allows them. Upvalues: the setters, the
// getters, whether fields are allowed.
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
    const std::uint32_t target = slot(n) + 1;
    while (state_->top < target) {
        state_->push(Value::nil());
    }
    state_->top = target;
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
    return lua2::to_integer(v);
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
    return v.as_userdata()->data();
}

Ref Context::ref(int n) const {
    state_->push(value_at(*state_, slot(n), is_none(n)));
    return Ref::from_top(*state_);
}

void Context::push_nil() {
    state_->push(Value::nil());
}

void Context::push_boolean(bool b) {
    state_->push(Value::boolean(b));
}

void Context::push_integer(std::int64_t i) {
    state_->push(Value::integer(i));
}

void Context::push_number(double d) {
    state_->push(Value::number(d));
}

void Context::push_string(std::string_view s) {
    state_->push(string_value(state_->heap(), s));
}

void Context::push_pointer(void* p) {
    state_->push(Value::light_userdata(p));
}

void Context::push(const Ref& r) {
    r.push_to(*state_);
}

void Context::push_copy(int n) {
    state_->push(value_at(*state_, slot(n), is_none(n)));
}

void* Context::push_userdata(const void* tag, std::size_t size) {
    Heap& heap = state_->heap();
    Table* mt = class_metatable(heap, tag);
    if (mt == nullptr) {
        error("this C++ class has no Lua class (Interpreter::new_class)");
        return nullptr;
    }
    Userdata* u = heap.new_userdata(size, 1); // one user value: the object's own fields
    u->type_tag = tag;
    state_->push(Value::userdata(u));
    heap.set_metatable(u, mt);
    return u->data();
}

void Context::set_destructor(void (*destructor)(void*)) {
    state_->get(state_->top - 1).as_userdata()->destructor = destructor;
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
    return lua2::arg_error(*state_, n, extra);
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
    Value result;
    if (index_value(*state_, state_->get(slot(t)), string_value(state_->heap(), key), result) !=
        Status::Ok) {
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
    const Value value = state_->get(state_->top - 1);
    if (assign_value(*state_, state_->get(slot(t)), string_value(state_->heap(), key), value) !=
        Status::Ok) {
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
    state.push(heap_ == nullptr ? Value::nil() : heap_->referenced(key_));
}

State* Ref::state() const {
    return heap_ == nullptr ? nullptr : &heap_->main_thread();
}

Ref Ref::get(std::string_view key) const {
    if (heap_ == nullptr) {
        return {};
    }
    ScopedStack stack{*state()};
    stack.state().push(Value::c_function(&protected_index));
    const int func = stack.top();
    push_to(*state());
    stack.push_string(key);
    if (!stack.call(func, 2, 1)) {
        return {};
    }
    return stack.ref(func);
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

// The class table: __index when there are no properties, else the first
// upvalue of the __index closure.
Table* class_table(Heap& heap, Table* mt) {
    const Value index = mt->get(string_value(heap, "__index")).value();
    if (index.is_table()) {
        return std::bit_cast<Table*>(index.payload());
    }
    return std::bit_cast<Table*>(index.as_c_closure()->upvalues()[0].payload());
}

// Switches the metatable to the __index and __newindex functions that know
// properties and fields (once); returns the __newindex closure.
CClosure* accessors(Heap& heap, Table* mt) {
    const Value index = mt->get(string_value(heap, "__index")).value();
    if (index.is_c_closure()) {
        return std::bit_cast<CClosure*>(
            mt->get(string_value(heap, "__newindex")).value().payload());
    }
    const auto* cls = std::bit_cast<const Table*>(index.payload());
    const Table* getters = heap.new_table();
    const Table* setters = heap.new_table();
    CClosure* get = heap.new_c_closure(&class_index, 2);
    get->upvalues()[0] = Value::table(cls);
    get->upvalues()[1] = Value::table(getters);
    CClosure* put = heap.new_c_closure(&class_newindex, 3);
    put->upvalues()[0] = Value::table(setters);
    put->upvalues()[1] = Value::table(getters);
    put->upvalues()[2] = Value::boolean(false);
    mt->set(heap, string_value(heap, "__index"), Value::c_closure(get));
    mt->set(heap, string_value(heap, "__newindex"), Value::c_closure(put));
    return put;
}

} // namespace

void ClassBase::add_function(std::string_view name, Function f) {
    Table* mt = metatable_of_class(*heap_, tag_);
    class_table(*heap_, mt)
        ->set(*heap_, string_value(*heap_, name), make_bound(*heap_, std::move(f)));
}

void ClassBase::add_getter(std::string_view name, Function f) {
    CClosure* put = accessors(*heap_, metatable_of_class(*heap_, tag_));
    auto* getters = std::bit_cast<Table*>(put->upvalues()[1].payload());
    getters->set(*heap_, string_value(*heap_, name), make_bound(*heap_, std::move(f)));
}

void ClassBase::add_setter(std::string_view name, Function f) {
    CClosure* put = accessors(*heap_, metatable_of_class(*heap_, tag_));
    auto* setters = std::bit_cast<Table*>(put->upvalues()[0].payload());
    setters->set(*heap_, string_value(*heap_, name), make_bound(*heap_, std::move(f)));
}

void ClassBase::add_meta(std::string_view name, Function f) {
    Table* mt = metatable_of_class(*heap_, tag_);
    mt->set(*heap_, string_value(*heap_, name), make_bound(*heap_, std::move(f)));
}

void ClassBase::allow_fields() {
    CClosure* put = accessors(*heap_, metatable_of_class(*heap_, tag_));
    put->upvalues()[2] = Value::boolean(true);
}

} // namespace detail

// ==== Interpreter ====

Interpreter::Interpreter() : heap_(std::make_unique<Heap>()) {
    open_base(*heap_);
    open_debug(*heap_);
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
    State& state = heap_->main_thread();
    static_cast<void>(state.check_stack(1));
    state.push(Value::table(&heap_->globals()));
    return Ref::from_top(state);
}

Ref Interpreter::new_table() {
    State& state = heap_->main_thread();
    static_cast<void>(state.check_stack(1));
    state.push(Value::table(heap_->new_table()));
    return Ref::from_top(state);
}

void Interpreter::set_global_from_top(std::string_view name) {
    State& state = heap_->main_thread();
    const Value value = state.get(state.top - 1);
    --state.top;
    heap_->globals().set(*heap_, string_value(*heap_, name), value);
}

void Interpreter::set_global_function(std::string_view name, Function f) {
    heap_->globals().set(*heap_, string_value(*heap_, name), make_bound(*heap_, std::move(f)));
}

Ref Interpreter::make_function(Function f) {
    State& state = heap_->main_thread();
    static_cast<void>(state.check_stack(1));
    state.push(make_bound(*heap_, std::move(f)));
    return Ref::from_top(state);
}

void Interpreter::set_output(std::function<void(std::string_view)> output) {
    heap_->set_output(std::move(output));
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

State& Interpreter::main_thread() {
    return heap_->main_thread();
}

} // namespace lua2
