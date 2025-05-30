#------------------------------------------------------------------------------
# Top-level CMake file for bcos-rpc
# ------------------------------------------------------------------------------
# Copyright (C) 2021 bcos-rpc
# SPDX-License-Identifier: Apache-2.0
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.10)

project(bcos-rpc VERSION ${VERSION})

find_package(jsoncpp CONFIG REQUIRED)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB_RECURSE SRCS bcos-rpc/*.cpp)

find_package(tarscpp REQUIRED)

add_library(${RPC_TARGET} ${SRCS} ${HEADERS})
target_link_libraries(${RPC_TARGET} PUBLIC bcos-boostssl ${LEDGER_TARGET} ${CODEC_TARGET} ${CRYPTO_TARGET} ${TARS_PROTOCOL_TARGET} jsoncpp_static tarscpp::tarsservant tarscpp::tarsutil)
set_target_properties(${RPC_TARGET} PROPERTIES UNITY_BUILD "ON")

 if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
 endif()

# for code coverage
#if (COVERAGE)
#    include(Coverage)
#    config_coverage("rpc-coverage" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_SOURCE_DIR}/test/mock**' '${CMAKE_SOURCE_DIR}/test/main**'")
#endif ()
