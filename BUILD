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
