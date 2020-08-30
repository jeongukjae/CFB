cmake_minimum_required(VERSION 3.0)

project(CFB VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB TEST_CODE
  tests/*.cc
)

include_directories(${PROJECT_SOURCE_DIR}/include)

option(CODE_COVERAGE "Enable coverage reporting" OFF)

add_library(coverage_config INTERFACE)
if(CODE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # Add required flags (GCC & LLVM/Clang)
  target_compile_options(coverage_config INTERFACE
    -O0        # no optimization
    -g         # generate debug info
    --coverage # sets all required flags
  )
  target_link_libraries(coverage_config INTERFACE --coverage)
endif()

option(BUILD_TEST "Build the tests" OFF)

if(BUILD_TEST)
  enable_testing()
  add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/googletest)

  include_directories(${gtest_SOURCE_DIR}/include)
  include_directories(${gmock_SOURCE_DIR}/include)

  add_executable(run-test ${TEST_CODE})
  target_link_libraries(run-test gtest gmock gtest_main)
  target_link_libraries(run-test coverage_config)

  add_test(NAME run-test COMMAND ./run-test)
endif()
