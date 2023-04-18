// Copyright 2023 The Marl Authors.
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

#ifndef marl_move_only_function_h
#define marl_move_only_function_h

#include "memory.h"

#include <cstddef>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace marl {

#if __cplusplus > 201402L

using std::invoke;

#else
// std::invoke for C++11 and C++14
template <typename Functor, typename... Args>
typename std::enable_if<
    std::is_member_pointer<typename std::decay<Functor>::type>::value,
    typename std::result_of<Functor && (Args && ...)>::type>::type
invoke(Functor&& f, Args&&... args) {
  return std::mem_fn(f)(std::forward<Args>(args)...);
}

template <typename Functor, typename... Args>
typename std::enable_if<
    !std::is_member_pointer<typename std::decay<Functor>::type>::value,
    typename std::result_of<Functor && (Args && ...)>::type>::type
invoke(Functor&& f, Args&&... args) {
  return std::forward<Functor>(f)(std::forward<Args>(args)...);
}

#endif  // __cplusplus > 201402L

template <class R>
class move_only_function;

#if __cplusplus > 201402L && (defined(__GNUC__) || defined(__clang__))
template <class R, class... Args, bool kNoexcept>
class move_only_function<R(Args...) noexcept(kNoexcept)> {
#else
template <class R, class... Args>
class move_only_function<R(Args...)> {
  static const bool kNoexcept = false;  // private
#endif

 public:
  constexpr move_only_function() = default;
  move_only_function(std::nullptr_t) {}

  template <class F,
            class = typename std::enable_if<
                !std::is_same<typename std::decay<F>::type,
                              move_only_function>::value>::type>
  move_only_function(F&& function) {
    if (sizeof(typename std::decay<F>::type) <= kMaximumOptimizableSize) {
      ops_ = EraseCopySmall(&object_, std::forward<F>(function));
    } else {
      ops_ = EraseCopyLarge(&object_, std::forward<F>(function));
    }
  }

  move_only_function(move_only_function&& function) noexcept {
    ops_ = function.ops_;
    function.ops_ = nullptr;
    if (ops_) {
      ops_->manager(&object_, &function.object_);
    }
  }
  ~move_only_function() {
    if (ops_) {
      ops_->manager(&object_, nullptr);
    }
  }

  template <class F,
            class = typename std::enable_if<
                !std::is_same<typename std::decay<F>::type,
                              move_only_function>::value>::type>
  move_only_function& operator=(F&& function) {
    this->~move_only_function();
    new (this) move_only_function(std::forward<F>(function));
    return *this;
  }

  move_only_function& operator=(move_only_function&& function) noexcept {
    if (&function != this) {
      this->~move_only_function();
      new (this) move_only_function(std::move(function));
    }
    return *this;
  }

  move_only_function& operator=(std::nullptr_t) {
    ops_->manager(&object_, nullptr);
    ops_ = nullptr;
    return *this;
  }

  // The behavior is undefined if `*this == nullptr` holds.
  R operator()(Args... args) const noexcept(kNoexcept) {
    return ops_->invoker(&object_, std::forward<Args>(args)...);
  }

  constexpr explicit operator bool() const { return ops_; }

 private:
  static constexpr std::size_t kMaximumOptimizableSize = 3 * sizeof(void*);

  struct TypeOps {
    using Invoker = R (*)(void* object, Args&&... args);
    using Manager = void (*)(void* dest, void* src);

    Invoker invoker;
    Manager manager;
  };

  template <class F>
  static R Invoke(F&& f, Args&&... args) {
    return invoke(std::forward<F>(f), std::forward<Args>(args)...);
  }

  template <class T>
  const TypeOps* EraseCopySmall(void* buffer, T&& obejct) {
    using Decayed = typename std::decay<T>::type;

    static const TypeOps ops = {
        // Invoker
        [](void* object, Args&&... args) -> R {
          return Invoke(*static_cast<Decayed*>(object),
                        std::forward<Args>(args)...);
        },
        // Manager
        [](void* dest, void* src) {
          if (src) {
            new (dest) Decayed(std::move(*static_cast<Decayed*>(src)));
            static_cast<Decayed*>(src)->~Decayed();
          } else {
            static_cast<Decayed*>(dest)->~Decayed();
          }
        }};

    new (buffer) Decayed(std::forward<T>(obejct));
    return &ops;
  }

  template <class T>
  const TypeOps* EraseCopyLarge(void* buffer, T&& object) {
    using Decayed = typename std::decay<T>::type;
    using Stored = Decayed*;

    static const TypeOps ops = {
        /* invoker */
        [](void* object, Args&&... args) -> R {
          return Invoke(**static_cast<Stored*>(object),
                        std::forward<Args>(args)...);
        },
        /* Manager */
        [](void* dest, void* src) {
          if (src) {
            new (dest) Stored(*static_cast<Stored*>(src));
          } else {
            delete *static_cast<Stored*>(dest);
          }
        },
    };
    new (buffer) Stored(new Decayed(std::forward<T>(object)));
    return &ops;
  }

  mutable marl::aligned_storage<kMaximumOptimizableSize, 1>::type object_;
  const TypeOps* ops_ = nullptr;
};

}  // namespace marl

#endif
