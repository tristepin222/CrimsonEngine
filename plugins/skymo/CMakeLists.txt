cmake_minimum_required(VERSION 3.20)
project(AtmospherePlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Custom command to generate reflection boilerplate
if(NOT CMAKE_GENERATOR_PLATFORM MATCHES "ARM" AND NOT CMAKE_CROSSCOMPILING)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/src/generated_reflection.cpp"
        COMMAND $<TARGET_FILE:reflection_generator> "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src/generated_reflection.cpp"
        DEPENDS reflection_generator "${CMAKE_CURRENT_SOURCE_DIR}/include/AtmosphereComponent.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/include/VolumetricFogComponent.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/include/VolumetricCloudComponent.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/include/WeatherComponent.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/include/AtmosphereSystem.hpp"
        COMMENT "[Reflection Generator] Parsing annotations and generating registration code for Atmosphere..."
    )
else()
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/src/generated_reflection.cpp"
        COMMAND ${CMAKE_COMMAND} -E echo "[Reflection Generator] Skipping generation for Atmosphere (ARM/Cross-compilation, using pre-generated file)"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/AtmosphereComponent.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/include/VolumetricFogComponent.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/include/VolumetricCloudComponent.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/include/WeatherComponent.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/include/AtmosphereSystem.hpp"
        COMMENT "[Reflection Generator] Skipping generation for Atmosphere..."
    )
endif()

find_program(Vulkan_GLSLC NAMES glslc)

set(SKYMO_SHADERS_SPV "")
function(compile_skymo_shader SHADER_PATH)
    get_filename_component(SHADER_NAME ${SHADER_PATH} NAME)
    set(SPV_PATH "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    set(SDK_SPV_PATH "${CMAKE_SOURCE_DIR}/sdk/shaders/${SHADER_NAME}.spv")
    
    add_custom_command(
        OUTPUT ${SPV_PATH}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/sdk/shaders"
        COMMAND ${Vulkan_GLSLC} ${SHADER_PATH} -o ${SPV_PATH}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SPV_PATH} ${SDK_SPV_PATH}
        DEPENDS ${SHADER_PATH}
        COMMENT "Compiling Skymo shader: ${SHADER_NAME} -> ${SHADER_NAME}.spv"
    )
    list(APPEND SKYMO_SHADERS_SPV ${SPV_PATH})
    set(SKYMO_SHADERS_SPV ${SKYMO_SHADERS_SPV} PARENT_SCOPE)
endfunction()

compile_skymo_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/transmittance.comp")
compile_skymo_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/multiscatter.comp")
compile_skymo_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/skyview.comp")
compile_skymo_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky_composition.vert")
compile_skymo_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky_composition.frag")
compile_skymo_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/aerial_perspective.comp")
compile_skymo_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/aerial_composition.vert")
compile_skymo_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/aerial_composition.frag")
compile_skymo_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloud_noise.comp")
compile_skymo_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather_particles.vert")
compile_skymo_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather_particles.frag")
compile_skymo_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather_lightning.vert")
compile_skymo_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather_lightning.frag")

add_custom_target(skymo_shaders ALL DEPENDS ${SKYMO_SHADERS_SPV})

set(PLUGIN_SOURCES
    src/generated_reflection.cpp
    src/AtmosphereSystem.cpp
    src/AtmosphereRenderFeature.cpp
)

add_library(skymo_plugin SHARED ${PLUGIN_SOURCES})
add_dependencies(skymo_plugin skymo_shaders)

# Public includes
target_include_directories(skymo_plugin PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${stb_SOURCE_DIR}
)

# Link against the engine shared library and third party dependencies
target_link_libraries(skymo_plugin PRIVATE
    engine
    imgui
    glm::glm
    glfw
)


# Post-Build Step: Copy DLL to sdk/plugins/ so build_game.bat can pick it up
set(PLUGIN_SDK_OUTPUT "${CMAKE_SOURCE_DIR}/sdk/plugins")
add_custom_command(TARGET skymo_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_SDK_OUTPUT}"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:skymo_plugin> "${PLUGIN_SDK_OUTPUT}/"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_SDK_OUTPUT}/skymo/assets/textures"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/textures" "${PLUGIN_SDK_OUTPUT}/skymo/assets/textures"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_SDK_OUTPUT}/skymo/assets/sounds"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/sounds" "${PLUGIN_SDK_OUTPUT}/skymo/assets/sounds"
    COMMENT "Copying Atmosphere plugin DLL and assets to sdk/plugins/skymo/..."
)

