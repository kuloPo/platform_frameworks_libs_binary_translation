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

#ifndef BERBERIS_ASSEMBLER_CODE_COST_H_
#define BERBERIS_ASSEMBLER_CODE_COST_H_

#include <cstdint>

namespace berberis {

class MachineCode;

// Compile-time metadata only. No counters are inserted into generated code.
enum class CodeCostFlow : uint8_t {
  kNext,
  kBranch,
  kJump,
  kExit,
  kConditionalExit,
  kUnknown,
};

struct CodeCostInsn {
  uint32_t begin;
  uint32_t end;
  uint16_t weight;
  CodeCostFlow flow;
};

struct CodeCostSummary {
  bool valid = false;
  uint32_t instructions = 0;
  uint32_t backedges = 0;
  uint64_t score = 0;
  uint64_t loop_score = 0;
};

// These are relative work scores, not cycle estimates. Unknown emission,
// calls, or multiple backedges make the model unavailable. Unreachable
// recovery code does not contribute. A simple loop is compared separately
// without assuming its trip count or any branch probabilities.
CodeCostSummary AnalyzeCodeCost(const MachineCode& code);

// Require a 10% margin in both the reachable body and the loop body, if any.
// Candidates must cover the same guest region; the caller verifies this.
bool PreferHeavyCode(const CodeCostSummary& heavy, const CodeCostSummary& lite);

}  // namespace berberis

#endif  // BERBERIS_ASSEMBLER_CODE_COST_H_
