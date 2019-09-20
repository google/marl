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

#ifndef marl_memory_h
#define marl_memory_h

#include "debug.h"

#include <stdint.h>
#include <cstdlib>

namespace marl {

template <typename T>
inline T alignUp(T val, T alignment) {
  return alignment * ((val + alignment - 1) / alignment);
}

// aligned_malloc allocates size bytes of uninitialized storage with the minumum
// specified byte alignment.
// The pointer returned must be freed with aligned_free().
inline void* aligned_malloc(size_t alignment, size_t size) {
  MARL_ASSERT(alignment < 256, "alignment must less than 256");
  auto allocation = new uint8_t[size + sizeof(uint8_t) + alignment];
  auto aligned = allocation;
  aligned += sizeof(uint8_t);  // Make space for the base-address offset.
  aligned = reinterpret_cast<uint8_t*>(
      alignUp(reinterpret_cast<uintptr_t>(aligned), alignment));  // align
  auto offset = static_cast<uint8_t>(aligned - allocation);
  aligned[-1] = offset;
  return aligned;
}

// aligned_free frees memory allocated by aligned_malloc.
inline void aligned_free(void* ptr) {
  auto aligned = reinterpret_cast<uint8_t*>(ptr);
  auto offset = aligned[-1];
  auto allocation = aligned - offset;
  delete[] allocation;
}

}  // namespace marl

#endif  // marl_memory_h
