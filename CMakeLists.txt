cmake_minimum_required(VERSION 3.20)
project(grammar_stress CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

find_package(Threads REQUIRED)

add_executable(grammar-stress src/main.cpp)
target_link_libraries(grammar-stress PRIVATE Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(grammar-stress PRIVATE -Wall -Wextra -Wpedantic)
endif()
