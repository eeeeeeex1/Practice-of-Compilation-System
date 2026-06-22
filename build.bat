@echo off
REM ToyC Compiler Build Script for Windows

set CXX=g++
set CXXFLAGS=-std=c++20 -Wall -Wextra -pedantic
set TARGET=build\toyc.exe
set SOURCES=src\main.cpp src\Lexer.cpp src\Parser.cpp src\SemanticAnalyzer.cpp src\CodeGenerator.cpp

if not exist build mkdir build

echo Building ToyC Compiler...
%CXX% %CXXFLAGS% -o %TARGET% %SOURCES%

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b 1
)

echo Build successful: %TARGET%
exit /b 0