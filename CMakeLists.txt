cmake_minimum_required(VERSION 3.29)
project(SygusCoding)

set(CMAKE_CXX_STANDARD 20)

set(INCLUDE_FOLDER
        ${CMAKE_SOURCE_DIR}/include)



set(CVC5_INCLUDE "$ENV{CVC5_INCLUDE}")
set(CVC5_LIB "$ENV{CVC5_LIB}")

message("INCLUDE_FOLDER: ${INCLUDE_FOLDER}")




macro(get_environment_variable VAR)
    if(DEFINED ENV{${VAR}})
        message("${VAR}: $ENV{${VAR}}")
    else()
        message("${VAR} is not defined")
    endif()
endmacro()

get_environment_variable(CVC5_INCLUDE)
get_environment_variable(CVC5_LIB)


include_directories(SygusCoding ${INCLUDE_FOLDER} ${CVC5_INCLUDE} ${CVC5_LIB} )
#include_directories(SygusCoding ${CVC_EXPORT})


include_directories(${CVC5_INCLUDE} incls)

message("incls: ${incls}")

add_executable(SygusCoding main.cpp
        ${INCLUDE_FOLDER}/bfs_strategy.hpp
        ${INCLUDE_FOLDER}/sa_strategy.hpp
        ${INCLUDE_FOLDER}/sygus_parser.hpp
        ${INCLUDE_FOLDER}/sygus_synthesizer.hpp
        ${INCLUDE_FOLDER}/synthesis_strategy.hpp

        ${incls}


        src/bfs_strategy.cpp
        src/sa_strategy.cpp
        src/sygus_parser.cpp
        src/sygus_synthesizer.cpp
        src/synthesis_strategy.cpp
        test.cpp
)
SET(CVC_LIB
    ${CVC5_LIB}\\libcvc5.a)
SET(CADICAL_LIB
        ${CVC5_LIB}\\libcadical.a)
SET(CVC_PARSER_LIB
        ${CVC5_LIB}\\libcvc5parser.a)
SET(POLY_CXX_LIB
        ${CVC5_LIB}\\libpicpolyxx.a)
SET(POLY_LIB
        ${CVC5_LIB}\\libpicpoly.a)

include_directories(SygusCoding Public "D:\\cvc5-Win64-static\\lib\\cmake\\cvc5")

set(CMAKE_FIND_DEBUG_MODE 1)
#find_package(cvc5 REQUIRED PATH "D:\\cvc5-Win64-static\\lib\\cmake\\cvc5")
find_package(cvc5 REQUIRED )

add_executable(test test.cpp)

#[[target_link_libraries(test PUBLIC "D:/cvc5-Win64-static/lib/libcvc5.a")]]
target_link_libraries(SygusCoding PUBLIC cvc5)

message("CVC_LIB: ${CVC_LIB}")

target_link_libraries(SygusCoding PUBLIC
        ${CVC_LIB}
        ${CVC_PARSER_LIB}
        ${CADICAL_LIB}
        ${POLY_CXX_LIB}
        ${POLY_LIB})



#[[
target_link_libraries(SygusCoding PUBLIC "D:/cvc5-Win64-static/lib/libcvc5.a")]]
