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

function show_cmds { set -x; }
function hide_cmds { { set +x; } 2>/dev/null; }

. /bin/using.sh # Declare the bash `using` function for configuring toolchains.

echo "Fetching submodules..."
git submodule update --init

using gcc-9 # Always update gcc so we get a newer standard library.

if [ "$BUILD_SYSTEM" == "cmake" ]; then
    using cmake-3.17.2

    SRC_DIR=$(pwd)
    BUILD_DIR=/tmp/marl-build

    COMMON_CMAKE_FLAGS=""
    COMMON_CMAKE_FLAGS+=" -DMARL_BUILD_EXAMPLES=1"
    COMMON_CMAKE_FLAGS+=" -DMARL_BUILD_TESTS=1"
    COMMON_CMAKE_FLAGS+=" -DMARL_BUILD_BENCHMARKS=1"
    COMMON_CMAKE_FLAGS+=" -DMARL_WARNINGS_AS_ERRORS=1"
    COMMON_CMAKE_FLAGS+=" -DMARL_DEBUG_ENABLED=1"
    COMMON_CMAKE_FLAGS+=" -DMARL_BUILD_SHARED=${BUILD_SHARED}"

    if [ "$BUILD_TOOLCHAIN" == "ndk" ]; then
        using ndk-r21d
        COMMON_CMAKE_FLAGS+=" -DANDROID_ABI=$BUILD_TARGET_ARCH"
        COMMON_CMAKE_FLAGS+=" -DANDROID_NATIVE_API_LEVEL=18"
        COMMON_CMAKE_FLAGS+=" -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
    else # !ndk
        if [ "$BUILD_TOOLCHAIN" == "clang" ]; then
            using clang-10.0.0
        fi
        if [ "$BUILD_TARGET_ARCH" == "x86" ]; then
            COMMON_CMAKE_FLAGS+=" -DCMAKE_CXX_FLAGS=-m32"
            COMMON_CMAKE_FLAGS+=" -DCMAKE_C_FLAGS=-m32"
            COMMON_CMAKE_FLAGS+=" -DCMAKE_ASM_FLAGS=-m32"
        fi
    fi

    if [ "$BUILD_SANITIZER" == "asan" ]; then
        COMMON_CMAKE_FLAGS+=" -DMARL_ASAN=1"
    elif [ "$BUILD_SANITIZER" == "msan" ]; then
        COMMON_CMAKE_FLAGS+=" -DMARL_MSAN=1"
    elif [ "$BUILD_SANITIZER" == "tsan" ]; then
        COMMON_CMAKE_FLAGS+=" -DMARL_TSAN=1"
    fi

    function buildAndTest {
        DESCRIPTION=$1
        CMAKE_FLAGS=$2

        echo "Building ${DESCRIPTION}..."

        # Ensure BUILD_DIR is empty.
        if [ -d ${BUILD_DIR} ]; then
            rm -fr ${BUILD_DIR}
        fi
        mkdir ${BUILD_DIR}
        cd ${BUILD_DIR}

        show_cmds
            cmake ${SRC_DIR} ${CMAKE_FLAGS} ${COMMON_CMAKE_FLAGS}

            make --jobs=$(nproc)

            echo "Testing ${DESCRIPTION}..."

            if [ "$BUILD_TOOLCHAIN" != "ndk" ]; then
                ./marl-unittests
                ./fractal
                ./hello_task
                ./primes > /dev/null
                ./tasks_in_tasks
            fi
        hide_cmds
    }

    buildAndTest "marl with ucontext fibers" "-DMARL_FIBERS_USE_UCONTEXT=1"
    buildAndTest "marl with assembly fibers" "-DMARL_FIBERS_USE_UCONTEXT=0"

    if [ -n "$BUILD_ARTIFACTS" ]; then
        ARTIFACTS=()
        ARTIFACTS+=($(find . -maxdepth 1 -type f   -name "*.a"))
        ARTIFACTS+=($(find . -maxdepth 1 -type f,l -name "*.so*"))
        ARTIFACTS+=($(find . -maxdepth 1 -type f   -name "*.dlsym"))
        ARTIFACTS+=($(find . -maxdepth 1 -type f   -name "*.dll"))
        for file in "${ARTIFACTS[@]}"; do
            echo "Copying build artifact ${file}..."
            cp ${file} $BUILD_ARTIFACTS
        done
    fi

elif [ "$BUILD_SYSTEM" == "bazel" ]; then
    using bazel-3.1.0

    show_cmds
        bazel test //:tests --test_output=all
        bazel run //examples:fractal
        bazel run //examples:hello_task
        bazel run //examples:primes > /dev/null
        bazel run //examples:tasks_in_tasks
    hide_cmds
else
    echo "Unknown build system: $BUILD_SYSTEM"
    exit 1
fi

echo "*** Done ***"
