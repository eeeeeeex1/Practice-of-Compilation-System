cmake_minimum_required(VERSION 3.16)
project(toyc_compiler LANGUAGES CXX)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /utf-8")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/Lexer.cpp
    src/Parser.cpp
    src/SemanticAnalyzer.cpp
    src/CompileTimeEvaluator.cpp
    src/CodeGenerator.cpp
)

set(HEADERS
    src/Token.h
    src/Lexer.h
    src/AST.h
    src/Parser.h
    src/SemanticAnalyzer.h
    src/CompileTimeEvaluator.h
    src/CodeGenerator.h
)

# Create executable
add_executable(toyc ${SOURCES} ${HEADERS})

# Release build by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
