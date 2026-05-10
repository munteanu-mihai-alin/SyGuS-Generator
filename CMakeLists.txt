cmake_minimum_required(VERSION 3.21)

project(SyGuSGenerator LANGUAGES CXX)

option(SYGUS_BUILD_TESTS "Build parser unit and module tests" ON)
option(SYGUS_BUILD_SOLVER "Build the AST-based SyGuS solver targets" ON)
option(SYGUS_ENABLE_COVERAGE "Enable coverage instrumentation" OFF)
option(SYGUS_REQUIRE_CVC5 "Fail configure if cvc5 is unavailable" OFF)

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

if(SYGUS_BUILD_SOLVER)
  if(SYGUS_REQUIRE_CVC5)
    find_package(cvc5 CONFIG REQUIRED)
  else()
    find_package(cvc5 CONFIG QUIET)
  endif()

  add_library(sygus_solver
    src/sygus_solver.cpp
  )

  target_include_directories(sygus_solver
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

  target_link_libraries(sygus_solver
    PUBLIC
      sygus_parser
  )

  target_compile_features(sygus_solver PUBLIC cxx_std_20)

  if(cvc5_FOUND)
    target_link_libraries(sygus_solver PUBLIC cvc5::cvc5 cvc5::cvc5parser)
    target_compile_definitions(sygus_solver PUBLIC SYGUS_HAVE_CVC5=1)
  else()
    target_compile_definitions(sygus_solver PUBLIC SYGUS_HAVE_CVC5=0)
  endif()

  if(MSVC)
    target_compile_options(sygus_solver PRIVATE /W4 /permissive-)
  else()
    target_compile_options(sygus_solver PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  if(SYGUS_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sygus_solver PUBLIC -O0 -g --coverage)
    target_link_options(sygus_solver PUBLIC --coverage)
  endif()

  add_executable(sygus_solve
    src/sygus_solve_main.cpp
  )

  target_link_libraries(sygus_solve PRIVATE sygus_solver)
endif()

if(SYGUS_BUILD_TESTS)
  include(CTest)
  enable_testing()

  add_executable(sygus_ut
    tests/sygus_ut.cpp
  )
  target_link_libraries(sygus_ut PRIVATE sygus_parser)
  if(TARGET sygus_solver)
    target_link_libraries(sygus_ut PRIVATE sygus_solver)
  endif()
  target_compile_definitions(sygus_ut PRIVATE SYGUS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

  add_executable(sygus_mt
    tests/sygus_mt.cpp
  )
  target_link_libraries(sygus_mt PRIVATE sygus_parser)
  if(TARGET sygus_solver)
    target_link_libraries(sygus_mt PRIVATE sygus_solver)
  endif()
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
