// The examples of docs/how_to_put_cpp_functions_and_classes_into_lua.md,
// compiled and run, so the document cannot drift from the code: if you
// change one, change the other. This file uses only the public headers
// (include/cpplua): it is built into its own test program, without cpplua's
// internal headers on the include path.

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <cpplua/interpreter.hpp>

namespace {

// ==== The C++ class, which knows nothing about Lua ====

// Counts the Cars alive, for the tests (a member, so Car keeps its
// compiler-made copy and move).
struct Counted {
    inline static int alive = 0;

    Counted() { ++alive; }

    Counted(const Counted& /*other*/) { ++alive; }

    Counted(Counted&& /*other*/) noexcept { ++alive; }

    Counted& operator=(const Counted&) = default;
    Counted& operator=(Counted&&) = default;

    ~Counted() { --alive; }
};

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

// ==== The binding ====

void bind_car(cpplua::Interpreter& lua) {
    lua.new_class<Car>("Car")
        .constructor<std::string, double>() // Car.new(model, fuel)
        .method("accelerate",
                [](cpplua::Context& ctx, Car& car, double delta) {
                    if (!car.accelerate(delta)) { // out of fuel: call the car's on_empty
                        const cpplua::Ref on_empty = ctx.field(1, "on_empty");
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
                [](cpplua::Context& ctx, Car& car, double litres) {
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

// An interpreter with the examples' functions, and print captured.
struct Lua {
    std::string output; // before lua: finalizers may still print while it closes
    cpplua::Interpreter lua;

    Lua() {
        lua.set_output([this](std::string_view text) { output += text; });
        lua.set_function("add", [](double a, double b) { return a + b; });
        lua.set_function("greet", [](const std::string& name) { return "Hello, " + name + "!"; });
        lua.set_function("divmod",
                         [](cpplua::Context& ctx, std::int64_t a,
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

} // namespace

TEST_CASE("embedding: C++ functions", "[embedding]") {
    Lua lua;
    CHECK(lua.run("return add(2, 3)") == "5.0");
    CHECK(lua.run("return greet('Lua')") == "Hello, Lua!");
    CHECK(lua.run("return divmod(17, 5)") == "3,2");
    CHECK(lua.run("return divmod(1, 0)") == "script.lua:1: division by zero");
    CHECK(lua.run("return add(1, 'x')") ==
          "script.lua:1: bad argument #2 to 'add' (number expected, got string)");
    CHECK(lua.run("return add(1)") ==
          "script.lua:1: bad argument #2 to 'add' (number expected, got no value)");
    CHECK(lua.run("return pcall(divmod, 1, 0)") == "false,division by zero");
}

TEST_CASE("embedding: a C++ class as a Lua type", "[embedding]") {
    Lua lua;
    CHECK(lua.run(R"(
        local car = Car.new("Tesla", 5)
        car:accelerate(30)
        print(car)
        print(car.model, car.speed, car.fuel)
        car:brake()
        return car.speed, type(car), car == Car.new("Tesla", 1), car == Car.new("Fiat", 1)
    )") == "0.0,userdata,true,false");
    CHECK(lua.output == "Car(Tesla, 30 km/h, 2 l)\nTesla\t30.0\t2.0\n");
}

TEST_CASE("embedding: errors from bound classes", "[embedding]") {
    Lua lua;
    CHECK(lua.run("local car = Car.new('Fiat', 10) car:refuel(-1)") ==
          "script.lua:1: bad argument #1 to 'refuel' (litres must be positive)");
    CHECK(lua.run("local car = Car.new('Fiat', 10) car.accelerate(5, 10)") ==
          "script.lua:1: bad argument #1 to 'accelerate' (Car expected, got number)");
    CHECK(lua.run("local car = Car.new('Fiat', 10) car.speed = 100") ==
          "script.lua:1: property 'speed' of Car is read-only");
    CHECK(lua.run("return Car.new('Fiat')") ==
          "script.lua:1: bad argument #2 to 'new' (number expected, got no value)");
}

TEST_CASE("embedding: C++ calls back into Lua", "[embedding]") {
    Lua lua;
    CHECK(lua.run(R"(
        local car = Car.new("Beetle", 1)
        car.on_empty = function(c) print("out of fuel at " .. c.speed) end
        car:accelerate(5)
        car:accelerate(20)
        return car.fuel
    )") == "0.0");
    CHECK(lua.output == "out of fuel at 10.0\n");
    CHECK(lua.run(R"(
        local car = Car.new("Mini", 0)
        car.on_empty = function() error("tow truck needed") end
        return pcall(car.accelerate, car, 10)
    )") == "false,script.lua:3: tow truck needed");
}

TEST_CASE("embedding: values and calls from C++", "[embedding]") {
    Lua lua;
    lua.lua.set("max_speed", 200);
    lua.lua.set("greeting", std::string{"hi"});
    REQUIRE(lua.run("function double_it(x) return x * 2 end "
                    "result = max_speed / 4 .. greeting "
                    "config = {name = 'test', size = 3}")
                .empty());
    CHECK(lua.lua.call<double>("double_it", 21) == 42.0);
    CHECK(lua.lua.get("result").as<std::string>() == "50.0hi");
    const cpplua::Ref config = lua.lua.get("config");
    CHECK(config.get<std::string>("name") == "test");
    CHECK(config.get<int>("size") == 3);
    REQUIRE(config.set("size", 4));
    CHECK(lua.run("return config.size") == "4");
    CHECK_FALSE(lua.lua.call("missing").has_value());
    CHECK(lua.lua.call("missing").error() == "attempt to call a nil value");
    CHECK(lua.lua.call<int>("error", "boom", 0).error() == "boom");
    // A C++ object made in C++ and given to Lua.
    lua.lua.set("my_car", Car{"Volvo", 50});
    CHECK(lua.run("my_car:accelerate(10) return my_car.speed, my_car.model") == "10.0,Volvo");
}

TEST_CASE("embedding: C++ keeps Lua functions", "[embedding]") {
    Lua lua;
    std::vector<cpplua::Ref> handlers;
    lua.lua.set_function("on_tick",
                         [&handlers](cpplua::Ref f) { handlers.push_back(std::move(f)); });
    REQUIRE(lua.run("ticks = 0 on_tick(function(n) ticks = ticks + n end) "
                    "on_tick(function(n) print('tick', n) end)")
                .empty());
    lua.lua.collect(); // the Refs keep both functions alive
    for (int n = 1; n <= 2; ++n) {
        for (const cpplua::Ref& handler : handlers) {
            REQUIRE(handler.call(n));
        }
    }
    CHECK(lua.run("return ticks") == "3");
    CHECK(lua.output == "tick\t1\ntick\t2\n");
    handlers.clear(); // before the interpreter goes
}

TEST_CASE("embedding: Lua frees C++ objects", "[embedding]") {
    {
        Lua lua;
        const int before = Counted::alive;
        REQUIRE(lua.run("for i = 1, 100 do Car.new('temp', 1) end collectgarbage()").empty());
        CHECK(Counted::alive == before);
        REQUIRE(lua.run("kept = Car.new('kept', 1)").empty());
        CHECK(Counted::alive == before + 1);
    }
    CHECK(Counted::alive == 0); // closing the interpreter destroys the rest
}

TEST_CASE("embedding: raw functions and C++ pointers", "[embedding]") {
    Lua lua;
    int counter = 0;
    // A raw function: it pushes its results itself and returns how many.
    lua.lua.set_function("get_counter", [&counter](cpplua::Context& ctx) {
        ctx.push_pointer(&counter); // a light userdata: the address, nothing more
        return 1;
    });
    lua.lua.set_function("bump", [](cpplua::Context& ctx) {
        auto* c = static_cast<int*>(ctx.to_pointer(1));
        if (c == nullptr) {
            return ctx.type_error(1, "counter");
        }
        ++*c;
        return 0;
    });
    CHECK(lua.run("local c = get_counter() bump(c) bump(c) return type(c)") == "userdata");
    CHECK(counter == 2);
    CHECK(lua.run("bump(1)") ==
          "script.lua:1: bad argument #1 to 'bump' (counter expected, got number)");
}
