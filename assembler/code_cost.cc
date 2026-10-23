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

#include "berberis/assembler/code_cost.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "berberis/assembler/machine_code.h"

namespace berberis {

CodeCostSummary AnalyzeCodeCost(const MachineCode& code) {
  const auto& insns = code.cost_insns();
  // Bound the auxiliary work even for unusually large generated regions.
  if (insns.empty() || insns.size() > 16384 || insns.front().begin != 0) {
    return {};
  }
  const size_t count = insns.size();
  std::vector<bool> visited(count, false);
  std::vector<size_t> pending{0};
  size_t loop_begin = count;
  size_t loop_end = count;
  CodeCostSummary result;

  auto add_target = [&](int64_t offset) {
    if (offset < 0 || offset >= code.install_size()) {
      return false;
    }
    auto it = std::lower_bound(
        insns.begin(), insns.end(), offset, [](const CodeCostInsn& insn, int64_t value) {
          return insn.begin < value;
        });
    if (it == insns.end() || it->begin != offset) {
      return false;
    }
    pending.push_back(it - insns.begin());
    return true;
  };

  while (!pending.empty()) {
    size_t index = pending.back();
    pending.pop_back();
    if (visited[index]) {
      continue;
    }
    visited[index] = true;
    const auto& insn = insns[index];
    if (insn.end <= insn.begin || insn.end > code.install_size() ||
        insn.flow == CodeCostFlow::kUnknown) {
      return {};
    }
    result.score += insn.weight;
    result.instructions++;

    if (insn.flow == CodeCostFlow::kBranch || insn.flow == CodeCostFlow::kJump) {
      // These records describe local, finalized relative jumps only.
      // External relocated jumps have their own flow classification.
      const uint8_t* bytes = code.AddrAs<uint8_t>(insn.begin);
      uint32_t size = insn.end - insn.begin;
      int64_t displacement;
      if (size == 2 && (bytes[0] == 0xeb || (bytes[0] & 0xf0) == 0x70)) {
        displacement = static_cast<int8_t>(bytes[1]);
      } else if ((size == 5 && bytes[0] == 0xe9) ||
                 (size == 6 && bytes[0] == 0x0f && (bytes[1] & 0xf0) == 0x80)) {
        int32_t value;
        memcpy(&value, bytes + size - 4, sizeof(value));
        displacement = value;
      } else {
        return {};
      }
      int64_t target = insn.end + displacement;
      if (!add_target(target)) {
        return {};
      }
      if (target <= insn.begin) {
        if (++result.backedges > 1) {
          return {};
        }
        loop_begin = pending.back();
        loop_end = index;
      }
    }
    if (insn.flow != CodeCostFlow::kExit && insn.flow != CodeCostFlow::kJump) {
      if (!add_target(insn.end)) {
        return {};
      }
    }
  }

  if (result.backedges) {
    for (size_t i = loop_begin; i <= loop_end; ++i) {
      if (visited[i]) {
        result.loop_score += insns[i].weight;
      }
    }
  }
  result.valid = true;
  return result;
}

bool PreferHeavyCode(const CodeCostSummary& heavy, const CodeCostSummary& lite) {
  if (!heavy.valid || !lite.valid || heavy.backedges != lite.backedges || !lite.score) {
    return false;
  }
  if (heavy.score * 10 > lite.score * 9) {
    return false;
  }
  return !lite.backedges || (lite.loop_score && heavy.loop_score * 10 <= lite.loop_score * 9);
}

}  // namespace berberis
