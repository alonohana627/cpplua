# How to put C++ functions and classes into Lua

This page explains how C++ and Lua talk to each other in lua2: how Lua calls C++ functions, how
C++ calls Lua functions, how values cross between them, and how a whole C++ class (a `Car`)
becomes a Lua type with methods, properties, operators, callbacks and garbage collection.

Everything here is lua2's **public API**: one header, `#include <lua2/interpreter.hpp>`, and the
`lua2::lua2` library. Nothing from lua2's internals is needed. Every piece of code on this page
is compiled and run by `tests/embedding_test.cpp`, which is built into its own test program
(`lua2_api_tests`) that cannot even see the internal headers. If you change an example, change
the test too.

## Contents

1. [The big picture](#1-the-big-picture)
2. [Starting an interpreter and running code](#2-starting-an-interpreter-and-running-code)
3. [C++ functions for Lua](#3-c-functions-for-lua)
4. [How values convert](#4-how-values-convert)
5. [Values and calls from C++](#5-values-and-calls-from-c)
6. [A C++ class as a Lua type: the Car](#6-a-c-class-as-a-lua-type-the-car)
7. [Raw functions and the stack](#7-raw-functions-and-the-stack)
8. [How it works inside](#8-how-it-works-inside)
9. [Rules](#9-rules)
10. [API reference](#10-api-reference)

---

## 1. The big picture

| You want | You write | Section |
| -------- | --------- | ------- |
| Lua to call a C++ function | `lua.set_function("add", [](double a, double b) { return a + b; });` | 3 |
| Lua to use a C++ class | `lua.new_class<Car>("Car").constructor<...>().method(...).property(...)` | 6 |
| C++ to read or set a Lua global | `lua.get("x").as<int>()`, `lua.set("x", 42)` | 5 |
| C++ to call a Lua function | `lua.call<double>("f", 1, 2)` | 5 |
| C++ to keep a Lua value (a callback) | a `lua2::Ref` | 5 |
| full control of arguments and results | a raw function `int f(lua2::Context&)` | 7 |

Arguments and results are converted automatically between C++ and Lua types (section 4). A wrong
argument from Lua is a Lua error such as `bad argument #2 to 'add' (number expected, got string)`,
exactly as real Lua reports it, and a C++ exception thrown inside a bound function becomes a Lua
error too, so Lua code can catch everything with `pcall`.

## 2. Starting an interpreter and running code

```cpp
#include <lua2/interpreter.hpp>

lua2::Interpreter lua;   // memory, collector, globals, the base library, debug.traceback

auto results = lua.run("return 1 + 1", "script.lua");
if (results) {
    // (*results)[0] == "2": each result as print would show it
} else {
    // results.error() is the message, such as "script.lua:1: attempt to ..."
}
```

- `run(source, chunk_name)` compiles and runs a chunk; pass `true` as a third argument to get a
  stack traceback after an error message, as `lua2 file.lua` prints it.
- `lua.set_output(function)` sends what `print`, `io.write` and `io.stdout` write somewhere
  else than stdout, and `lua.set_error_output(function)` does the same for `warn` and
  `io.stderr`.
- A new `Interpreter` has the whole standard library open (`string`, `table`, `math`, `io`,
  `os`, `require`, ...); `std/*.lua` documents every function. Lua modules load with `require`
  from `package.path`; C libraries (`.dll`, `.so`) do not: add C++ functions with this API.
- Destroying the `Interpreter` closes it like `lua_close`: pending `<close>` variables close,
  `__gc` finalizers run, and every C++ object Lua still holds is destroyed (section 6).

The examples below use a small wrapper from the test:

```cpp
// An interpreter with the examples' functions, and print captured.
struct Lua {
    std::string output; // before lua: finalizers may still print while it closes
    lua2::Interpreter lua;

    Lua() {
        lua.set_output([this](std::string_view text) { output += text; });
        lua.set_function("add", [](double a, double b) { return a + b; });
        lua.set_function("greet", [](const std::string& name) { return "Hello, " + name + "!"; });
        lua.set_function("divmod",
                         [](lua2::Context& ctx, std::int64_t a,
                            std::int64_t b) -> std::tuple<std::int64_t, std::int64_t> {
                             if (b == 0) {
                                 ctx.error("division by zero");
                                 return {};
                             }
                             return {a / b, a % b};
                         });
        bind_car(lua);
    }

    // Runs a chunk; its results as text joined with commas, or the error.
    std::string run(std::string_view source) {
        const auto results = lua.run(source, "script.lua");
        if (!results) {
            return results.error();
        }
        std::string out;
        for (const std::string& r : *results) {
            out += (out.empty() ? "" : ",") + r;
        }
        return out;
    }
};
```

## 3. C++ functions for Lua

`set_function(name, callable)` makes a global Lua function out of any C++ callable: a lambda, a
function pointer, a `std::function`. Its parameters are filled from Lua's arguments, converted
(section 4), and its result goes back to Lua (the three functions of the wrapper above):

```cpp
lua.set_function("add", [](double a, double b) { return a + b; });
lua.set_function("greet", [](const std::string& name) { return "Hello, " + name + "!"; });
lua.set_function("divmod",
                 [](lua2::Context& ctx, std::int64_t a,
                    std::int64_t b) -> std::tuple<std::int64_t, std::int64_t> {
                     if (b == 0) {
                         ctx.error("division by zero");
                         return {};
                     }
                     return {a / b, a % b};
                 });
```

```lua
print(add(2, 3))            --> 5.0
print(greet("Lua"))         --> Hello, Lua!
print(divmod(17, 5))        --> 3   2
divmod(1, 0)                --> error: script.lua:1: division by zero
add(1, "x")                 --> error: script.lua:1: bad argument #2 to 'add' (number expected, got string)
add(1)                      --> error: script.lua:1: bad argument #2 to 'add' (number expected, got no value)
print(pcall(divmod, 1, 0))  --> false   division by zero
```

What the parameters and results can be:

- **Results:** nothing (`void`), one value, or several with a `std::tuple`.
- **A `lua2::Context&` parameter**, anywhere in the list, takes no Lua argument: it gives the
  function access to the call (section 7). Its most common use is errors: `ctx.error(message)`
  raises `"file:line: message"` (the position of the Lua code that called), `ctx.arg_error(n,
  text)` raises `"bad argument #n to 'f' (text)"`, and `ctx.raise(message)` raises the message as
  it is. After raising, return anything: the error goes to Lua when the function returns.
- **A thrown C++ exception** is caught and becomes a Lua error with `what()` as its message.
- The function's name in messages (`'add'`) is the name the Lua code called it by, as in Lua.

Generic lambdas (with `auto` parameters) cannot be bound: the parameter types must be known.

## 4. How values convert

| C++ type | From Lua (parameters) | To Lua (results) |
| -------- | --------------------- | ---------------- |
| `bool` | any value: `nil` and `false` are false | boolean |
| `int`, `std::int64_t`, ... | an integer, or a float or numeric string with an integer value (out of range: an error) | integer |
| `double`, `float` | a number, or a numeric string | float |
| `std::string`, `const std::string&` | a string, or a number (as text) | string |
| `std::string_view` | the same (valid during the call) | string |
| `const char*`, a string literal | | string (`nullptr`: nil) |
| `std::nullptr_t` | | nil |
| `lua2::Ref` | any value, kept alive (section 5) | that value |
| `std::optional<T>` | `nil` or a missing argument: empty; else a T | nil when empty |
| `T&`, `const T&`, `T*` for a bound class T | the object (a `T*` also takes nil) | |
| `T` by value, a bound class | a copy of the object | a new object (a copy) |
| `std::tuple<A, B, ...>` | | several results |

The names in "bad argument" messages follow Lua's: `number expected`, `string expected`, and for
bound classes the class's Lua name, `Car expected`.

## 5. Values and calls from C++

Globals, from the test (`lua.lua` is the wrapper's `Interpreter`):

```cpp
lua.lua.set("max_speed", 200);
lua.lua.set("greeting", std::string{"hi"});
```

```lua
function double_it(x) return x * 2 end
result = max_speed / 4 .. greeting
config = {name = 'test', size = 3}
```

```cpp
lua.lua.call<double>("double_it", 21);           // 42.0
lua.lua.get("result").as<std::string>();         // "50.0hi"
const lua2::Ref config = lua.lua.get("config");
config.get<std::string>("name");                 // "test"
config.get<int>("size");                         // 3
config.set("size", 4);                           // config.size = 4, in Lua too
lua.lua.call("missing").error();                 // "attempt to call a nil value"
lua.lua.call<int>("error", "boom", 0).error();   // "boom"
```

- `call<R>(name, args...)` calls a global function in protected mode and gives a
  `std::expected<R, std::string>`: the first result converted to R (`void` for none), or the
  error message.
- `get(name)` gives a `lua2::Ref`; `as<T>()` converts it (an empty optional if it does not
  convert); for tables, `get<T>(key)` and `set(key, value)` (with `__index` and `__newindex`,
  protected); for functions, `call<R>(args...)`.

**Keeping Lua values in C++.** A `lua2::Ref` keeps its value alive for as long as the Ref exists,
so C++ can store Lua functions (callbacks), tables and objects. A typical event system:

```cpp
std::vector<lua2::Ref> handlers;
lua.lua.set_function("on_tick", [&handlers](lua2::Ref f) { handlers.push_back(std::move(f)); });
```

```lua
ticks = 0
on_tick(function(n) ticks = ticks + n end)
on_tick(function(n) print('tick', n) end)
```

```cpp
lua.lua.collect(); // the Refs keep both functions alive
for (int n = 1; n <= 2; ++n) {
    for (const lua2::Ref& handler : handlers) {
        REQUIRE(handler.call(n));
    }
}
// ticks == 3, and "tick 1", "tick 2" were printed
handlers.clear(); // before the interpreter goes
```

Copying a Ref makes another reference to the same value. **Every Ref must be destroyed before its
interpreter.**

## 6. A C++ class as a Lua type: the Car

The C++ class knows nothing about Lua (its `Counted` member only lets the test count Cars):

```cpp
class Car {
public:
    Car(std::string model, double fuel) : model_(std::move(model)), fuel_(fuel) {}

    // Speeds up by `delta` km/h, burning 0.1 litre per km/h. False when the
    // tank runs dry.
    bool accelerate(double delta) {
        const double needed = delta * 0.1;
        if (needed > fuel_) {
            speed_ += fuel_ / 0.1;
            fuel_ = 0;
            return false;
        }
        fuel_ -= needed;
        speed_ += delta;
        return true;
    }

    void brake() { speed_ = 0; }

    void refuel(double litres) { fuel_ += litres; }

    [[nodiscard]] const std::string& model() const { return model_; }

    [[nodiscard]] double speed() const { return speed_; }

    [[nodiscard]] double fuel() const { return fuel_; }

private:
    std::string model_;
    double speed_ = 0;
    double fuel_;
    Counted counted_; // (only for the tests)
};
```

The whole binding is one statement:

```cpp
void bind_car(lua2::Interpreter& lua) {
    lua.new_class<Car>("Car")
        .constructor<std::string, double>() // Car.new(model, fuel)
        .method("accelerate",
                [](lua2::Context& ctx, Car& car, double delta) {
                    if (!car.accelerate(delta)) { // out of fuel: call the car's on_empty
                        const lua2::Ref on_empty = ctx.field(1, "on_empty");
                        if (on_empty.is_function()) {
                            if (auto called = on_empty.call(ctx.ref(1)); !called) {
                                ctx.raise(called.error()); // the callback's error goes on up
                            }
                        }
                    }
                    return car.speed();
                })
        .method("brake", &Car::brake)
        .method("refuel",
                [](lua2::Context& ctx, Car& car, double litres) {
                    if (litres <= 0) {
                        ctx.arg_error(2, "litres must be positive");
                        return;
                    }
                    car.refuel(litres);
                })
        .property("model", &Car::model)
        .property("speed", &Car::speed)
        .property("fuel", &Car::fuel)
        .meta("__tostring",
              [](const Car& car) {
                  return std::format("Car({}, {} km/h, {} l)", car.model(), car.speed(),
                                     car.fuel());
              })
        .meta("__eq", [](const Car& a, const Car& b) { return a.model() == b.model(); })
        .fields(); // Lua code may add its own fields, such as on_empty
}
```

Line by line:

- **`new_class<Car>("Car")`** makes a Lua type named `Car`: a global table `Car` for its functions
  and methods, and a metatable for its objects (with `__name = "Car"`, so messages say `Car`).
- **`constructor<std::string, double>()`** adds `Car.new(model, fuel)`, which builds a C++ `Car`
  inside a new Lua object (pass a name to call it something else: `constructor<...>("make")`).
- **`method(name, f)`**: `f` is a member function (`&Car::brake`) or a callable whose first
  parameter is `Car&` (or `const Car&`); Lua calls it as `car:name(...)`, so the object is
  argument 1. `accelerate` also takes a `Context&` to call back into Lua and to raise errors.
- **`property(name, getter)`** makes `car.name` read the value: a member function without
  parameters, a data member pointer (`&Car::speed_` if it were public), or a callable taking
  `const Car&`. `property(name, getter, setter)` also allows `car.name = value`. Assigning a
  property without a setter is an error.
- **`meta(name, f)`** adds a metamethod: `__tostring`, `__eq`, `__lt`, `__le`, `__add` and the
  other operators, `__len`, `__concat`, `__call`, `__close`, `__gc`.
- **`fields()`** lets Lua code store its own fields in each object (`car.on_empty = function ...
  end`). Without it, assigning an unknown field is an error (`Car has no field 'x'`).
- **`function(name, f)`** (not used here) adds a plain function to the class table:
  `Car.name(...)`, for factories and helpers.

### What Lua sees

```lua
local car = Car.new("Tesla", 5)
car:accelerate(30)
print(car)                              --> Car(Tesla, 30 km/h, 2 l)
print(car.model, car.speed, car.fuel)   --> Tesla   30.0   2.0
car:brake()
print(car.speed, type(car))             --> 0.0     userdata
print(car == Car.new("Tesla", 1))       --> true    (__eq compares the models)

local beetle = Car.new("Beetle", 1)
beetle.on_empty = function(c) print("out of fuel at " .. c.speed) end
beetle:accelerate(5)
beetle:accelerate(20)                   --> out of fuel at 10.0
```

and the errors, all catchable with `pcall`:

```lua
car:refuel(-1)          --> bad argument #1 to 'refuel' (litres must be positive)
car.accelerate(5, 10)   --> bad argument #1 to 'accelerate' (Car expected, got number)
car.speed = 100         --> property 'speed' of Car is read-only
Car.new("Fiat")         --> bad argument #2 to 'new' (number expected, got no value)

local mini = Car.new("Mini", 0)
mini.on_empty = function() error("tow truck needed") end
print(pcall(mini.accelerate, mini, 10))   --> false   script.lua:3: tow truck needed
```

`car:refuel(-1)` says argument **#1** though `-1` is the second value passed: in a method call
Lua does not count `self` (as real Lua's `luaL_argerror`). The error raised in the Lua callback
went through the C++ method and reached `pcall` with its own position.

### Lifetime

A Lua `Car` object *contains* its C++ `Car` (a userdata, section 8). When nothing in Lua refers to
it any more, the collector destroys it, and the C++ destructor runs; when the interpreter closes,
every object left is destroyed. From the test:

```lua
for i = 1, 100 do Car.new('temp', 1) end
collectgarbage()           -- the 100 C++ Cars are destroyed (the test counts them)
kept = Car.new('kept', 1)  -- one more, kept by a global until the interpreter closes
```

Objects also cross from C++ to Lua: `lua.set("my_car", Car{"Volvo", 50})` copies the Car into a
new Lua object, and a C++ function returning a `Car` by value does the same. A C++ function that
takes `Car&` works on the object Lua has, without copying.

## 7. Raw functions and the stack

A callable with exactly the signature `int(lua2::Context&)` is bound as it is: it reads its
arguments itself, pushes its results, and returns how many (or what an error function returns):

```cpp
int counter = 0;
// A raw function: it pushes its results itself and returns how many.
lua.lua.set_function("get_counter", [&counter](lua2::Context& ctx) {
    ctx.push_pointer(&counter); // a light userdata: the address, nothing more
    return 1;
});
lua.lua.set_function("bump", [](lua2::Context& ctx) {
    auto* c = static_cast<int*>(ctx.to_pointer(1));
    if (c == nullptr) {
        return ctx.type_error(1, "counter");
    }
    ++*c;
    return 0;
});
```

```lua
local c = get_counter()
bump(c) bump(c)        -- counter == 2 in C++
print(type(c))         --> userdata
bump(1)                --> bad argument #1 to 'bump' (counter expected, got number)
```

The `Context` (section 10) reads arguments by number (`arg_count`, `type`, `to_number`,
`to_integer`, `to_string`, `to_object<T>`, `to_pointer`, `ref`), pushes results (`push_nil`,
`push_boolean`, `push_integer`, `push_number`, `push_string`, `push_pointer`, `push(ref)`,
`push_object<T>(args...)`), indexes (`field`, `get_field`, `set_field`) and calls
(`call(func, args, results)`), like Lua's C API but with C++ types. A **light userdata**
(`push_pointer`) is a bare C++ address as a Lua value: Lua can store and pass it back, but it has
no metatable and Lua never frees it.

## 8. How it works inside

- **Userdata.** An object of a bound class is a Lua *userdata* (`type(car) == "userdata"`): one
  block of memory the collector manages, holding the C++ object itself, plus a metatable and a
  slot for the object's own Lua fields. The block also records which C++ class it holds, so
  `Car&` parameters check it in one comparison, and the destructor to run when it is freed (after
  any `__gc` finalizer).
- **Bound functions** are Lua *C++ closures*: a function value whose upvalue is a userdata holding
  the `std::function`. One trampoline makes the `Context`, calls it, and turns C++ exceptions into
  Lua errors.
- **Methods and properties.** Without properties or fields, the objects' `__index` is the class
  table, so a method call costs what a Lua method call through `__index` costs. With properties
  or `fields()`, `__index` is a C++ function that looks in the class table, then the getters,
  then the object's own fields.
- **Refs** are entries in the interpreter's table of references (Lua's `luaL_ref`), a root of the
  collector.
- The C++ side never touches the collector's objects directly: everything goes through the stack
  of the interpreter's main thread, as in Lua's C API, which is what keeps values alive while C++
  works with them.

## 9. Rules

1. **Refs die before their interpreter.** A `Ref` (and anything holding one, like the `handlers`
   vector) must be destroyed before the `Interpreter`.
2. **Destructors of bound classes must not use the interpreter**: they run while the collector is
   freeing objects.
3. **Bound classes need an alignment of 16 bytes or less** (anything but over-aligned types).
4. **One thread at a time.** An interpreter is not thread-safe; use one per thread, or a lock.
5. **Errors are Lua errors.** Inside a bound function, report problems with `ctx.error`,
   `ctx.arg_error` or a thrown exception; never let an exception escape in other ways (a
   destructor, a callback outside a bound function).
6. **A `std::string_view` argument** points into a Lua string that is alive during the call: copy
   it (`std::string`) to keep it.

## 10. API reference

**`lua2::Interpreter`** (`<lua2/interpreter.hpp>`)

| Member | Does |
| ------ | ---- |
| `run(source, name, traceback = false)` | runs a chunk: its results as text, or the error |
| `call<R>(name, args...)` | calls a global function: `std::expected<R, std::string>` |
| `get(name)` / `globals()` | a global / the globals table, as a `Ref` |
| `set(name, value)` | sets a global (any convertible value, or a bound class's object) |
| `set_function(name, f)` / `function(f)` | a C++ callable as a global / as a `Ref` |
| `new_table()` | an empty table, as a `Ref` |
| `new_class<T>(name)` | a `Class<T>` builder |
| `set_output(f)`, `set_error_output(f)`, `collect()`, `set_generational(b)`, `memory_in_use()` | output and collector |

**`lua2::Class<T>`**: `constructor<A...>(name = "new")`, `function(name, f)`, `method(name, f)`,
`property(name, getter[, setter])`, `meta(event, f)`, `fields()`; each returns the builder.

**`lua2::Ref`**: `type()`, `is_nil()`, `is_function()`, `as<T>()`, `get(key)`, `get<T>(key)`,
`set(key, value)`, `call<R>(args...)`; copyable and movable.

**`lua2::Context`** (in bound functions; `lua2::ScopedStack` gives one on top of the stack
outside a call):

| Group | Members |
| ----- | ------- |
| arguments | `arg_count()`, `is_none(n)`, `type(n)`, `type_name(n)`, `to_boolean(n)`, `to_number(n)`, `to_integer(n)`, `to_string(n)`, `to_object<T>(n)`, `to_pointer(n)`, `ref(n)` |
| results | `push_nil()`, `push_boolean(b)`, `push_integer(i)`, `push_number(d)`, `push_string(s)`, `push_pointer(p)`, `push(ref)`, `push_copy(n)`, `push_object<T>(args...)`, `top()`, `set_top(n)` |
| errors | `error(message)`, `raise(message)`, `arg_error(n, text)`, `type_error(n, expected)`, `failed()` |
| tables and calls | `field(t, key)`, `get_field(t, key)`, `set_field(t, key)`, `call(func, args, results)` |

**Free functions**: `lua2::bind(f)` turns a callable into a `lua2::Function`
(`std::function<int(Context&)>`), what `set_function` and the builder use; `lua2::Converter<T>`
can be specialized to teach the API a new C++ type (`get(ctx, n)`, `push(ctx, value)` and an
`expected` name).
