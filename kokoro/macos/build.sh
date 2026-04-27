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

cd "${ROOT_DIR}"

function show_cmds { set -x; }
function hide_cmds { { set +x; } 2>/dev/null; }
function status {
    echo ""
    echo "*****************************************************************"
    echo "* $@"
    echo "*****************************************************************"
    echo ""
}

status "Fetching submodules"
git submodule update --init

status "Setting up environment"

if [ "$BUILD_SYSTEM" == "cmake" ]; then
    SRC_DIR=$(pwd)
    BUILD_DIR=/tmp/marl-build
    INSTALL_DIR=${BUILD_DIR}/install
    CMAKE_DIR=/tmp/cmake-install
    CMAKE_VER=4.3.2

    # clean
    # Ensures the given directory exists and is empty
    function clean {
        if [[ -d "$1" ]]; then
            rm -fr "$1"
        fi
        mkdir -p "$1"
    }

    # The macos Kokoro bots don't have cmake.
    clean $CMAKE_DIR
    status "Download cmake"
    show_cmds
      pushd $CMAKE_DIR
      curl -L -o tarball.tar.gz https://github.com/Kitware/CMake/releases/download/v$CMAKE_VER/cmake-$CMAKE_VER-macos-universal.tar.gz
      tar xf tarball.tar.gz
      popd
    hide_cmds
    CMAKE=$CMAKE_DIR/cmake-$CMAKE_VER-macos-universal/CMake.app/Contents/bin/cmake
    if [[ ! -x $CMAKE ]]; then
      echo "Failed to download cmake"
      exit 1
    fi

    COMMON_CMAKE_FLAGS=""
    COMMON_CMAKE_FLAGS+=" -DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    COMMON_CMAKE_FLAGS+=" -DMARL_BUILD_EXAMPLES=1"
    COMMON_CMAKE_FLAGS+=" -DMARL_BUILD_TESTS=1"
    COMMON_CMAKE_FLAGS+=" -DMARL_BUILD_BENCHMARKS=1"
    COMMON_CMAKE_FLAGS+=" -DMARL_WARNINGS_AS_ERRORS=1"
    COMMON_CMAKE_FLAGS+=" -DMARL_DEBUG_ENABLED=1"
    COMMON_CMAKE_FLAGS+=" -DMARL_BUILD_SHARED=${BUILD_SHARED:-0}"
    COMMON_CMAKE_FLAGS+=" -DBENCHMARK_ENABLE_INSTALL=0"
    COMMON_CMAKE_FLAGS+=" -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}"

    if [ "$BUILD_SANITIZER" == "asan" ]; then
        COMMON_CMAKE_FLAGS+=" -DMARL_ASAN=1"
    elif [ "$BUILD_SANITIZER" == "msan" ]; then
        COMMON_CMAKE_FLAGS+=" -DMARL_MSAN=1"
    elif [ "$BUILD_SANITIZER" == "tsan" ]; then
        COMMON_CMAKE_FLAGS+=" -DMARL_TSAN=1"
    fi

    # build <description> <flags>
    # Cleans build directory and performs a build using the provided CMake flags.
    function build {
        DESCRIPTION=$1
        CMAKE_FLAGS=$2

        status "Building ${DESCRIPTION}"
        clean ${BUILD_DIR}
        cd ${BUILD_DIR}
        show_cmds
            $CMAKE ${SRC_DIR} ${CMAKE_FLAGS} ${COMMON_CMAKE_FLAGS}
            make --jobs=$(sysctl -n hw.logicalcpu)
        hide_cmds
    }

    # test <description>
    # Runs the pre-built unit tests (if not an NDK build).
    function test {
        DESCRIPTION=$1

        status "Testing ${DESCRIPTION}"
        cd ${BUILD_DIR}
        show_cmds
            if [ "$BUILD_TOOLCHAIN" != "ndk" ]; then
                ./marl-unittests
                ./fractal
                ./hello_task
                ./primes > /dev/null
                ./tasks_in_tasks
            fi
        hide_cmds
    }

    # install <description>
    # Installs the pre-built library to ${INSTALL_DIR}.
    function install {
        DESCRIPTION=$1

        status "Installing ${DESCRIPTION}"
        cd ${BUILD_DIR}
        show_cmds
            make install
        hide_cmds
    }

    # build <description> <flags>
    # Cleans build directory and performs a build using the provided CMake
    # flags, then runs tests.
    function buildAndTest {
        DESCRIPTION=$1
        CMAKE_FLAGS=$2
        build "$DESCRIPTION" "$CMAKE_FLAGS"
        test  "$DESCRIPTION"
    }

    # build <description> <flags>
    # Cleans build directory and performs a build using the provided CMake
    # flags, then installs the library to ${INSTALL_DIR}.
    function buildAndInstall {
        DESCRIPTION=$1
        CMAKE_FLAGS=$2
        build   "$DESCRIPTION" "$CMAKE_FLAGS"
        install "$DESCRIPTION"
    }

    if [ -n "$RUN_TESTS" ]; then
        buildAndTest "marl for test" ""
    fi

    buildAndInstall "marl for install" "-DMARL_INSTALL=1"

    if [ -n "$BUILD_ARTIFACTS" ]; then
        status "Copying build artifacts"
        show_cmds
            tar -czvf "$BUILD_ARTIFACTS/build.tar.gz" -C "$INSTALL_DIR" .
        hide_cmds
    fi

elif [ "$BUILD_SYSTEM" == "bazel" ]; then
    # Get bazel
    BAZEL_VER=7.4.0
    BAZEL=bazel-$BAZEL_VER-darwin-$(uname -m)
    gcloud config set auth/disable_credentials True
    gcloud storage cp gs://bazel/$BAZEL_VER/release/$BAZEL .
    chmod +x $BAZEL

    show_cmds
        "./${BAZEL}" test //:tests --test_output=all
        "./${BAZEL}" run //examples:fractal
        "./${BAZEL}" run //examples:hello_task
        "./${BAZEL}" run //examples:primes > /dev/null
        "./${BAZEL}" run //examples:tasks_in_tasks
    hide_cmds
else
    status "Unknown build system: $BUILD_SYSTEM"
    exit 1
fi

status "Done"
