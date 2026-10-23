/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "gtest/gtest.h"

#include <cstring>

#include "berberis/assembler/code_cost.h"
#include "berberis/assembler/machine_code.h"
#include "berberis/assembler/x86_64.h"

#if defined(__x86_64__)
namespace berberis {
namespace {

TEST(CodeCostTest, RecordingPreservesFinalBytesAndShortcuts) {
  auto emit = [](MachineCode* code) {
    x86_64::Assembler as(code);
    x86_64::Assembler::Label done;
    as.Movq(as.rax, int64_t{7});
    as.Addq(as.rax, 1);
    as.Shlq(as.rax, int8_t{1});
    as.Movq({.base = as.rbp, .disp = 16}, as.rax);
    as.Jcc(x86_64::Assembler::Condition::kEqual, done);
    as.Vmovaps(as.xmm0, as.xmm1);
    as.Movq(as.rcx, int64_t{0x123456789abcdef});
    as.Bind(&done);
    as.Jmp32(uintptr_t{0x1000});
    as.Finalize();
  };
  MachineCode plain;
  MachineCode recorded(true);
  emit(&plain);
  emit(&recorded);
  ASSERT_EQ(plain.install_size(), recorded.install_size());
  EXPECT_EQ(0, memcmp(plain.AddrAs<uint8_t>(0), recorded.AddrAs<uint8_t>(0), plain.install_size()));
  EXPECT_TRUE(plain.cost_insns().empty());
  auto cost = AnalyzeCodeCost(recorded);
  ASSERT_TRUE(cost.valid);
  EXPECT_EQ(cost.instructions, 8U);
}

TEST(CodeCostTest, ExcludesUnreachableRecoveryAndRejectsUnrecordedReachableBytes) {
  MachineCode cold(true);
  x86_64::Assembler as(&cold);
  as.Movq(as.rax, as.rcx);
  as.Jmp32(uintptr_t{0x1000});
  as.Byte(uint8_t{0x90});
  auto cost = AnalyzeCodeCost(cold);
  ASSERT_TRUE(cost.valid);
  EXPECT_EQ(cost.instructions, 2U);

  MachineCode gap(true);
  x86_64::Assembler broken(&gap);
  broken.Movq(broken.rax, broken.rcx);
  broken.Byte(uint8_t{0x90});
  broken.Jmp32(uintptr_t{0x1000});
  EXPECT_FALSE(AnalyzeCodeCost(gap).valid);
}

TEST(CodeCostTest, ComparesLoopBodyWithoutAssumingTripCount) {
  MachineCode code(true);
  x86_64::Assembler as(&code);
  x86_64::Assembler::Label loop;
  as.Movq(as.rax, int64_t{100});
  as.Bind(&loop);
  as.Addq(as.rcx, 1);
  as.Subq(as.rax, 1);
  as.Jcc(x86_64::Assembler::Condition::kNotEqual, loop);
  as.Jmp32(uintptr_t{0x1000});
  as.Finalize();
  auto cost = AnalyzeCodeCost(code);
  ASSERT_TRUE(cost.valid);
  EXPECT_EQ(cost.backedges, 1U);
  EXPECT_LT(cost.loop_score, cost.score);

  auto heavy = cost;
  heavy.score /= 2;
  // Cheaper setup cannot compensate for a more expensive loop body.
  heavy.loop_score++;
  EXPECT_FALSE(PreferHeavyCode(heavy, cost));
  heavy.loop_score = cost.loop_score / 2;
  EXPECT_TRUE(PreferHeavyCode(heavy, cost));
}

TEST(CodeCostTest, CallsAndComplexCyclesUseFallback) {
  MachineCode calls(true);
  x86_64::Assembler as(&calls);
  as.Call(reinterpret_cast<void*>(0x1000));
  as.Jmp32(uintptr_t{0x2000});
  EXPECT_FALSE(AnalyzeCodeCost(calls).valid);

  MachineCode loops(true);
  x86_64::Assembler nested(&loops);
  x86_64::Assembler::Label outer, inner;
  nested.Bind(&outer);
  nested.Movq(nested.rax, nested.rcx);
  nested.Bind(&inner);
  nested.Subq(nested.rax, 1);
  nested.Jcc(x86_64::Assembler::Condition::kNotEqual, inner);
  nested.Subq(nested.rcx, 1);
  nested.Jcc(x86_64::Assembler::Condition::kNotEqual, outer);
  nested.Jmp32(uintptr_t{0x1000});
  nested.Finalize();
  EXPECT_FALSE(AnalyzeCodeCost(loops).valid);
}

TEST(CodeCostTest, MemoryWorkMattersMoreThanCodeSize) {
  MachineCode reg(true), memory(true);
  x86_64::Assembler a(&reg), b(&memory);
  a.Movq(a.r8, int64_t{0x123456789abcdef});
  a.Jmp32(uintptr_t{0x1000});
  b.Addq({.base = b.rax}, 1);
  b.Jmp32(uintptr_t{0x1000});
  auto reg_cost = AnalyzeCodeCost(reg);
  auto mem_cost = AnalyzeCodeCost(memory);
  ASSERT_TRUE(reg_cost.valid);
  ASSERT_TRUE(mem_cost.valid);
  ASSERT_GT(reg.install_size(), memory.install_size());
  EXPECT_TRUE(PreferHeavyCode(reg_cost, mem_cost));
  EXPECT_FALSE(PreferHeavyCode(mem_cost, reg_cost));
}

}  // namespace
}  // namespace berberis
#endif
