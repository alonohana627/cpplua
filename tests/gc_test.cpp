#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "gc/heap.hpp"
#include "gc/object.hpp"
#include "gc/pool.hpp"
#include "value/string.hpp"
#include "vm/function.hpp"
#include "vm/run.hpp"
#include "vm/state.hpp"

// The garbage collector (stage 5, docs/spec/execution.md E9): a port of Lua
// 5.4's lgc.c with its two modes. Most of its testing is the my_lua programs
// run under GC stress ([my_lua][gc]); these tests check the collector's own
// promises directly.

using lua2::GcMode;
using lua2::GcStress;
using lua2::Heap;
using lua2::Value;

namespace {

constexpr std::uint32_t test_seed = 1;

// Runs a chunk and returns its first result as text, or the error.
std::string first_result(Heap& heap, std::string_view source) {
    const auto results = lua2::run(heap.main_thread(), source, "test");
    if (!results) {
        return results.error();
    }
    return results->empty() ? std::string{"(none)"} : lua2::display_text(results->front());
}

} // namespace

TEST_CASE("the pool hands out distinct 16-byte aligned blocks and reuses freed ones", "[gc]") {
    lua2::Pool pool;
    void* a = pool.allocate(24);
    void* b = pool.allocate(24);
    void* big = pool.allocate(5000); // past the pooled sizes: from operator new
    CHECK(a != b);
    CHECK(std::bit_cast<std::uintptr_t>(a) % 16 == 0);
    CHECK(std::bit_cast<std::uintptr_t>(b) % 16 == 0);
    pool.free(a, 24);
    pool.free(big, 5000);
    // A freed block of the same size class comes back (with ASan, operator
    // new decides).
    void* again = pool.allocate(32);
    CHECK(again != b);
    pool.free(again, 32);
    pool.free(b, 24);
}

TEST_CASE("E9.1 unreachable strings are collected, reachable ones are not", "[gc]") {
    Heap heap{test_seed};
    const std::size_t before = heap.string_count();
    for (int i = 0; i < 1000; ++i) {
        heap.new_string("garbage " + std::to_string(i)); // anchored nowhere
    }
    const lua2::String* kept = heap.new_string("kept on the stack");
    heap.main_thread().push(Value::string(kept));
    CHECK(heap.string_count() == before + 1001);

    heap.collect();
    CHECK(heap.string_count() == before + 1); // only the anchored one is left
    CHECK(kept->view() == "kept on the stack");

    // E9.3: the survivor has not moved, and interning finds it again.
    CHECK(heap.new_string("kept on the stack") == kept);

    heap.main_thread().top--; // drop it; now it dies too
    heap.collect();
    CHECK(heap.string_count() == before);
}

TEST_CASE("E9.1 the string table forgets dead strings and shrinks", "[gc]") {
    Heap heap{test_seed};
    for (int i = 0; i < 5000; ++i) {
        heap.new_string("s" + std::to_string(i));
    }
    CHECK(heap.short_string_count() >= 5000);
    heap.collect();
    CHECK(heap.short_string_count() == 0);
    // New strings still intern correctly after the table shrank.
    const lua2::String* a = heap.new_string("again");
    CHECK(heap.new_string("again") == a);
}

TEST_CASE("E9.1 closures, upvalues and protos die with the last reference", "[gc]") {
    Heap heap{test_seed};
    const auto baseline = heap.total_bytes();
    CHECK(first_result(heap, "local function f() local n = 0 return function() n = n + 1 "
                             "return n end end local g = f() g() return g()") == "2");
    heap.collect();
    // Everything the chunk made is garbage now; only the stack may have
    // grown.
    CHECK(heap.total_bytes() < baseline + 4096);
}

TEST_CASE("E9.2 fixed objects survive every collection", "[gc]") {
    Heap heap{test_seed};
    const lua2::String* fixed = heap.new_string("a fixed string");
    heap.fix(fixed);
    heap.collect();
    heap.collect();
    CHECK(fixed->view() == "a fixed string");
    CHECK(heap.new_string("a fixed string") == fixed);
}

TEST_CASE("E9.4 memory stays bounded in a loop that allocates", "[gc]") {
    for (const auto mode : {GcMode::Incremental, GcMode::Generational}) {
        Heap heap{test_seed};
        heap.set_mode(mode);
        CAPTURE(static_cast<int>(mode));
        // 200,000 strings and closures of garbage: tens of megabytes if
        // nothing were freed.
        CHECK(first_result(heap, "local n = 0 for i = 1, 200000 do local s = 'garbage ' .. i "
                                 "local f = function() return s end n = n + #f() end return n") ==
              "2688895");
        CHECK(heap.total_bytes() < std::size_t{4} * 1024 * 1024);
        CHECK(heap.cycles() > 0);
    }
}

TEST_CASE("E9.5 the collector switches modes at run time", "[gc]") {
    Heap heap{test_seed};
    CHECK(heap.mode() == GcMode::Incremental);
    heap.set_mode(GcMode::Generational);
    CHECK(heap.mode() == GcMode::Generational);
    CHECK(first_result(heap, "local s = '' for i = 1, 1000 do s = s .. i % 10 end return #s") ==
          "1000");
    heap.set_mode(GcMode::Incremental);
    CHECK(heap.mode() == GcMode::Incremental);
    CHECK(first_result(heap, "local s = '' for i = 1, 1000 do s = s .. i % 10 end return #s") ==
          "1000");
    heap.collect();
}

TEST_CASE("E9.5 Lua's default parameters", "[gc]") {
    Heap heap{test_seed};
    CHECK(heap.parameters().pause == 200);
    CHECK(heap.parameters().step_multiplier == 100);
    CHECK(heap.parameters().step_size == 13);
    CHECK(heap.parameters().minor_multiplier == 20);
    CHECK(heap.parameters().major_multiplier == 100);
}

TEST_CASE("E9.6 a stopped collector does not collect until restarted", "[gc]") {
    Heap heap{test_seed};
    heap.stop();
    CHECK_FALSE(heap.is_running());
    const std::size_t cycles = heap.cycles();
    CHECK(first_result(heap, "local n = 0 for i = 1, 20000 do n = n + #('x' .. i) end "
                             "return n") == "108894");
    CHECK(heap.cycles() == cycles);
    heap.restart();
    CHECK(heap.is_running());
    CHECK(first_result(heap, "local n = 0 for i = 1, 20000 do n = n + #('x' .. i) end "
                             "return n") == "108894");
    CHECK(heap.cycles() > cycles);
}

TEST_CASE("E9.2 stress modes collect at every check and change nothing", "[gc]") {
    for (const auto mode : {GcMode::Incremental, GcMode::Generational}) {
        for (const auto stress : {GcStress::Steps, GcStress::FullCycles}) {
            Heap heap{test_seed};
            heap.set_mode(mode);
            heap.set_stress(stress);
            CAPTURE(static_cast<int>(mode), static_cast<int>(stress));
            CHECK(first_result(heap, "local function cons(h, t) return function(w) if w then "
                                     "return h end return t end end local l for i = 1, 300 do "
                                     "l = cons('n' .. i, l) end local n = 0 while l do n = n + "
                                     "#l(true) l = l(false) end return n") == "1092");
            CHECK(heap.cycles() > 2);
        }
    }
}

TEST_CASE("E9.2 open upvalues follow the stack when it moves", "[gc]") {
    Heap heap{test_seed};
    heap.set_stress(GcStress::FullCycles);
    // A closure over each level of a deep recursion: the stack is
    // reallocated several times while the upvalues are open.
    CHECK(first_result(heap, "local function deep(n) local v = n local f = function() return v "
                             "end if n == 0 then return f() end local r = deep(n - 1) return r + "
                             "f() end return deep(2000)") == "2001000");
}
