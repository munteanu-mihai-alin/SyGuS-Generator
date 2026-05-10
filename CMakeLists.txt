cmake_minimum_required(VERSION 3.21)

project(SyGuSGenerator LANGUAGES CXX)

option(SYGUS_BUILD_TESTS "Build parser unit and module tests" ON)
option(SYGUS_ENABLE_COVERAGE "Enable coverage instrumentation" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(sygus_parser
  src/sygus_parser.cpp
)

target_include_directories(sygus_parser
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(sygus_parser PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(sygus_parser PRIVATE /W4 /permissive-)
else()
  target_compile_options(sygus_parser PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SYGUS_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sygus_parser PUBLIC -O0 -g --coverage)
  target_link_options(sygus_parser PUBLIC --coverage)
endif()

add_executable(sygus_parse
  src/sygus_parse_main.cpp
)

target_link_libraries(sygus_parse PRIVATE sygus_parser)

if(SYGUS_BUILD_TESTS)
  include(CTest)
  enable_testing()

  add_executable(sygus_ut
    tests/sygus_ut.cpp
  )
  target_link_libraries(sygus_ut PRIVATE sygus_parser)
  target_compile_definitions(sygus_ut PRIVATE SYGUS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

  add_executable(sygus_mt
    tests/sygus_mt.cpp
  )
  target_link_libraries(sygus_mt PRIVATE sygus_parser)
  target_compile_definitions(sygus_mt PRIVATE SYGUS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

  if(MSVC)
    target_compile_options(sygus_ut PRIVATE /W4 /permissive-)
    target_compile_options(sygus_mt PRIVATE /W4 /permissive-)
  else()
    target_compile_options(sygus_ut PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(sygus_mt PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  if(SYGUS_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sygus_ut PRIVATE -O0 -g --coverage)
    target_compile_options(sygus_mt PRIVATE -O0 -g --coverage)
    target_link_options(sygus_ut PRIVATE --coverage)
    target_link_options(sygus_mt PRIVATE --coverage)
  endif()

  add_test(NAME sygus_ut COMMAND sygus_ut)
  add_test(NAME sygus_mt COMMAND sygus_mt)

  add_custom_target(ut DEPENDS sygus_ut)
  add_custom_target(mt DEPENDS sygus_mt)
endif()
