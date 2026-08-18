cmake_minimum_required(VERSION 3.20)
project(EngineWorkspace VERSION 1.4.3 LANGUAGES CXX)

# Require C++20 standard globally
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set forgiving timeouts
set(ENV{GIT_HTTP_LOW_SPEED_LIMIT} 10000)
set(ENV{GIT_HTTP_LOW_SPEED_TIME} 12000)

# --- Dependencies Configuration (Shared across projects) ---

# 1. Vulkan SDK
if(MSVC AND (CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64"))
    if(DEFINED ENV{VULKAN_SDK})
        set(Vulkan_LIBRARY "$ENV{VULKAN_SDK}/Lib-ARM64/vulkan-1.lib" CACHE FILEPATH "Vulkan Library Path override for ARM64" FORCE)
        message(STATUS "Overriding Vulkan library path for ARM64: ${Vulkan_LIBRARY}")
    endif()
endif()

find_package(Vulkan REQUIRED)
message(STATUS "Found Vulkan SDK: ${Vulkan_LIBRARIES}")

# 2. FetchContent for GLFW (v3.4) and GLM (v1.0.1)
include(FetchContent)

# Fetch GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "")
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(glfw)

# Fetch GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

# Fetch cgltf
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.14
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cgltf)

# Fetch stb (for stb_image)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

# Fetch miniaudio
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(miniaudio)

# Fetch Tracy Profiler (v0.14)
FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG v0.14.0
    GIT_SHALLOW TRUE
)
set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy Profiler" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "On-demand Tracy profiling" FORCE)
FetchContent_MakeAvailable(tracy)

# --- Add Subdirectories ---
# sandbox_game is now a standalone project — build it with build_game.bat
add_subdirectory(engine)
add_subdirectory(plugins)

message(STATUS "Configured engine workspace subdirectories")

