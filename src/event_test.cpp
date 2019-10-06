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

#include "marl/event.h"
#include "marl/waitgroup.h"

#include "marl_test.h"

TEST_P(WithBoundScheduler, EventIsSignalled) {
  std::vector<marl::Event::Mode> modes = {marl::Event::Mode::Manual,
                                          marl::Event::Mode::Auto};
  for (auto mode : modes) {
    auto event = marl::Event(mode);
    ASSERT_EQ(event.isSignalled(), false);
    event.signal();
    ASSERT_EQ(event.isSignalled(), true);
    ASSERT_EQ(event.isSignalled(), true);
    event.clear();
    ASSERT_EQ(event.isSignalled(), false);
  }
}

TEST_P(WithBoundScheduler, EventAutoTest) {
  auto event = marl::Event(marl::Event::Mode::Auto);
  ASSERT_EQ(event.test(), false);
  event.signal();
  ASSERT_EQ(event.test(), true);
  ASSERT_EQ(event.test(), false);
}

TEST_P(WithBoundScheduler, EventManualTest) {
  auto event = marl::Event(marl::Event::Mode::Manual);
  ASSERT_EQ(event.test(), false);
  event.signal();
  ASSERT_EQ(event.test(), true);
  ASSERT_EQ(event.test(), true);
}

TEST_P(WithBoundScheduler, EventAutoWait) {
  std::atomic<int> counter = {0};
  auto event = marl::Event(marl::Event::Mode::Auto);
  auto done = marl::Event(marl::Event::Mode::Auto);

  for (int i = 0; i < 3; i++) {
    marl::schedule([=, &counter] {
      event.wait();
      counter++;
      done.signal();
    });
  }

  ASSERT_EQ(counter.load(), 0);
  event.signal();
  done.wait();
  ASSERT_EQ(counter.load(), 1);
  event.signal();
  done.wait();
  ASSERT_EQ(counter.load(), 2);
  event.signal();
  done.wait();
  ASSERT_EQ(counter.load(), 3);
}

TEST_P(WithBoundScheduler, EventManualWait) {
  std::atomic<int> counter = {0};
  auto event = marl::Event(marl::Event::Mode::Manual);
  auto wg = marl::WaitGroup(3);
  for (int i = 0; i < 3; i++) {
    marl::schedule([=, &counter] {
      event.wait();
      counter++;
      wg.done();
    });
  }
  event.signal();
  wg.wait();
  ASSERT_EQ(counter.load(), 3);
}

TEST_P(WithBoundScheduler, EventSequence) {
  std::vector<marl::Event::Mode> modes = {marl::Event::Mode::Manual,
                                          marl::Event::Mode::Auto};
  for (auto mode : modes) {
    std::string sequence;
    auto eventA = marl::Event(mode);
    auto eventB = marl::Event(mode);
    auto eventC = marl::Event(mode);
    auto done = marl::Event(mode);
    marl::schedule([=, &sequence] {
      eventB.wait();
      sequence += "B";
      eventC.signal();
    });
    marl::schedule([=, &sequence] {
      eventA.wait();
      sequence += "A";
      eventB.signal();
    });
    marl::schedule([=, &sequence] {
      eventC.wait();
      sequence += "C";
      done.signal();
    });
    ASSERT_EQ(sequence, "");
    eventA.signal();
    done.wait();
    ASSERT_EQ(sequence, "ABC");
  }
}
