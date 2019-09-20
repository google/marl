// Copyright 2019 The Marl Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "marl/memory.h"

#include "marl_test.h"

class MemoryTest : public testing::Test {};

TEST(MemoryTest, AlignedMalloc) {
  std::vector<size_t> sizes = {1,   2,   3,   4,   5,   7,   8,   14,  16,  17,
                               31,  34,  50,  63,  64,  65,  100, 127, 128, 129,
                               200, 255, 256, 257, 500, 511, 512, 513};
  std::vector<size_t> alignments = {1, 2, 4, 8, 16, 32, 64, 128};
  for (auto alignment : alignments) {
    for (auto size : sizes) {
      auto ptr = marl::aligned_malloc(alignment, size);
      ASSERT_EQ(reinterpret_cast<uintptr_t>(ptr) & (alignment - 1), 0U);
      memset(ptr, 0, size);  // Check the memory was actually allocated.
      marl::aligned_free(ptr);
    }
  }
}