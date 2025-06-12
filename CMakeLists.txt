set(CMAKE_TOOLCHAIN_FILE "C:/raylib/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
cmake_minimum_required(VERSION 3.31)
project(ECG_Monitor)

set(CMAKE_CXX_STANDARD 20)

find_package(raylib CONFIG REQUIRED)

add_executable(ECG_Monitor main.cpp)

target_link_libraries(ECG_Monitor PRIVATE raylib)