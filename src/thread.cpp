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

#include "marl/thread.h"

#include "marl/debug.h"
#include "marl/defer.h"
#include "marl/trace.h"

#include <cstdarg>
#include <cstdio>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <condition_variable>
#include <cstdlib>  // mbstowcs
#include <mutex>
#include <vector>
#elif defined(__APPLE__)
#include <mach/thread_act.h>
#include <pthread.h>
#include <unistd.h>
#include <thread>
#else
#include <pthread.h>
#include <unistd.h>
#include <thread>
#endif

namespace marl {

#if defined(_WIN32)

#define CHECK_WIN32(expr)                                    \
  do {                                                       \
    auto res = expr;                                         \
    (void)res;                                               \
    MARL_ASSERT(res == TRUE, #expr " failed with error: %d", \
                (int)GetLastError());                        \
  } while (false)

namespace {

struct ProcessorGroup {
  unsigned int count;  // number of logical processors in this group.
  KAFFINITY affinity;  // affinity mask.
};

const std::vector<ProcessorGroup>& getProcessorGroups() {
  static std::vector<ProcessorGroup> groups = [] {
    std::vector<ProcessorGroup> out;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info[32] = {};
    DWORD size = sizeof(info);
    CHECK_WIN32(GetLogicalProcessorInformationEx(RelationGroup, info, &size));
    DWORD count = size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
    for (DWORD i = 0; i < count; i++) {
      if (info[i].Relationship == RelationGroup) {
        auto groupCount = info[i].Group.ActiveGroupCount;
        for (WORD groupIdx = 0; groupIdx < groupCount; groupIdx++) {
          auto const& groupInfo = info[i].Group.GroupInfo;
          out.emplace_back(ProcessorGroup{groupInfo->ActiveProcessorCount,
                                          groupInfo->ActiveProcessorMask});
        }
      }
    }
    return out;
  }();
  return groups;
}

bool getGroupAffinity(unsigned int index, GROUP_AFFINITY* groupAffinity) {
  auto& groups = getProcessorGroups();
  for (size_t groupIdx = 0; groupIdx < groups.size(); groupIdx++) {
    auto& group = groups[groupIdx];
    if (index < group.count) {
      for (int i = 0; i < sizeof(group.affinity) * 8; i++) {
        if (group.affinity & (1ULL << i)) {
          if (index == 0) {
            groupAffinity->Group = static_cast<WORD>(groupIdx);
            // Use the whole group's affinity, as the OS is then able to shuffle
            // threads around based on external demands. Pinning these to a
            // single core can cause up to 20% performance loss in benchmarking.
            groupAffinity->Mask = group.affinity;
            return true;
          }
          index--;
        }
      }
      return false;
    } else {
      index -= group.count;
    }
  }
  return false;
}

}  // namespace

class Thread::Impl {
 public:
  Impl(const Func& func) : func(func) {}
  static DWORD WINAPI run(void* self) {
    reinterpret_cast<Impl*>(self)->run();
    return 0;
  }

  void run() {
    func();
    func = Func();
    std::unique_lock<std::mutex> lock(mutex);
    finished = true;
    cv.notify_all();
  }

  Func func;
  std::mutex mutex;
  HANDLE handle;
  std::condition_variable cv;  // guarded by mutex.
  bool finished = false;       // guarded by mutex.
};

Thread::Thread(unsigned int logicalCpu, const Func& func) {
  SIZE_T size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
  MARL_ASSERT(size > 0,
              "InitializeProcThreadAttributeList() did not give a size");

  LPPROC_THREAD_ATTRIBUTE_LIST attributes =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(alloca(size));
  CHECK_WIN32(InitializeProcThreadAttributeList(attributes, 1, 0, &size));
  defer(DeleteProcThreadAttributeList(attributes));

  GROUP_AFFINITY groupAffinity = {};
  if (getGroupAffinity(logicalCpu, &groupAffinity)) {
    CHECK_WIN32(UpdateProcThreadAttribute(
        attributes, 0, PROC_THREAD_ATTRIBUTE_GROUP_AFFINITY, &groupAffinity,
        sizeof(groupAffinity), nullptr, nullptr));
  }

  impl = new Impl(func);
  impl->handle = CreateRemoteThreadEx(GetCurrentProcess(), nullptr, 0,
                                      &Impl::run, impl, 0, attributes, nullptr);
}

Thread::~Thread() {
  if (impl) {
    CloseHandle(impl->handle);
    delete impl;
  }
}

void Thread::join() {
  MARL_ASSERT(impl != nullptr, "join() called on unjoinable thread");
  std::unique_lock<std::mutex> lock(impl->mutex);
  impl->cv.wait(lock, [&] { return impl->finished; });
}

void Thread::setName(const char* fmt, ...) {
  static auto setThreadDescription =
      reinterpret_cast<HRESULT(WINAPI*)(HANDLE, PCWSTR)>(GetProcAddress(
          GetModuleHandle("kernelbase.dll"), "SetThreadDescription"));
  if (setThreadDescription == nullptr) {
    return;
  }

  char name[1024];
  va_list vararg;
  va_start(vararg, fmt);
  vsnprintf(name, sizeof(name), fmt, vararg);
  va_end(vararg);

  wchar_t wname[1024];
  mbstowcs(wname, name, 1024);
  setThreadDescription(GetCurrentThread(), wname);
  MARL_NAME_THREAD("%s", name);
}

unsigned int Thread::numLogicalCPUs() {
  unsigned int count = 0;
  for (auto& group : getProcessorGroups()) {
    count += group.count;
  }
  return count;
}

#else

class Thread::Impl {
 public:
  template <typename F>
  Impl(F&& func) : thread(func) {}
  std::thread thread;
};

Thread::Thread(unsigned int /* logicalCpu */, const Func& func)
    : impl(new Thread::Impl(func)) {}

Thread::~Thread() {
  delete impl;
}

void Thread::join() {
  impl->thread.join();
}

void Thread::setName(const char* fmt, ...) {
  char name[1024];
  va_list vararg;
  va_start(vararg, fmt);
  vsnprintf(name, sizeof(name), fmt, vararg);
  va_end(vararg);

#if defined(__APPLE__)
  pthread_setname_np(name);
#elif !defined(__Fuchsia__)
  pthread_setname_np(pthread_self(), name);
#endif

  MARL_NAME_THREAD("%s", name);
}

unsigned int Thread::numLogicalCPUs() {
  return sysconf(_SC_NPROCESSORS_ONLN);
}

#endif  // OS

Thread::Thread(Thread&& rhs) : impl(rhs.impl) {
  rhs.impl = nullptr;
}

Thread& Thread::operator=(Thread&& rhs) {
  if (impl) {
    delete impl;
    impl = nullptr;
  }
  impl = rhs.impl;
  rhs.impl = nullptr;
  return *this;
}

}  // namespace marl
