#include <cstdint>
#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "gc/heap.hpp"
#include "value/string.hpp"
#include "vm/opcodes.hpp"
#include "vm/proto.hpp"

using lua2::Instruction;
using lua2::OpCode;

// ---- Part 0: instructions and protos ----

TEST_CASE("the instruction format is Lua 5.4's", "[vm][opcodes]") {
    STATIC_REQUIRE(sizeof(Instruction) == 4);
    STATIC_REQUIRE(lua2::opcode_count == 83);
    STATIC_REQUIRE(lua2::pos_a == 7);
    STATIC_REQUIRE(lua2::pos_k == 15);
    STATIC_REQUIRE(lua2::pos_b == 16);
    STATIC_REQUIRE(lua2::pos_c == 24);
    STATIC_REQUIRE(lua2::max_bx == 131071);
    STATIC_REQUIRE(lua2::offset_sbx == 65535);
    STATIC_REQUIRE(lua2::offset_sj == 16777215);
    STATIC_REQUIRE(lua2::offset_sc == 127);
    // The opcode is the low 7 bits; ADD is opcode 34, as in Lua.
    STATIC_REQUIRE(static_cast<int>(OpCode::Add) == 34);
    STATIC_REQUIRE(static_cast<int>(OpCode::Closure) == 79);
    STATIC_REQUIRE(lua2::make_abc(OpCode::Add, 0, 0, 0) % 128 == 34);
}

TEST_CASE("ABC instructions keep their fields", "[vm][opcodes]") {
    constexpr Instruction i = lua2::make_abc(OpCode::Add, 1, 2, 3);
    STATIC_REQUIRE(lua2::get_op(i) == OpCode::Add);
    STATIC_REQUIRE(lua2::get_a(i) == 1);
    STATIC_REQUIRE(lua2::get_b(i) == 2);
    STATIC_REQUIRE(lua2::get_c(i) == 3);
    STATIC_REQUIRE_FALSE(lua2::get_k(i));

    constexpr Instruction full = lua2::make_abc(OpCode::SetField, 255, 255, 255, true);
    STATIC_REQUIRE(lua2::get_op(full) == OpCode::SetField);
    STATIC_REQUIRE(lua2::get_a(full) == 255);
    STATIC_REQUIRE(lua2::get_b(full) == 255);
    STATIC_REQUIRE(lua2::get_c(full) == 255);
    STATIC_REQUIRE(lua2::get_k(full));
}

TEST_CASE("signed B and C fields", "[vm][opcodes]") {
    // ADDI R1 R2 -5: sC is stored as C = -5 + 127.
    constexpr Instruction i = lua2::make_abc(OpCode::AddI, 1, 2, -5 + lua2::offset_sc);
    STATIC_REQUIRE(lua2::get_sc(i) == -5);
    STATIC_REQUIRE(lua2::get_c(i) == 122);
    constexpr Instruction lti = lua2::make_abc(OpCode::LtI, 0, 128 + lua2::offset_sc, 0);
    STATIC_REQUIRE(lua2::get_sb(lti) == 128);
}

TEST_CASE("ABx, AsBx, Ax and sJ instructions keep their fields", "[vm][opcodes]") {
    constexpr Instruction k = lua2::make_abx(OpCode::LoadK, 7, lua2::max_bx);
    STATIC_REQUIRE(lua2::get_op(k) == OpCode::LoadK);
    STATIC_REQUIRE(lua2::get_a(k) == 7);
    STATIC_REQUIRE(lua2::get_bx(k) == lua2::max_bx);

    STATIC_REQUIRE(lua2::get_sbx(lua2::make_asbx(OpCode::LoadI, 0, -65535)) == -65535);
    STATIC_REQUIRE(lua2::get_sbx(lua2::make_asbx(OpCode::LoadI, 0, 65536)) == 65536);
    STATIC_REQUIRE(lua2::get_sbx(lua2::make_asbx(OpCode::LoadI, 0, 0)) == 0);

    STATIC_REQUIRE(lua2::get_ax(lua2::make_ax(OpCode::ExtraArg, lua2::max_ax)) == lua2::max_ax);

    STATIC_REQUIRE(lua2::get_sj(lua2::make_sj(OpCode::Jmp, -16777215)) == -16777215);
    STATIC_REQUIRE(lua2::get_sj(lua2::make_sj(OpCode::Jmp, 16777216)) == 16777216);
    STATIC_REQUIRE(lua2::get_op(lua2::make_sj(OpCode::Jmp, -1)) == OpCode::Jmp);
}

TEST_CASE("fields can be patched without touching the others", "[vm][opcodes]") {
    constexpr Instruction i = lua2::make_abc(OpCode::Eq, 1, 2, 3, true);
    constexpr Instruction patched = lua2::set_c(lua2::set_a(i, 9), 200);
    STATIC_REQUIRE(lua2::get_op(patched) == OpCode::Eq);
    STATIC_REQUIRE(lua2::get_a(patched) == 9);
    STATIC_REQUIRE(lua2::get_b(patched) == 2);
    STATIC_REQUIRE(lua2::get_c(patched) == 200);
    STATIC_REQUIRE(lua2::get_k(patched));

    // Jumps are patched once their target is known.
    constexpr Instruction jump = lua2::set_sj(lua2::make_sj(OpCode::Jmp, 0), -3);
    STATIC_REQUIRE(lua2::get_sj(jump) == -3);
    constexpr Instruction loop = lua2::set_bx(lua2::make_abx(OpCode::ForLoop, 4, 0), 12);
    STATIC_REQUIRE(lua2::get_a(loop) == 4);
    STATIC_REQUIRE(lua2::get_bx(loop) == 12);
}

TEST_CASE("each opcode's description", "[vm][opcodes]") {
    STATIC_REQUIRE(lua2::op_info(OpCode::Move).name == "MOVE");
    STATIC_REQUIRE(lua2::op_info(OpCode::VarArgPrep).name == "VARARGPREP");
    STATIC_REQUIRE(lua2::op_info(OpCode::ExtraArg).name == "EXTRAARG");
    STATIC_REQUIRE(lua2::op_info(OpCode::Jmp).format == lua2::OpFormat::SJ);
    STATIC_REQUIRE(lua2::op_info(OpCode::LoadI).format == lua2::OpFormat::AsBx);
    STATIC_REQUIRE(lua2::op_info(OpCode::LoadK).format == lua2::OpFormat::ABx);
    STATIC_REQUIRE(lua2::op_info(OpCode::Eq).is_test);
    STATIC_REQUIRE_FALSE(lua2::op_info(OpCode::Eq).sets_a);
    STATIC_REQUIRE(lua2::op_info(OpCode::TestSet).sets_a);
    STATIC_REQUIRE(lua2::op_info(OpCode::Call).uses_top);
    STATIC_REQUIRE(lua2::op_info(OpCode::Call).sets_top);
    STATIC_REQUIRE(lua2::op_info(OpCode::MmBin).metamethod);
    for (const auto& info : lua2::op_infos) {
        CHECK(!info.name.empty());
    }
}

TEST_CASE("a proto lists like luac -l -l", "[vm][opcodes]") {
    lua2::Heap heap{1};
    // local x = 1; return x + 2, assembled by hand as the real Lua 5.4.9
    // compiles it; the expected text is what its luac -l -l -p prints, with the
    // addresses taken out.
    lua2::Proto proto;
    proto.source = heap.new_string("@test.lua");
    proto.is_vararg = true;
    proto.max_stack_size = 2;
    proto.code = {
        lua2::make_abc(OpCode::VarArgPrep, 0, 0, 0),
        lua2::make_asbx(OpCode::LoadI, 0, 1),
        lua2::make_abc(OpCode::AddI, 1, 0, 2 + lua2::offset_sc),
        lua2::make_abc(OpCode::MmBinI, 0, 2 + lua2::offset_sc, 6),
        lua2::make_abc(OpCode::Return, 1, 2, 1),
        lua2::make_abc(OpCode::Return, 1, 1, 1),
    };
    proto.lines = {1, 1, 1, 1, 1, 1};
    proto.locals = {
        {.name = heap.new_string("x"), .start_pc = 2, .end_pc = 6},
    };
    proto.upvalues = {{.name = heap.new_string("_ENV"), .in_stack = true, .index = 0}};

    CHECK(lua2::list_proto(proto) == "\n"
                                     "main <test.lua:0,0> (6 instructions)\n"
                                     "0+ params, 2 slots, 1 upvalue, 1 local, 0 constants, "
                                     "0 functions\n"
                                     "\t1\t[1]\tVARARGPREP\t0\n"
                                     "\t2\t[1]\tLOADI    \t0 1\n"
                                     "\t3\t[1]\tADDI     \t1 0 2\n"
                                     "\t4\t[1]\tMMBINI   \t0 2 6 0\t; __add\n"
                                     "\t5\t[1]\tRETURN   \t1 2 1\t; 1 out\n"
                                     "\t6\t[1]\tRETURN   \t1 1 1\t; 0 out\n"
                                     "constants (0):\n"
                                     "locals (1):\n"
                                     "\t0\tx\t3\t7\n"
                                     "upvalues (1):\n"
                                     "\t0\t_ENV\t1\t0\n");
}

TEST_CASE("listings show constants and jumps", "[vm][opcodes]") {
    lua2::Heap heap{1};
    lua2::Proto proto;
    proto.source = heap.new_string("=stdin");
    proto.line_defined = 3;
    proto.last_line_defined = 5;
    proto.parameter_count = 1;
    proto.constants = {
        lua2::Value::string(heap.new_string("a\n\x01")),
        lua2::Value::number(2.0),
        lua2::Value::integer(7),
    };
    proto.code = {
        lua2::make_abx(OpCode::LoadK, 1, 0),   lua2::make_abc(OpCode::EqK, 0, 1, 0, true),
        lua2::make_sj(OpCode::Jmp, 1),         lua2::make_abc(OpCode::AddK, 0, 0, 2),
        lua2::make_abx(OpCode::ForLoop, 2, 3),
    };
    const std::string listing = lua2::list_proto(proto);
    CHECK(
        listing.starts_with("\nfunction <stdin:3,5> (5 instructions)\n"
                            "1 param, 2 slots, 0 upvalues, 0 locals, 3 constants, 0 functions\n"));
    CHECK(listing.contains("\t1\t[-]\tLOADK    \t1 0\t; \"a\\n\\001\"\n"));
    CHECK(listing.contains("\t2\t[-]\tEQK      \t0 1 1\t; 2.0\n"));
    CHECK(listing.contains("\t3\t[-]\tJMP      \t1\t; to 5\n"));
    CHECK(listing.contains("\t4\t[-]\tADDK     \t0 0 2\t; 7\n"));
    CHECK(listing.contains("\t5\t[-]\tFORLOOP  \t2 3\t; to 3\n"));
    CHECK(listing.contains("constants (3):\n\t0\tS\t\"a\\n\\001\"\n\t1\tF\t2.0\n\t2\tI\t7\n"));
}
