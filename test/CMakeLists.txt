# Copyright (c) Maarten L. Hekkelman 2026
# 
# SPDX-License-Identifier: BSD-2-Clause

find_package(Catch2 REQUIRED)

add_executable(unit-test unit-test.cpp ${PROJECT_SOURCE_DIR}/src/fisher.cpp)

target_include_directories(unit-test PRIVATE ${PROJECT_SOURCE_DIR}/src)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    target_compile_options(unit-test PRIVATE -Wall -Wextra)
elseif(MSVC)
    target_compile_options(unit-test PRIVATE /W4)
endif()

target_link_libraries(unit-test Catch2::Catch2WithMain)
add_test(NAME unit-test COMMAND unit-test)
