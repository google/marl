#!/bin/bash

# Copyright 2020 The Marl Authors.
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

set -e # Fail on any error.
set -x # Display commands being run.

BUILD_ROOT=$PWD

cd github/marl

git submodule update --init

if [ "$BUILD_SYSTEM" == "cmake" ]; then
    mkdir build
    cd build

    cmake .. -DMARL_BUILD_EXAMPLES=1 \
             -DMARL_BUILD_TESTS=1 \
             -DMARL_BUILD_BENCHMARKS=1 \
             -DMARL_WARNINGS_AS_ERRORS=1 \
             -DMARL_DEBUG_ENABLED=1

    make -j$(sysctl -n hw.logicalcpu)

    ./marl-unittests

    ./fractal
    ./primes > /dev/null
elif [ "$BUILD_SYSTEM" == "bazel" ]; then
    # Get bazel
    curl -L -k -O -s https://github.com/bazelbuild/bazel/releases/download/0.29.1/bazel-0.29.1-installer-darwin-x86_64.sh
    mkdir $BUILD_ROOT/bazel
    sh bazel-0.29.1-installer-darwin-x86_64.sh --prefix=$BUILD_ROOT/bazel
    rm bazel-0.29.1-installer-darwin-x86_64.sh
    # Build and run
    $BUILD_ROOT/bazel/bin/bazel test //:tests --test_output=all
    $BUILD_ROOT/bazel/bin/bazel run //examples:fractal
    $BUILD_ROOT/bazel/bin/bazel run //examples:primes > /dev/null
else
    echo "Unknown build system: $BUILD_SYSTEM"
    exit 1
fi
