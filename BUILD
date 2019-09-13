# Copyright 2019 The Marl Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cc_library(
  name = "marl",
  includes = [
    "include",
  ],
  hdrs = glob([
    "include/marl/**/*.h",
  ]),
  srcs = glob([
    "src/**/*.cpp",
    "src/**/*.c",
    "src/**/*.S",
    "src/**/*.h",
  ], exclude = glob([
    "src/**/*_test.cpp",
  ])),
  linkopts = select({
    "@bazel_tools//src/conditions:linux_x86_64": [ "-pthread" ],
    "//conditions:default": [],
  }),
  visibility = [
    "//visibility:public",
  ],
)

cc_test(
  name = "tests",
  srcs = glob([
    "src/**/*_test.cpp",
  ]),
  deps = [
    "//:marl",
    "@googletest//:gtest",
  ],
)
