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

#include <cstring>

namespace {

template <typename T>
inline T alignUp(T val, T alignment) {
  return alignment * ((val + alignment - 1) / alignment);
}

// aligned_malloc() allocates size bytes of uninitialized storage with the
// specified minimum byte alignment. The pointer returned must be freed with
// aligned_free().
inline void* aligned_malloc(size_t alignment, size_t size) {
  size_t allocSize = size + alignment + sizeof(void*);
  auto allocation = malloc(allocSize);
  auto aligned = reinterpret_cast<uint8_t*>(
      alignUp(reinterpret_cast<uintptr_t>(allocation), alignment));  // align
  memcpy(aligned + size, &allocation, sizeof(void*));  // pointer-to-allocation
  return aligned;
}

// aligned_free() frees memory allocated by aligned_malloc.
inline void aligned_free(void* ptr, size_t size) {
  void* base;
  memcpy(&base, reinterpret_cast<uint8_t*>(ptr) + size, sizeof(size_t));
  free(base);
}

class DefaultAllocator : public marl::Allocator {
 public:
  static DefaultAllocator instance;

  virtual marl::Allocation allocate(
      const marl::Allocation::Request& request) override {
    void* ptr = nullptr;

    MARL_ASSERT(!request.use_guards, "TODO: Guards not yet implemented");
    if (request.alignment > 1U) {
      ptr = ::aligned_malloc(request.alignment, request.size);
    } else {
      ptr = ::malloc(request.size);
    }

    MARL_ASSERT(ptr != nullptr, "Allocation failed");

    marl::Allocation allocation;
    allocation.ptr = ptr;
    allocation.request = request;
    return allocation;
  }

  virtual void free(const marl::Allocation& allocation) override {
    MARL_ASSERT(!allocation.request.use_guards,
                "TODO: Guards not yet implemented");
    if (allocation.request.alignment > 1U) {
      ::aligned_free(allocation.ptr, allocation.request.size);
    } else {
      ::free(allocation.ptr);
    }
  }
};

DefaultAllocator DefaultAllocator::instance;

}  // anonymous namespace

namespace marl {

Allocator* Allocator::Default = &DefaultAllocator::instance;

}  // namespace marl
