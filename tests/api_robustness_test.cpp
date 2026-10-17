// Hostile and unlucky uses of the public API (include/cpplua) that must give
// an error, never undefined behaviour: out of memory, exceptions from host
// callbacks, interrupts. Like embedding_test.cpp, this file uses only the
// public headers.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cpplua/interpreter.hpp>

namespace {

// Allocates in every way Lua code can: strings, tables in both parts,
// closures, upvalues, coroutines, stack growth, concatenation, the string
// table, to-be-closed variables.
constexpr std::string_view allocating = R"(
    local t = {}
    for i = 1, 300 do
        t[i] = {i, tostring(i) .. "x", k = i, [i .. "k"] = {}}
        t[i].f = function() return i, t end
    end
    local function deep(n) if n == 0 then return 0 end return 1 + deep(n - 1) end
    local co = coroutine.wrap(function(...) coroutine.yield(...) return deep(200) end)
    co(table.unpack(t, 1, 100))
    co()
    local s = string.rep("ab", 100, ",") .. table.concat(t[1], "-", 1, 2)
    do local c <close> = setmetatable({}, {__close = function() s = s .. "!" end}) end
    local m = setmetatable({}, {__index = function(_, k) return k .. k end})
    return #t + #s + #m.abc
)";

} // namespace

TEST_CASE("API1 running out of memory is the error 'not enough memory'", "[api][memory]") {
    // Every limit from tiny to enough fails some allocation in a different
    // place: each must be caught by the pcall, leave the interpreter whole,
    // and let it run the same code once the limit is gone.
    const bool generational = GENERATE(false, true);
    CAPTURE(generational);
    cpplua::Interpreter lua;
    lua.set_generational(generational);
    lua.collect();
    const std::size_t base = lua.memory_in_use();
    const std::string program = "return pcall(load([==[" + std::string{allocating} + "]==]))";
    const std::string expected = lua.run(allocating, "=first").value().at(0);
    int failures = 0;
    for (std::size_t extra = 0; extra < 400'000; extra += 307) {
        CAPTURE(extra);
        lua.collect();
        lua.set_memory_limit(base + extra);
        const auto result = lua.run(program, "=oom");
        lua.set_memory_limit(0);
        if (!result) { // not even the outer chunk could be compiled or started
            CHECK(result.error() == "not enough memory");
            ++failures;
        } else if (result->at(0) == "false") {
            CHECK(result->at(1) == "not enough memory");
            ++failures;
        } else {
            CHECK(result->at(1) == expected);
        }
        const auto again = lua.run(allocating, "=again");
        REQUIRE(again.has_value());
        CHECK(again->at(0) == expected);
    }
    CHECK(failures > 5);
}

TEST_CASE("API1.1 out of memory outside pcall stops run() with the error", "[api][memory]") {
    cpplua::Interpreter lua;
    lua.collect();
    lua.set_memory_limit(lua.memory_in_use() + 4096);
    const auto result = lua.run(R"(local t = {} for i = 1, 1e6 do t[i] = {} end)", "=oom");
    lua.set_memory_limit(0);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == "not enough memory");
    CHECK(lua.run("return 1 + 1", "=after").value().at(0) == "2");
}

TEST_CASE("API2 an exception from the output callback is a Lua error", "[api]") {
    cpplua::Interpreter lua;
    lua.set_output([](std::string_view text) {
        if (text.contains("boom")) {
            throw std::runtime_error("output failed");
        }
    });
    // Many times, inside pcall and coroutines: nothing leaks a C call or a
    // frame (it once ended in "C stack overflow" for good).
    for (int i = 0; i < 300; ++i) {
        const auto result = lua.run(R"(
            local ok, e = pcall(print, "boom")
            local co = coroutine.wrap(function() return pcall(io.write, "boom") end)
            local ok2, e2 = co()
            return ok, e, ok2, e2
        )",
                                    "=cb");
        REQUIRE(result.has_value());
        CHECK(result->at(0) == "false");
        CHECK(result->at(1) == "output failed");
        CHECK(result->at(2) == "false");
        CHECK(result->at(3) == "output failed");
    }
    const auto result = lua.run("print('boom')", "=top");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == "output failed");
    CHECK(lua.run("return 2 * 21", "=after").value().at(0) == "42");
}

TEST_CASE("API3 an interrupt stops only its own interpreter", "[api]") {
    cpplua::Interpreter a;
    cpplua::Interpreter b;
    a.request_interrupt();
    CHECK(b.run("local n = 0 for i = 1, 1000 do n = n + i end return n", "=b").value().at(0) ==
          "500500");
    const auto stopped = a.run("local n = 0 for i = 1, 1000 do n = n + i end return n", "=a");
    REQUIRE_FALSE(stopped.has_value());
    CHECK(stopped.error().ends_with("interrupted!"));
    CHECK(a.run("return 'again'", "=a").value().at(0) == "again"); // taken back
}

TEST_CASE("API4 hooks in one interpreter neither slow nor break another's", "[api][hooks]") {
    // Each interpreter (each thread) has its own dispatch: one turning its
    // hooks on and off all the time never loses the other's hook events.
    constexpr std::string_view counting = R"(
        local events = 0
        debug.sethook(function() events = events + 1 end, "", 1)
        for i = 1, 2000 do local _ = i * 2 end
        debug.sethook()
        return events
    )";
    cpplua::Interpreter reference;
    const std::string expected = reference.run(counting, "=count").value().at(0);
    std::atomic<bool> done{false};
    std::thread toggler([&done] {
        cpplua::Interpreter other;
        while (!done.load()) {
            (void)other.run("debug.sethook(print, 'l') debug.sethook()", "=toggle");
        }
    });
    cpplua::Interpreter lua;
    bool all_same = true;
    for (int trial = 0; trial < 300; ++trial) {
        all_same = all_same && lua.run(counting, "=count").value().at(0) == expected;
    }
    done.store(true);
    toggler.join();
    CHECK(all_same);
}

TEST_CASE("API5 an error in __gc is a warning", "[api][gc]") {
    cpplua::Interpreter lua;
    std::string warnings;
    lua.set_error_output([&warnings](std::string_view text) { warnings += text; });
    REQUIRE(lua.run(R"(
        warn("@on")
        setmetatable({}, {__gc = function() error("in gc", 0) end})
        setmetatable({}, {__gc = function() error({}) end})
        collectgarbage()
    )",
                    "=gc"));
    CHECK(warnings.contains("Lua warning: error in __gc (in gc)\n"));
    CHECK(warnings.contains("Lua warning: error in __gc (error object is not a string)\n"));
}

namespace {

struct Engine {
    int hp = 100;
};

struct Vehicle {
    Engine engine;
    int id = 7;

    [[nodiscard]] int get_id() const { return id; }
};

struct Truck : Vehicle {
    int load = 0;
};

struct Fragile {
    inline static int alive = 0;

    explicit Fragile(int n) {
        if (n < 0) {
            throw std::runtime_error("negative");
        }
        ++alive;
    }

    Fragile(const Fragile& /*other*/) { ++alive; }

    Fragile(Fragile&&) = delete;
    Fragile& operator=(const Fragile&) = delete;
    Fragile& operator=(Fragile&&) = delete;

    ~Fragile() { --alive; }
};

template <std::size_t... I>
std::expected<std::int64_t, std::string> call_with_many(cpplua::Interpreter& lua,
                                                        std::index_sequence<I...> /*i*/) {
    return lua.call<std::int64_t>("sum", static_cast<int>(I)...);
}

} // namespace

TEST_CASE("API6 pushing many values grows the stack", "[api]") {
    cpplua::Interpreter lua;
    REQUIRE(lua.set_function("many", [](cpplua::Context& ctx) {
        for (int i = 0; i < 2000; ++i) {
            ctx.push_integer(i);
        }
        return 2000;
    }));
    CHECK(lua.run("return select('#', many()), select(2000, many())", "=many").value() ==
          std::vector<std::string>{"2000", "1999"});
    REQUIRE(lua.run("function sum(...) local s = 0 for _, v in ipairs({...}) do s = s + v end "
                    "return s end",
                    "=sum"));
    CHECK(call_with_many(lua, std::make_index_sequence<60>{}).value() == 59 * 60 / 2);
}

TEST_CASE("API7 a raw function cannot return more than it pushed", "[api]") {
    cpplua::Interpreter lua;
    REQUIRE(lua.set_function("over", [](cpplua::Context& ctx) {
        ctx.push_integer(1);
        return 50;
    }));
    const auto result = lua.run("return pcall(over)", "=over");
    REQUIRE(result.has_value());
    CHECK(result->at(0) == "false");
    CHECK(result->at(1) == "C++ function returned 50 results but pushed only 1");
}

TEST_CASE("API8 a class's own __index and __newindex work with properties", "[api]") {
    cpplua::Interpreter lua;
    lua.new_class<Vehicle>("Vehicle")
        .constructor<>()
        .meta("__index", [](Vehicle& /*v*/, const std::string& key) { return "dyn:" + key; })
        .method("get_id", &Vehicle::get_id)
        .property("id", &Vehicle::id);
    lua.new_class<Engine>("Engine").property("hp", &Engine::hp, &Engine::hp);
    lua.new_class<Truck>("Truck")
        .constructor<>()
        .property("load", &Truck::load, &Truck::load)
        .meta("__newindex",
              [](Truck& t, const std::string& /*key*/, int value) { t.load = -value; })
        .method("get_id", &Truck::get_id); // inherited from Vehicle
    const auto result = lua.run(R"(
        local v = Vehicle.new()
        local t = Truck.new()
        t.load = 3
        t.anything = 4 -- the class's own __newindex
        return v:get_id(), v.id, v.other, t:get_id(), t.load
    )",
                                "=meta");
    REQUIRE(result.has_value());
    CHECK(*result == std::vector<std::string>{"7", "7", "dyn:other", "7", "-4"});
}

TEST_CASE("API9 a data member of a bound class is the object itself", "[api]") {
    cpplua::Interpreter lua;
    lua.new_class<Engine>("Engine").property("hp", &Engine::hp, &Engine::hp);
    lua.new_class<Vehicle>("Vehicle").constructor<>().property("engine", &Vehicle::engine);
    const auto result = lua.run(R"(
        local v = Vehicle.new()
        local e = v.engine
        e.hp = 250
        v = nil
        collectgarbage() -- e keeps the vehicle alive
        return e.hp, Vehicle.new().engine.hp
    )",
                                "=member");
    REQUIRE(result.has_value());
    CHECK(*result == std::vector<std::string>{"250", "100"});
}

TEST_CASE("API10 a Ref of another interpreter is refused", "[api]") {
    auto a = std::make_unique<cpplua::Interpreter>();
    cpplua::Interpreter b;
    const cpplua::Ref t = a->new_table();
    CHECK_THROWS_AS((void)b.set("t", t), std::invalid_argument);
    CHECK(b.run("return t", "=t").value().at(0) == "nil");
}

TEST_CASE("API11 a constructor that throws leaves no object behind", "[api]") {
    cpplua::Interpreter lua;
    lua.new_class<Fragile>("Fragile").constructor<int>();
    const auto result = lua.run("return pcall(Fragile.new, -1)", "=ctor");
    REQUIRE(result.has_value());
    CHECK(result->at(0) == "false");
    CHECK(result->at(1).ends_with("negative"));
    REQUIRE(lua.run("local f = Fragile.new(1)", "=ok"));
    lua.collect();
    lua.collect();
    CHECK(Fragile::alive == 0); // the finished one destroyed, the failed one never
}

TEST_CASE("API12 integer arguments and results convert with clear errors", "[api]") {
    cpplua::Interpreter lua;
    REQUIRE(lua.set_function("i32", [](std::int32_t x) { return x; }));
    REQUIRE(lua.set_function("u32", [](std::uint32_t x) { return x; }));
    REQUIRE(lua.set_function("big", [] { return std::numeric_limits<std::uint64_t>::max(); }));
    REQUIRE(lua.set_function("len", [](const char* s) { return std::string_view{s}.size(); }));
    const auto result = lua.run(R"(
        local function e(f, x) return select(2, pcall(f, x)) end
        return e(i32, 1.5), e(i32, 2^31), e(u32, -1), e(i32, "x"), big() > 0, len("abc")
    )",
                                "=ints");
    REQUIRE(result.has_value());
    CHECK(*result == std::vector<std::string>{
                         "bad argument #1 to 'i32' (number has no integer representation)",
                         "bad argument #1 to 'i32' (number out of range)",
                         "bad argument #1 to 'u32' (number out of range)",
                         "bad argument #1 to 'i32' (number expected, got string)", "true", "3"});
}

TEST_CASE("API13 globals set from C++ go through _G's __newindex", "[api]") {
    cpplua::Interpreter lua;
    REQUIRE(lua.run("setmetatable(_G, {__newindex = function(t, k, v) rawset(t, k, v * 2) end})",
                    "=mt"));
    REQUIRE(lua.set("x", 21));
    CHECK(lua.run("return x", "=x").value().at(0) == "42");
    REQUIRE(
        lua.run("setmetatable(_G, {__newindex = function() error('read-only', 0) end})", "=ro"));
    CHECK(lua.set("y", 1).error() == "read-only");
}

TEST_CASE("API14 Ref::index reports what get() turns into nil", "[api]") {
    cpplua::Interpreter lua;
    REQUIRE(lua.run("n = 5 t = setmetatable({}, {__index = function() error('no', 0) end})", "=t"));
    CHECK(lua.get("t").get("k").is_nil());
    CHECK(lua.get("t").index("k").error() == "no");
    CHECK(lua.get("n").index("k").error().contains("attempt to index a number value"));
}

TEST_CASE("API15 C++ called from a coroutine calls back in that coroutine", "[api]") {
    cpplua::Interpreter lua;
    REQUIRE(lua.set_function("back",
                             [&lua] { return lua.get("inside").call<bool>().value_or(false); }));
    const auto result = lua.run(R"(
        function inside() return coroutine.running() ~= main end
        main = coroutine.running()
        return coroutine.wrap(function() return back() end)()
    )",
                                "=co");
    REQUIRE(result.has_value());
    CHECK(result->at(0) == "true");
}

TEST_CASE("API16 move-only callables and stack indices", "[api]") {
    cpplua::Interpreter lua;
    auto owned = [p = std::make_unique<int>(41)] { return *p + 1; };
    const bool set = lua.set_function("owned", std::move(owned)).has_value();
    REQUIRE(set);
    CHECK(lua.run("return owned()", "=m").value().at(0) == "42");
    REQUIRE(lua.set_function("trim", [](cpplua::Context& ctx) {
        ctx.set_top(-2); // drops the last argument
        return ctx.top();
    }));
    REQUIRE(lua.set_function("bad_index", [](cpplua::Context& ctx) {
        // No value 99: indexing nil, an error, never a read past the stack.
        return ctx.get_field(99, "x") ? 1 : cpplua::function_error;
    }));
    const auto result = lua.run("return select('#', trim(1, 2, 3)), pcall(bad_index)", "=t");
    REQUIRE(result.has_value());
    CHECK(*result == std::vector<std::string>{"2", "false", "attempt to index a nil value"});
}

TEST_CASE("API17 an exception from a bound function is a Lua error pcall catches", "[api]") {
    cpplua::Interpreter lua;
    REQUIRE(lua.set_function("fails", [](int n) -> int {
        if (n > 0) {
            throw std::runtime_error("too big");
        }
        return n;
    }));
    const auto result = lua.run("return pcall(fails, 1)", "=throw");
    REQUIRE(result.has_value());
    // (no position: pcall, a C function, called it)
    CHECK(*result == std::vector<std::string>{"false", "too big"});
    CHECK(lua.run("return fails(0)", "=ok").value().at(0) == "0");
}
