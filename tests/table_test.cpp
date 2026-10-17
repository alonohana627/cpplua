#include <cstdint>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <cpplua/interpreter.hpp>

#include "gc/heap.hpp"
#include "value/table.hpp"

// Tables (stage 6) against the rules of docs/spec/tables.md. Most behavior
// is covered by the my_lua programs (table_*.lua), whose output comes from
// the real Lua 5.4.9; these name the rules one by one, and test the Table
// class directly.

namespace {

// Runs a chunk named "input" and returns its results joined with commas, or
// the error message.
std::string run(std::string_view source) {
    cpplua::Interpreter interpreter;
    const auto results = interpreter.run(source, "input");
    if (!results) {
        return results.error();
    }
    std::string out;
    for (std::size_t n = 0; n < results->size(); ++n) {
        if (n > 0) {
            out += ',';
        }
        out += (*results)[n];
    }
    return out;
}

using cpplua::Value;

} // namespace

// ---- The Table class ----

TEST_CASE("T1.1 a table stores and removes entries of every key type", "[table]") {
    cpplua::Heap heap{1};
    cpplua::Table* t = heap.new_table();
    const Value s = Value::string(heap.new_string("name"));
    t->set(heap, Value::integer(1), Value::integer(10));
    t->set(heap, s, Value::integer(20));
    t->set(heap, Value::boolean(true), Value::integer(30));
    t->set(heap, Value::number(2.5), Value::integer(40));
    CHECK(t->get(Value::integer(1)).value().identical(Value::integer(10)));
    CHECK(t->get(s).value().identical(Value::integer(20)));
    CHECK(t->get(Value::boolean(true)).value().identical(Value::integer(30)));
    CHECK(t->get(Value::number(2.5)).value().identical(Value::integer(40)));
    CHECK(t->get(Value::integer(2)).value().is_nil());
    CHECK(t->get(Value::nil()).absent());
    t->set(heap, s, Value::nil());
    CHECK(t->get(s).value().is_nil());
}

TEST_CASE("T1.3 a float key with an integral value is the integer key", "[table]") {
    cpplua::Heap heap{1};
    cpplua::Table* t = heap.new_table();
    t->set(heap, Value::number(3.0), Value::integer(1));
    CHECK(t->get(Value::integer(3)).value().identical(Value::integer(1)));
    Value key;
    Value value;
    REQUIRE(t->next_entry(key, value) == cpplua::Table::NextResult::Found);
    CHECK(key.identical(Value::integer(3))); // stored as an integer
}

TEST_CASE("T1.4 nil and NaN cannot be keys", "[table]") {
    CHECK(std::string{cpplua::Table::check_key(Value::nil())} == "table index is nil");
    CHECK(std::string{cpplua::Table::check_key(Value::number(0.0 / 0.0))} == "table index is NaN");
    CHECK(cpplua::Table::check_key(Value::integer(1)) == nullptr);
    CHECK(run("local t = {} t[nil] = 1") == "input:1: table index is nil");
    CHECK(run("local t = {} t[0/0] = 1") == "input:1: table index is NaN");
    CHECK(run("local t = {} return t[nil], t[0/0]") == "nil,nil");
}

TEST_CASE("T6.2 the array part grows with sequences and the hash part with the rest", "[table]") {
    cpplua::Heap heap{1};
    cpplua::Table* t = heap.new_table();
    for (std::int64_t i = 1; i <= 100; ++i) {
        t->set_int(heap, i, Value::integer(i * i));
    }
    CHECK(t->real_array_size() >= 100);
    CHECK(t->nodes().empty());
    CHECK(t->length() == 100);
    for (std::int64_t i = 1; i <= 100; ++i) {
        CHECK(t->get_int(i).value().identical(Value::integer(i * i)));
    }
    // Sparse keys go to the hash part.
    cpplua::Table* sparse = heap.new_table();
    sparse->set_int(heap, 1000000, Value::integer(1));
    sparse->set_int(heap, -5, Value::integer(2));
    CHECK(sparse->real_array_size() == 0);
    CHECK(sparse->get_int(1000000).value().identical(Value::integer(1)));
    CHECK(sparse->get_int(-5).value().identical(Value::integer(2)));
}

TEST_CASE("T4.1 next visits every entry once, the array part first", "[table]") {
    cpplua::Heap heap{1};
    cpplua::Table* t = heap.new_table();
    for (std::int64_t i = 1; i <= 5; ++i) {
        t->set_int(heap, i, Value::integer(i));
    }
    t->set(heap, Value::string(heap.new_string("a")), Value::integer(6));
    t->set(heap, Value::boolean(false), Value::integer(7));
    Value key;
    Value value;
    std::int64_t sum = 0;
    int count = 0;
    while (t->next_entry(key, value) == cpplua::Table::NextResult::Found) {
        if (count < 5) {
            CHECK(key.identical(Value::integer(count + 1)));
        }
        sum += value.as_integer();
        ++count;
    }
    CHECK(count == 7);
    CHECK(sum == 28);
    Value absent = Value::string(heap.new_string("absent"));
    CHECK(t->next_entry(absent, value) == cpplua::Table::NextResult::InvalidKey);
}

// ---- In Lua ----

TEST_CASE("T1.2 tables are references, and each constructor makes a new one", "[table]") {
    CHECK(run("local a = {} local b = a b.x = 1 return a.x, a == b, {} == {}") == "1,true,false");
}

TEST_CASE("T1.5 indexing a non-table is an error with the variable's name", "[table]") {
    CHECK(run("local n = 5 return n.x") == "input:1: attempt to index a number value (local 'n')");
    CHECK(run("return undefined.x") ==
          "input:1: attempt to index a nil value (global 'undefined')");
    CHECK(run("local t = {} return t.a.b") == "input:1: attempt to index a nil value (field 'a')");
    CHECK(run("local t = {} t.a.b = 1") == "input:1: attempt to index a nil value (field 'a')");
}

TEST_CASE("T2.1 constructors take positional, named and computed fields", "[table]") {
    CHECK(run("local t = {10, 20, x = 1, ['y'] = 2, [3] = 30} return t[1], t[2], t[3], t.x, t.y") ==
          "10,20,30,1,2");
}

TEST_CASE("T2.2 a call or ... at the end of a constructor gives all its values", "[table]") {
    CHECK(run("local function f() return 1, 2, 3 end local t = {f()} return #t") == "3");
    CHECK(run("local function f() return 1, 2, 3 end local t = {f(), 0} return #t") == "2");
    CHECK(run("local function f(...) return {...} end return #f(1, 2, 3, 4)") == "4");
    CHECK(run("local function f() return 1, 2, 3 end local t = {(f())} return #t") == "1");
}

TEST_CASE("T2.4 a positional field stored later wins over a keyed one", "[table]") {
    CHECK(run("local t = {[1] = 'a', 'b'} return t[1]") == "b");
}

TEST_CASE("T3.1 the length of a sequence is its size", "[table]") {
    CHECK(run("return #{}, #{1, 2, 3}, #{n = 1}") == "0,3,0");
    CHECK(run("local t = {} for i = 1, 100 do t[i] = i end return #t") == "100");
    CHECK(run("local t = {1, 2, 3} t[#t] = nil return #t") == "2");
    CHECK(run("local t = {} for i = 1, 10 do t[i] = i end for i = 10, 1, -1 do t[i] = nil end "
              "return #t") == "0");
}

TEST_CASE("T3.3 # of anything but a table or a string is an error", "[table]") {
    CHECK(run("local b = true return #b") ==
          "input:1: attempt to get length of a boolean value (local 'b')");
}

TEST_CASE("T4.3 next with a key that is not in the table fails", "[table]") {
    CHECK(run("return pcall(next, {}, 'x')") == "false,invalid key to 'next'");
}

TEST_CASE("T4.4 pairs and ipairs", "[table]") {
    CHECK(run("local n = 0 for k, v in pairs({1, 2, a = 3}) do n = n + v end return n") == "6");
    CHECK(run("local s = '' for i, v in ipairs({'a', 'b', nil, 'd'}) do s = s .. i .. v end "
              "return s") == "1a2b");
    CHECK(run("local f, t, k = pairs({}) return f == next, k") == "true,nil");
}

TEST_CASE("T4.5 select counts and picks its arguments", "[table]") {
    CHECK(run("return select('#')") == "0");
    CHECK(run("return select('#', nil, nil)") == "2");
    CHECK(run("return select(2, 'a', 'b', 'c')") == "b,c");
    CHECK(run("return select(-1, 'a', 'b', 'c')") == "c");
    CHECK(run("return select(5, 'a')").empty());
    CHECK(run("return pcall(select, 0, 'a')") ==
          "false,bad argument #1 to 'select' (index out of range)");
}

TEST_CASE("T5.1 raw functions ignore metamethods", "[table]") {
    CHECK(run("local t = setmetatable({}, {__index = function() return 1 end, "
              "__newindex = function() end, __len = function() return 9 end}) "
              "rawset(t, 'x', 2) return t.y, rawget(t, 'y'), t.x, #t, rawlen(t), rawlen('abc')") ==
          "1,nil,2,9,0,3");
    CHECK(run("return rawequal({}, {}), rawequal('a', 'a')") == "false,true");
    CHECK(run("return pcall(rawget, nil, 1)") ==
          "false,bad argument #1 to 'rawget' (table expected, got nil)");
    CHECK(run("return pcall(rawlen, 5)") ==
          "false,bad argument #1 to 'rawlen' (table or string expected, got number)");
}

TEST_CASE("T6.1 unreachable tables are collected", "[table][gc]") {
    cpplua::Interpreter interpreter;
    REQUIRE(
        interpreter.run("for i = 1, 1000 do local t = {i, i * 2, name = 'x' .. i} end", "input"));
    interpreter.collect();
    const std::size_t before = interpreter.memory_in_use();
    REQUIRE(
        interpreter.run("for i = 1, 100000 do local t = {i, i * 2, name = 'x' .. i} end", "input"));
    interpreter.collect();
    CHECK(interpreter.memory_in_use() < before + (std::size_t{64} * 1024));
}
