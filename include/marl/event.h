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

#ifndef marl_event_h
#define marl_event_h

#include "conditionvariable.h"
#include "memory.h"

namespace marl {

// Event is a synchronization primitive used to block until a signal is raised.
class Event {
 public:
  enum class Mode {
    // The event signal will be automatically reset when a call to wait()
    // returns.
    // A single call to signal() will only unblock a single (possibly
    // future) call to wait().
    Auto,

    // While the event is in the signaled state, any calls to wait() will
    // unblock without automatically reseting the signaled state.
    // The signaled state can be reset with a call to clear().
    Manual
  };

  inline Event(Mode mode = Mode::Auto,
               bool initialState = false,
               Allocator* allocator = Allocator::Default);

  // signal() signals the event, possibly unblocking a call to wait().
  inline void signal() const;

  // clear() clears the signaled state.
  inline void clear() const;

  // wait() blocks until the event is signaled.
  // If the event was constructed with the Auto Mode, then only one
  // call to wait() will unblock before returning, upon which the signalled
  // state will be automatically cleared.
  inline void wait() const;

  // wait_for() blocks until the event is signaled, or the timeout has been
  // reached.
  // If the timeout was reached, then wait_for() return false.
  // If the event is signalled and event was constructed with the Auto Mode,
  // then only one call to wait() will unblock before returning, upon which the
  // signalled state will be automatically cleared.
  template <typename Rep, typename Period>
  inline bool wait_for(
      const std::chrono::duration<Rep, Period>& duration) const;

  // wait_until() blocks until the event is signaled, or the timeout has been
  // reached.
  // If the timeout was reached, then wait_for() return false.
  // If the event is signalled and event was constructed with the Auto Mode,
  // then only one call to wait() will unblock before returning, upon which the
  // signalled state will be automatically cleared.
  template <typename Clock, typename Duration>
  inline bool wait_until(
      const std::chrono::time_point<Clock, Duration>& timeout) const;

  // test() returns true if the event is signaled, otherwise false.
  // If the event is signalled and was constructed with the Auto Mode
  // then the signalled state will be automatically cleared upon returning.
  inline bool test() const;

  // isSignalled() returns true if the event is signaled, otherwise false.
  // Unlike test() the signal is not automatically cleared when the event was
  // constructed with the Auto Mode.
  // Note: No lock is held after bool() returns, so the event state may
  // immediately change after returning. Use with caution.
  inline bool isSignalled() const;

 private:
  struct Data {
    inline Data(Mode mode, bool initialState);
    std::mutex mutex;
    ConditionVariable cv;
    const Mode mode;
    bool signalled;
  };
  const std::shared_ptr<Data> shared;
};

Event::Data::Data(Mode mode, bool initialState)
    : mode(mode), signalled(initialState) {}

Event::Event(Mode mode /* = Mode::Auto */,
             bool initialState /* = false */,
             Allocator* allocator /* = Allocator::Default */)
    : shared(allocator->make_shared<Data>(mode, initialState)) {}

void Event::signal() const {
  std::unique_lock<std::mutex> lock(shared->mutex);
  if (shared->signalled) {
    return;
  }
  shared->signalled = true;
  if (shared->mode == Mode::Auto) {
    shared->cv.notify_one();
  } else {
    shared->cv.notify_all();
  }
}

void Event::clear() const {
  std::unique_lock<std::mutex> lock(shared->mutex);
  shared->signalled = false;
}

void Event::wait() const {
  std::unique_lock<std::mutex> lock(shared->mutex);
  shared->cv.wait(lock, [&] { return shared->signalled; });
  if (shared->mode == Mode::Auto) {
    shared->signalled = false;
  }
}

template <typename Rep, typename Period>
bool Event::wait_for(const std::chrono::duration<Rep, Period>& duration) const {
  std::unique_lock<std::mutex> lock(shared->mutex);
  if (!shared->cv.wait_for(lock, duration, [&] { return shared->signalled; })) {
    return false;
  }
  if (shared->mode == Mode::Auto) {
    shared->signalled = false;
  }
  return true;
}

template <typename Clock, typename Duration>
bool Event::wait_until(
    const std::chrono::time_point<Clock, Duration>& timeout) const {
  std::unique_lock<std::mutex> lock(shared->mutex);
  if (!shared->cv.wait_until(lock, timeout,
                             [&] { return shared->signalled; })) {
    return false;
  }
  if (shared->mode == Mode::Auto) {
    shared->signalled = false;
  }
  return true;
}

bool Event::test() const {
  std::unique_lock<std::mutex> lock(shared->mutex);
  if (!shared->signalled) {
    return false;
  }
  if (shared->mode == Mode::Auto) {
    shared->signalled = false;
  }
  return true;
}

bool Event::isSignalled() const {
  std::unique_lock<std::mutex> lock(shared->mutex);
  return shared->signalled;
}

}  // namespace marl

#endif  // marl_event_h
