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

#include "marl/move_only_function.h"

#include "marl_test.h"

#include <array>
#include <memory>
#include <vector>

class MoveOnlyFunctionTest : public testing::Test {};

int minus(int a, int b) {
  return a - b;
}

int product(int a, int b) {
  return a * b;
}

struct divides {
  int operator()(int a, int b) const { return a / b; }
};

// noncopyable plus
struct plus {
  int operator()(int a, int b) const { return a + b; }
  plus(const plus&) = delete;
  plus() = default;
  plus(plus&&) = default;
};

template <class T>
void multiplication(T& a, const T& b) {
  a *= b;
}

TEST_F(MoveOnlyFunctionTest, Empty) {
  marl::move_only_function<void()> f;
  EXPECT_FALSE(f);
}

TEST_F(MoveOnlyFunctionTest, Lambda) {
  marl::move_only_function<int(int, int)> f1 = [](int a, int b) {
    return a + b;
  };
  EXPECT_EQ(f1(1, 2), 3);

  int a = 0;
  marl::move_only_function<void()> f2 = [&a]() { ++a; };
  f2();
  EXPECT_EQ(a, 1);

  plus p;
  marl::move_only_function<int(plus &&)> f3([](plus&& p) { return p(2, 5); });
  EXPECT_EQ(f3(std::move(p)), 7);

  divides d;
  marl::move_only_function<int(int, int)> f4(
      [d](int a, int b) { return d(a, b); });
  EXPECT_EQ(f4(20, 5), 4);

#if __cplusplus >= 201402L
  std::unique_ptr<int> uq(new int(3));
  marl::move_only_function<int()> f5 = [uni_ptr = std::move(uq)]() {
    return *uni_ptr;
  };
  EXPECT_EQ(f5(), 3);
#endif

  std::array<char, 1000000> payload;
  payload.back() = 5;
  marl::move_only_function<int()> f6 = [payload] { return payload.back(); };
  EXPECT_EQ(f6(), 5);
}

TEST_F(MoveOnlyFunctionTest, MemberMethod) {
  struct Pii {
    int a, b;
    int hash_func() { return std::hash<int>()(a) ^ std::hash<int>()(b); }
    int sum(int c = 0) const { return a + b + c; }
  };

  plus p;
  marl::move_only_function<int(int, int)> f1(std::move(p));
  EXPECT_EQ(f1(2, 5), 7);

  divides div;
  marl::move_only_function<int(int, int)> f2(div);
  EXPECT_EQ(f2(30, 5), 6);

  Pii pii{4, 5};
  marl::move_only_function<int(Pii*)> f3(&Pii::hash_func);
  EXPECT_EQ(f3(&pii), pii.hash_func());

  marl::move_only_function<int(Pii*, int)> f4(&Pii::sum);
  EXPECT_EQ(f4(&pii, 1), 10);
}

TEST_F(MoveOnlyFunctionTest, Fucntion) {
  marl::move_only_function<int(int, int)> f1(minus);
  EXPECT_EQ(f1(10, 5), 5);

  marl::move_only_function<int(int, int)> f2 = &product;
  EXPECT_EQ(f2(2, 3), 6);

  marl::move_only_function<void(double&, const double&)> f3(
      &multiplication<double>);
  double a = 4.0f;
  f3(a, 5);
  EXPECT_EQ(a, 20);
}

TEST_F(MoveOnlyFunctionTest, Move) {
  struct OnlyCopyable {
    OnlyCopyable() : v(new std::vector<int>()) {}
    OnlyCopyable(const OnlyCopyable& oc) : v(new std::vector<int>(*oc.v)) {}
    ~OnlyCopyable() { delete v; }
    std::vector<int>* v;
  };
  marl::move_only_function<int()> f, f2;
  OnlyCopyable payload;

  payload.v->resize(100, 12);

  // BE SURE THAT THE LAMBDA IS NOT LARGER THAN kMaximumOptimizableSize.
  f = [payload] { return payload.v->back(); };
  f2 = std::move(f);
  EXPECT_EQ(12, f2());
}

TEST_F(MoveOnlyFunctionTest, LargeFunctor) {
  marl::move_only_function<int()> f, f2;
  std::array<std::vector<int>, 100> payload;

  payload.back().resize(10, 12);
  f = [payload] { return payload.back().back(); };
  f2 = std::move(f);
  EXPECT_EQ(12, f2());
}

TEST_F(MoveOnlyFunctionTest, Clear) {
  marl::move_only_function<void()> f([] {});
  EXPECT_TRUE(f);
  f = nullptr;
  EXPECT_FALSE(f);
}
