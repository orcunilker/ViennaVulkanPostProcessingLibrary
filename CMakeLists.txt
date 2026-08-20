
cmake_minimum_required (VERSION 3.5.0)
project (
  "ViennaVulkanPostProcessingLibrary"
  VERSION 1.0
  DESCRIPTION "A post processing library for Vulkan."
  LANGUAGES CXX
)

# enforce C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release")

# the library uses Vulkan types, so the Vulkan SDK is needed
find_package(Vulkan REQUIRED)

# enable clang-tidy. disable to speed up compilation
#set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
#set(CMAKE_CXX_CLANG_TIDY clang-tidy -checks=readability-*;cppcoreguidelines-*;-header-filter=.*; -p build)


set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${GENERATED_DIR})

find_program(SLANGC slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)

# uebersetzt invert.slang nach SPIR-V und bettet es als uint32-Array ein
add_custom_command(
  OUTPUT ${GENERATED_DIR}/invert_spv.h
  COMMAND ${SLANGC} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/invert.slang
          -target spirv -profile spirv_1_3 -entry main -stage compute
          -source-embed-style u32 -source-embed-name invert_spv
          -o ${GENERATED_DIR}/invert_spv.h
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/invert.slang
  COMMENT "slangc invert.slang"
)

add_library(vvppl src/VVPPL.cpp ${GENERATED_DIR}/invert_spv.h)
target_include_directories(vvppl PUBLIC include PRIVATE ${GENERATED_DIR})
target_link_libraries(vvppl PUBLIC Vulkan::Vulkan)
target_compile_features(vvppl PUBLIC cxx_std_20)

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
  include(CTest)
  add_subdirectory(tests)
endif ()
