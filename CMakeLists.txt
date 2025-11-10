cmake_minimum_required(VERSION 3.23)

project(Vulkan3)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS FALSE)

add_executable(Engine main.cpp)