cmake_minimum_required(VERSION 3.20)
project(EnginePlugins LANGUAGES CXX)

# Require C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_PLUGIN_CINEMACHINE "Build Cinemachine camera tracking addon plugin" ON)

if (BUILD_PLUGIN_CINEMACHINE)
    add_subdirectory(cinemachine)
    add_subdirectory(AStar)
    add_subdirectory(skymo)
endif()

