cmake_minimum_required(VERSION 4.2)
project(Compiler_Impl)

set(CMAKE_CXX_STANDARD 20)

include_directories(${CMAKE_SOURCE_DIR}/include)

set(SOURCES
    main.cpp
    src/semantic/Symbol.cpp
    src/semantic/SymbolTable.cpp
    src/semantic/ConstEvaluator.cpp
    src/semantic/SemanticAnalyzer.cpp
)

add_executable(Compiler_Impl ${SOURCES})