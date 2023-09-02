cmake_minimum_required(VERSION 3.19..3.27)

project(Crew VERSION 0.1 LANGUAGES CXX)

# set cxx standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(external/googletest)
enable_testing()

add_subdirectory(external/fmt)
add_subdirectory(external/json)
add_subdirectory(src/app)
add_subdirectory(src/lib)
