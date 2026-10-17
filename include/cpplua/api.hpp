#pragma once

// The public C++ API for connecting C++ and Lua
// (docs/how_to_put_cpp_functions_and_classes_into_lua.md): what a C++
// function sees when Lua calls it (Context), Lua values that C++ keeps (Ref),
// automatic conversion between C++ and Lua values (Converter), and C++
// classes as Lua types (Class). Everything here works through the
// interpreter's stack, as Lua's own C API does, without exposing cpplua's
// internals.

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <cpplua/type.hpp>

namespace cpplua {

class State; // an interpreter's thread (internal)
class Heap;  // an interpreter (internal)
class Ref;
class Interpreter;

// What C++ functions return to raise an error (the error is already set).
inline constexpr int function_error = -1;

// The identity of a C++ class bound to Lua (Class<T>): the address of this
// variable, one per type.
template <class T>
inline constexpr char class_tag = 0;

// ==== Context: a C++ function's view of Lua ====
//
// When Lua calls a C++ function, the function gets a Context: its arguments,
// numbered from 1, and a place to push its results. A raw C++ function has
// the signature `int f(cpplua::Context&)` and returns how many results it
// pushed, or what error() returns. Functions with any other signature are
// converted automatically (Converter, below).
//
// Outside a call, a Context works on top of the stack (Interpreter::stack(),
// ScopedStack): what is pushed there can be read back with the same indices.
class Context {
public:
    explicit Context(State& state);

    // ---- Arguments ----

    // How many arguments the function got.
    [[nodiscard]] int arg_count() const;

    // Whether argument n was not given at all (Lua's LUA_TNONE).
    [[nodiscard]] bool is_none(int n) const;

    // The type of argument n (Nil when missing).
    [[nodiscard]] Type type(int n) const;

    // The type's name as messages show it: a table's or userdata's __name,
    // else type_name(type(n)), or "no value".
    [[nodiscard]] std::string type_name(int n) const;

    [[nodiscard]] bool to_boolean(int n) const; // false only for nil and false
    // A number, or a string that converts (V5); else nothing.
    [[nodiscard]] std::optional<double> to_number(int n) const;
    // An integer, or a float or string with an integer value; else nothing.
    [[nodiscard]] std::optional<std::int64_t> to_integer(int n) const;
    // A string, or a number (converted in place, as lua_tolstring does);
    // valid until the function returns. Else nothing.
    [[nodiscard]] std::optional<std::string_view> to_string(int n);

    // The C++ object of class T in argument n, or nullptr.
    template <class T>
    [[nodiscard]] T* to_object(int n) const {
        return static_cast<T*>(object_data(n, &class_tag<T>));
    }

    // A pointer given by push_pointer, or nullptr.
    [[nodiscard]] void* to_pointer(int n) const;
    // The value of argument n, kept alive for as long as the Ref lives.
    [[nodiscard]] Ref ref(int n) const;

    // ---- Results ----

    void push_nil();
    void push_boolean(bool b);
    void push_integer(std::int64_t i);
    void push_number(double d);
    void push_string(std::string_view s);
    void push_pointer(void* p); // a light userdata: a C++ pointer Lua can pass around
    void push(const Ref& r);
    void push_copy(int n); // argument (or stack value) n again
    // A new object of class T (registered with Interpreter::new_class), made
    // from `args`; nullptr after raising an error if the class is unknown.
    template <class T, class... A>
    T* push_object(A&&... args);

    // How many values are on this context's part of the stack (arguments and
    // what was pushed).
    [[nodiscard]] int top() const;
    void set_top(int n); // drops values above n (or adds nils)

    // ---- Errors ----
    //
    // Each raises an error and returns function_error, so a raw function goes
    // `return ctx.error("...")`. In an automatically converted function, call
    // it and return anything: the error is raised when the function returns.

    // "file:line: message", with the position of the Lua code calling.
    int error(std::string_view message);
    // The message as it is, without a position.
    int raise(std::string_view message);
    // "bad argument #n to 'f' (extra)", named as the caller called it.
    int arg_error(int n, std::string_view extra);
    // "bad argument #n to 'f' (expected expected, got TYPE)".
    int type_error(int n, std::string_view expected);

    // Whether this call raised an error.
    [[nodiscard]] bool failed() const { return failed_; }

    // ---- Calling and indexing ----

    // Calls the function at stack index `func` with the `args` values above
    // it, in protected mode; afterwards `results` results (or all of them,
    // for -1) are where the function was. The error message otherwise
    // (and the stack back to `func - 1` values).
    std::expected<void, std::string> call(int func, int args, int results);
    // t[key] with __index, pushed; false after raising an error.
    bool get_field(int t, std::string_view key);
    // t[key] with __index, as a Ref (nil after raising an error: check
    // failed()).
    [[nodiscard]] Ref field(int t, std::string_view key);
    // t[key] = (the value on top, popped), with __newindex; false after
    // raising an error.
    bool set_field(int t, std::string_view key);

    // ---- For the API's templates ----

    [[nodiscard]] State& state() const { return *state_; }

    void* object_data(int n, const void* tag) const;
    void* push_userdata(const void* tag, std::size_t size);
    void set_destructor(void (*destructor)(void*)); // for the userdata on top
    [[nodiscard]] std::string class_name(const void* tag) const;

protected:
    Context(State& state, std::uint32_t base) : state_(&state), base_(base) {}

private:
    [[nodiscard]] std::uint32_t slot(int n) const;

    State* state_;
    std::uint32_t base_; // the function's slot; argument 1 is the next
    bool failed_ = false;
};

// A Context on top of the stack, for C++ code outside any call from Lua;
// what it pushes goes away with it.
class ScopedStack : public Context {
public:
    explicit ScopedStack(State& state);
    ScopedStack(const ScopedStack&) = delete;
    ScopedStack& operator=(const ScopedStack&) = delete;
    ScopedStack(ScopedStack&&) = delete;
    ScopedStack& operator=(ScopedStack&&) = delete;
    ~ScopedStack();

private:
    std::uint32_t saved_top_;
};

// ==== Ref: a Lua value kept by C++ ====
//
// A Ref keeps its value alive (in the interpreter's table of references,
// Lua's luaL_ref) until the Ref is destroyed, so C++ can store Lua
// functions, tables and objects as long as it needs. Copying a Ref makes
// another reference to the same value. A Ref must not outlive its
// interpreter. An empty Ref (default) is nil.
class Ref {
public:
    Ref() = default;
    Ref(const Ref& other);
    Ref& operator=(const Ref& other);
    Ref(Ref&& other) noexcept;
    Ref& operator=(Ref&& other) noexcept;
    ~Ref();

    [[nodiscard]] Type type() const;

    [[nodiscard]] bool is_nil() const { return type() == Type::Nil; }

    [[nodiscard]] bool is_function() const { return type() == Type::Function; }

    // The value as a C++ type (Converter), or nothing if it does not
    // convert.
    template <class T>
    [[nodiscard]] std::optional<T> as() const;

    // For tables (and objects with __index / __newindex): t[key].
    [[nodiscard]] Ref get(std::string_view key) const;

    template <class T>
    [[nodiscard]] std::optional<T> get(std::string_view key) const {
        return get(key).template as<T>();
    }
    // t[key] = value; false (with the message) on an error.
    template <class V>
    std::expected<void, std::string> set(std::string_view key, V&& value) const;

    // For functions (and values with __call): calls it with `args` in
    // protected mode, and converts its first result to R (void for none).
    template <class R = void, class... A>
    std::expected<R, std::string> call(A&&... args) const;

    // ---- For the API ----

    // Makes a Ref of the value on top of the stack, and pops it.
    static Ref from_top(State& state);
    // t[key] = (the value on top, popped) in protected mode.
    [[nodiscard]] std::expected<void, std::string> set_from_top(std::string_view key) const;
    void push_to(State& state) const;
    [[nodiscard]] State* state() const;

private:
    Heap* heap_ = nullptr;
    std::int64_t key_ = 0;
};

// ==== Converter: C++ values to Lua and back ====
//
// Converter<T>::get(ctx, n) reads stack value n as a T (or nothing), and
// Converter<T>::push(ctx, v) pushes v. `expected` names the type for "bad
// argument" messages. Provided for bool, integers, floating point, strings,
// Ref, std::optional, std::nullptr_t (nil), and the classes registered with
// Interpreter::new_class (by value: a new object; as T& or T*: the object).

template <class T, class = void>
struct Converter; // the classes bound with Class<T> (below)

template <>
struct Converter<bool> {
    static constexpr std::string_view expected = "boolean";

    static std::optional<bool> get(Context& ctx, int n) { return ctx.to_boolean(n); }

    static void push(Context& ctx, bool v) { ctx.push_boolean(v); }
};

template <class T>
struct Converter<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
    static constexpr std::string_view expected = "number";

    static std::optional<T> get(Context& ctx, int n) {
        const auto i = ctx.to_integer(n);
        if (!i || !std::in_range<T>(*i)) {
            return std::nullopt;
        }
        return static_cast<T>(*i);
    }

    static void push(Context& ctx, T v) { ctx.push_integer(static_cast<std::int64_t>(v)); }
};

template <class T>
struct Converter<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static constexpr std::string_view expected = "number";

    static std::optional<T> get(Context& ctx, int n) {
        const auto d = ctx.to_number(n);
        return d ? std::optional<T>{static_cast<T>(*d)} : std::nullopt;
    }

    static void push(Context& ctx, T v) { ctx.push_number(static_cast<double>(v)); }
};

template <>
struct Converter<std::string> {
    static constexpr std::string_view expected = "string";

    static std::optional<std::string> get(Context& ctx, int n) {
        const auto s = ctx.to_string(n);
        return s ? std::optional<std::string>{std::string{*s}} : std::nullopt;
    }

    static void push(Context& ctx, const std::string& v) { ctx.push_string(v); }
};

template <>
struct Converter<std::string_view> {
    static constexpr std::string_view expected = "string";

    static std::optional<std::string_view> get(Context& ctx, int n) { return ctx.to_string(n); }

    static void push(Context& ctx, std::string_view v) { ctx.push_string(v); }
};

template <>
struct Converter<const char*> {
    static constexpr std::string_view expected = "string";

    static void push(Context& ctx, const char* v) {
        if (v == nullptr) {
            ctx.push_nil();
        } else {
            ctx.push_string(v);
        }
    }
};

template <>
struct Converter<std::nullptr_t> {
    static constexpr std::string_view expected = "nil";

    static void push(Context& ctx, std::nullptr_t /*nil*/) { ctx.push_nil(); }
};

template <>
struct Converter<Ref> {
    static constexpr std::string_view expected = "value";

    static std::optional<Ref> get(Context& ctx, int n) { return ctx.ref(n); }

    static void push(Context& ctx, const Ref& v) { ctx.push(v); }
};

template <class T>
struct Converter<std::optional<T>> {
    static constexpr std::string_view expected = Converter<T>::expected;

    static std::optional<std::optional<T>> get(Context& ctx, int n) {
        if (ctx.type(n) == Type::Nil) {
            return std::optional<T>{};
        }
        auto v = Converter<T>::get(ctx, n);
        if (!v) {
            return std::nullopt;
        }
        return std::optional<T>{std::move(*v)};
    }

    static void push(Context& ctx, const std::optional<T>& v) {
        if (v) {
            Converter<T>::push(ctx, *v);
        } else {
            ctx.push_nil();
        }
    }
};

// A class bound with Class<T>: by value, a copy of the object (and a new
// object when pushed).
template <class T, class>
struct Converter {
    static std::optional<T> get(Context& ctx, int n) {
        if (const T* object = ctx.to_object<T>(n)) {
            return *object;
        }
        return std::nullopt;
    }

    static void push(Context& ctx, const T& v) { ctx.push_object<T>(v); }

    static void push(Context& ctx, T&& v) { ctx.push_object<T>(std::move(v)); }
};

namespace detail {

template <class T>
using Bare = std::remove_cvref_t<T>;

// The C++ types that are Lua values rather than objects of a bound class.
template <class T>
concept LuaScalar =
    std::is_arithmetic_v<Bare<T>> || std::is_same_v<Bare<T>, std::string> ||
    std::is_same_v<Bare<T>, std::string_view> || std::is_same_v<Bare<T>, const char*> ||
    std::is_same_v<Bare<T>, Ref> || std::is_same_v<Bare<T>, std::nullptr_t>;

template <class T>
struct IsOptional : std::false_type {};

template <class T>
struct IsOptional<std::optional<T>> : std::true_type {};

template <class T>
struct IsTuple : std::false_type {};

template <class... T>
struct IsTuple<std::tuple<T...>> : std::true_type {};

// The name in "bad argument" messages for a parameter of type T.
template <class T>
std::string expected_name(Context& ctx) {
    using B = Bare<std::remove_pointer_t<Bare<T>>>;
    if constexpr (LuaScalar<B> || IsOptional<B>::value) {
        return std::string{Converter<B>::expected};
    } else {
        return ctx.class_name(&class_tag<B>);
    }
}

// One parameter of an automatically converted function: fetches its
// argument, or raises "bad argument". Context& takes no argument.
template <class P>
struct Arg {
    using B = Bare<P>;
    std::optional<B> value;

    bool fetch(Context& ctx, int& n) {
        value = Converter<B>::get(ctx, n);
        if (!value) {
            ctx.type_error(n, expected_name<B>(ctx));
            return false;
        }
        ++n;
        return true;
    }

    decltype(auto) get() {
        return std::move(*value); // NOLINT(bugprone-unchecked-optional-access): fetch() succeeded
    }
};

template <>
struct Arg<Context&> {
    Context* context = nullptr;

    bool fetch(Context& ctx, int& /*n*/) {
        context = &ctx;
        return true;
    }

    [[nodiscard]] Context& get() const { return *context; }
};

// Objects of bound classes by reference or pointer.
template <class P>
    requires(std::is_reference_v<P> && !LuaScalar<P> && !IsOptional<Bare<P>>::value &&
             !std::is_same_v<Bare<P>, Context>)
struct Arg<P> {
    using B = Bare<P>;
    B* object = nullptr;

    bool fetch(Context& ctx, int& n) {
        object = ctx.to_object<B>(n);
        if (object == nullptr) {
            ctx.type_error(n, ctx.class_name(&class_tag<B>));
            return false;
        }
        ++n;
        return true;
    }

    B& get() { return *object; }
};

template <class P>
    requires(std::is_pointer_v<P> && !std::is_same_v<Bare<P>, const char*>)
struct Arg<P> {
    using B = std::remove_cv_t<std::remove_pointer_t<P>>;
    B* object = nullptr;

    bool fetch(Context& ctx, int& n) {
        if (ctx.type(n) != Type::Nil) {
            object = ctx.to_object<B>(n);
            if (object == nullptr) {
                ctx.type_error(n, ctx.class_name(&class_tag<B>));
                return false;
            }
        }
        ++n;
        return true;
    }

    B* get() { return object; }
};

// Pushes a function's result(s); returns how many.
template <class R>
int push_result(Context& ctx, R&& r) {
    using B = Bare<R>;
    if constexpr (std::is_array_v<B>) { // a string literal
        ctx.push_string(std::string_view{static_cast<const char*>(r)});
        return 1;
    } else if constexpr (IsTuple<B>::value) {
        return std::apply(
            [&ctx](auto&&... items) {
                return (push_result(ctx, std::forward<decltype(items)>(items)) + ... + 0);
            },
            std::forward<R>(r));
    } else {
        Converter<B>::push(ctx, std::forward<R>(r));
        return 1;
    }
}

// The signature of a callable: its result and parameter types.
template <class F>
struct Signature : Signature<decltype(&F::operator())> {};

template <class R, class... P>
struct Signature<R (*)(P...)> {
    using Result = R;
    using Params = std::tuple<P...>;
};

template <class R, class... P>
struct Signature<R(P...)> : Signature<R (*)(P...)> {};

template <class C, class R, class... P>
struct Signature<R (C::*)(P...) const> : Signature<R (*)(P...)> {};

template <class C, class R, class... P>
struct Signature<R (C::*)(P...)> : Signature<R (*)(P...)> {};

template <class C, class R, class... P>
struct Signature<R (C::*)(P...) const noexcept> : Signature<R (*)(P...)> {};

template <class C, class R, class... P>
struct Signature<R (C::*)(P...) noexcept> : Signature<R (*)(P...)> {};

template <class F, class R, class... P, std::size_t... I>
int invoke(F& f, Context& ctx, std::index_sequence<I...> /*indices*/) {
    std::tuple<Arg<P>...> args;
    int n = 1;
    bool ok = true;
    ((ok = ok && std::get<I>(args).fetch(ctx, n)), ...);
    if (!ok) {
        return function_error;
    }
    if constexpr (std::is_void_v<R>) {
        std::invoke(f, std::get<I>(args).get()...);
        return ctx.failed() ? function_error : 0;
    } else {
        // NOLINTNEXTLINE(misc-const-correctness): forwarded below
        decltype(auto) r = std::invoke(f, std::get<I>(args).get()...);
        if (ctx.failed()) {
            return function_error;
        }
        return push_result(ctx, std::forward<decltype(r)>(r));
    }
}

template <class F, class R, class Params>
struct Invoker;

template <class F, class R, class... P>
struct Invoker<F, R, std::tuple<P...>> {
    static int run(F& f, Context& ctx) {
        return invoke<F, R, P...>(f, ctx, std::index_sequence_for<P...>{});
    }
};

// A member function pointer as a callable taking the object first (so the
// object is argument 1: `obj:method(...)`); other callables as they are.
template <class T, class R, class... P>
auto method_callable(R (T::*f)(P...)) {
    return [f](T& self, P... a) -> R { return (self.*f)(std::forward<P>(a)...); };
}

template <class T, class R, class... P>
auto method_callable(R (T::*f)(P...) const) {
    return [f](const T& self, P... a) -> R { return (self.*f)(std::forward<P>(a)...); };
}

template <class F>
    requires(!std::is_member_function_pointer_v<F>)
auto method_callable(F f) {
    return f;
}

} // namespace detail

// What the interpreter stores for a bound C++ function.
using Function = std::function<int(Context&)>;

// Wraps any callable (a function pointer, a lambda without `auto`
// parameters, a std::function) as a Function: its arguments are converted
// from Lua (a bad one is a "bad argument" error), its result to Lua (void:
// none; a std::tuple: several). A parameter of type Context& gets the
// context and takes no argument. A callable `int(Context&)` is used as it
// is: it pushes its results and returns how many.
template <class F>
Function bind(F f) {
    if constexpr (std::is_invocable_r_v<int, F&, Context&> &&
                  std::is_same_v<typename detail::Signature<std::decay_t<F>>::Params,
                                 std::tuple<Context&>>) {
        return Function{std::move(f)};
    } else {
        using Sig = detail::Signature<std::decay_t<F>>;
        return [f = std::move(f)](Context& ctx) mutable {
            return detail::Invoker<F, typename Sig::Result, typename Sig::Params>::run(f, ctx);
        };
    }
}

// ==== Template members of Context and Ref ====

template <class T, class... A>
T* Context::push_object(A&&... args) {
    static_assert(alignof(T) <= 16, "objects are aligned to 16 bytes");
    void* memory = push_userdata(&class_tag<T>, sizeof(T));
    if (memory == nullptr) {
        return nullptr;
    }
    // The object belongs to the userdata, which the collector owns.
    T* object = new (memory) T(std::forward<A>(args)...); // NOLINT(cppcoreguidelines-owning-memory)
    if constexpr (!std::is_trivially_destructible_v<T>) {
        set_destructor([](void* p) { static_cast<T*>(p)->~T(); });
    }
    return object;
}

template <class T>
std::optional<T> Ref::as() const {
    if (heap_ == nullptr) {
        if constexpr (detail::IsOptional<T>::value) {
            return T{};
        }
        return std::nullopt;
    }
    ScopedStack stack{*state()};
    push_to(*state());
    return Converter<T>::get(stack, stack.top());
}

template <class V>
std::expected<void, std::string> Ref::set(std::string_view key, V&& value) const {
    if (heap_ == nullptr) {
        return std::unexpected(std::string{"attempt to index a nil value"});
    }
    ScopedStack stack{*state()};
    detail::push_result(stack, std::forward<V>(value));
    return set_from_top(key);
}

template <class R, class... A>
std::expected<R, std::string> Ref::call(A&&... args) const {
    if (heap_ == nullptr) {
        return std::unexpected(std::string{"attempt to call a nil value"});
    }
    ScopedStack stack{*state()};
    push_to(*state());
    const int func = stack.top();
    (detail::push_result(stack, std::forward<A>(args)), ...);
    if (auto called = stack.call(func, static_cast<int>(sizeof...(A)), std::is_void_v<R> ? 0 : 1);
        !called) {
        return std::unexpected(std::move(called.error()));
    }
    if constexpr (std::is_void_v<R>) {
        return {};
    } else {
        auto r = Converter<R>::get(stack, func);
        if (!r) {
            return std::unexpected("the result is not a " + std::string{Converter<R>::expected});
        }
        return std::move(*r);
    }
}

// ==== Class: a C++ class as a Lua type ====

namespace detail {

// The parts of Class<T> that do not depend on T (lib/api.cpp).
class ClassBase {
public:
    ClassBase(Heap& heap, std::string_view name, const void* tag);

protected:
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes): for Class<T>
    void add_function(std::string_view name, Function f); // in the class table
    void add_getter(std::string_view name, Function f);
    void add_setter(std::string_view name, Function f);
    void add_meta(std::string_view name, Function f);
    void allow_fields();

    Heap* heap_;
    const void* tag_;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

} // namespace detail

// Builds a Lua type for C++ class T (Interpreter::new_class). The class is a
// global table of its functions and methods (`Car.new`, `Car.accelerate`);
// its objects are userdata that hold a T, with a metatable that finds the
// methods (`car:accelerate(10)`), properties (`car.speed`), and the
// metamethods given. When Lua drops an object, T's destructor runs.
template <class T>
class Class : detail::ClassBase {
public:
    Class(Heap& heap, std::string_view name) : ClassBase(heap, name, &class_tag<T>) {}

    // `name(args...)` makes a T from A... (default: Car.new(...)).
    template <class... A>
    Class& constructor(std::string_view name = "new") {
        add_function(name, [](Context& ctx) -> int {
            std::tuple<detail::Arg<A>...> args;
            int n = 1;
            bool ok = true;
            std::apply([&](auto&... a) { ((ok = ok && a.fetch(ctx, n)), ...); }, args);
            if (!ok) {
                return function_error;
            }
            std::apply([&](auto&... a) { ctx.push_object<T>(a.get()...); }, args);
            return ctx.failed() ? function_error : 1;
        });
        return *this;
    }

    // A function in the class table, called as `Car.name(...)`.
    template <class F>
    Class& function(std::string_view name, F f) {
        add_function(name, bind(std::move(f)));
        return *this;
    }

    // A method, called as `obj:name(...)`: a member function, or a callable
    // whose first parameter is T& (or const T&).
    template <class F>
    Class& method(std::string_view name, F f) {
        add_function(name, bind(detail::method_callable(f)));
        return *this;
    }

    // A property read as `obj.name`: a data member, a member function with
    // no parameters, or a callable taking T&. With a setter (a data member,
    // a member function or a callable taking T& and the value), it can be
    // assigned too; without, assigning it is an error.
    template <class G>
    Class& property(std::string_view name, G getter) {
        add_getter(name, getter_function(getter));
        return *this;
    }

    template <class G, class S>
    Class& property(std::string_view name, G getter, S setter) {
        add_getter(name, getter_function(getter));
        add_setter(name, setter_function(setter));
        return *this;
    }

    // A metamethod: "__tostring", "__eq", "__lt", "__le", "__add", "__len",
    // "__concat", "__call", "__close", "__gc", ... (a callable taking T& or
    // anything else, converted as for methods).
    template <class F>
    Class& meta(std::string_view event, F f) {
        add_meta(event, bind(detail::method_callable(f)));
        return *this;
    }

    // Lets Lua code store its own fields in each object (`obj.on_event =
    // function ... end`); without it, assigning an unknown field is an error.
    Class& fields() {
        allow_fields();
        return *this;
    }

private:
    template <class G>
    static Function getter_function(G getter) {
        if constexpr (std::is_member_object_pointer_v<G>) {
            return bind([getter](const T& self) { return self.*getter; });
        } else {
            return bind(detail::method_callable(getter));
        }
    }

    template <class S>
    static Function setter_function(S setter) {
        if constexpr (std::is_member_object_pointer_v<S>) {
            using V = std::remove_cvref_t<decltype(std::declval<T&>().*setter)>;
            return bind([setter](T& self, V value) { self.*setter = std::move(value); });
        } else {
            return bind(detail::method_callable(setter));
        }
    }
};

} // namespace cpplua
