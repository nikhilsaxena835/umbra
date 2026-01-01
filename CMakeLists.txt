cmake_minimum_required(VERSION 3.20)
project(umbra LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define project directories early
set(PROJECT_ROOT ${CMAKE_SOURCE_DIR})
set(INCLUDE_DIR ${PROJECT_ROOT}/include)
set(LIB_DIR ${PROJECT_ROOT}/lib)
set(SOURCE_DIR ${PROJECT_ROOT}/src)
set(SHADER_DIR ${PROJECT_ROOT}/shaders)
set(ASSETS_DIR ${PROJECT_ROOT}/assets)

# Source files
set(SOURCES
    ${SOURCE_DIR}/main.cpp
    ${SOURCE_DIR}/core/vulkan_engine.cpp
    ${SOURCE_DIR}/core/pipeline.cpp
    ${SOURCE_DIR}/core/buffer_manager.cpp
    ${SOURCE_DIR}/core/shader_manager.cpp
    ${SOURCE_DIR}/processing/frame_processor.cpp
    ${SOURCE_DIR}/processing/object_detector.cpp
    ${SOURCE_DIR}/processing/mask_generator.cpp
    ${SOURCE_DIR}/io/video_io.cpp
    ${SOURCE_DIR}/io/ppm_handler.cpp
)

# Define executable target
add_executable(${PROJECT_NAME} ${SOURCES})

# Find Vulkan
set(VULKAN_SDK ${PROJECT_ROOT})
find_library(VULKAN_LIBRARY NAMES vulkan vulkan-1 PATHS ${LIB_DIR}/vulkan)
find_path(VULKAN_INCLUDE_DIR NAMES vulkan/vulkan.h PATHS ${INCLUDE_DIR}/vulkan)

if(NOT VULKAN_LIBRARY OR NOT VULKAN_INCLUDE_DIR)
    message(FATAL_ERROR "Vulkan SDK not found in ${VULKAN_SDK}")
endif()

# Find ONNX Runtime
find_library(ONNXRUNTIME_LIB onnxruntime PATHS ${PROJECT_ROOT}/usr/local/lib)
include_directories(${PROJECT_ROOT}/usr/local/include)

# Include directories
include_directories(
    ${VULKAN_INCLUDE_DIR}
    ${SOURCE_DIR}
)

# Link all required libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${VULKAN_LIBRARY}
    ${ONNXRUNTIME_LIB}
)

# Compiler flags
if(UNIX)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
elseif(WIN32)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
endif()

# Find glslc
find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install the Vulkan SDK.")
endif()

# Compile shaders
set(COMPILED_SHADER_DIR ${PROJECT_ROOT}/bin/shaders)
file(MAKE_DIRECTORY ${COMPILED_SHADER_DIR})
file(GLOB_RECURSE SHADER_FILES "${SHADER_DIR}/*.comp")

foreach(SHADER_FILE ${SHADER_FILES})
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    set(OUTPUT_SPIRV ${COMPILED_SHADER_DIR}/${SHADER_NAME}.spv)
    
    add_custom_command(
        OUTPUT ${OUTPUT_SPIRV}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER_FILE} -o ${OUTPUT_SPIRV}
        DEPENDS ${SHADER_FILE}
        COMMENT "Compiling ${SHADER_FILE} to SPIR-V"
    )
    list(APPEND COMPILED_SHADERS ${OUTPUT_SPIRV})
endforeach()

add_custom_target(CompileShaders DEPENDS ${COMPILED_SHADERS})
add_dependencies(${PROJECT_NAME} CompileShaders)

# Installation targets
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(DIRECTORY ${COMPILED_SHADER_DIR}/ DESTINATION shaders)
install(DIRECTORY ${ASSETS_DIR}/ DESTINATION assets)
