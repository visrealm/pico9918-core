cmake_minimum_required(VERSION 3.12)

# Detect whether we're building for a Pico target
if(DEFINED PICO_BUILD OR DEFINED PICO_PLATFORM OR DEFINED PICO_SDK_PATH)
  set(VR_TMS_PICO_BUILD ON)
else()
  set(VR_TMS_PICO_BUILD OFF)
endif()

# Determine the assembly suffix for the TMS9900 core
if(PICO_PLATFORM STREQUAL "rp2040")
  set(VR_TMS_ASM_SUFFIX "_m0")
else()
  set(VR_TMS_ASM_SUFFIX "_m33")
endif()

# Core sources (always compiled)
set(VR_TMS_SOURCES vrEmuTms9918.c vrEmuTms9918Util.c)

# GPU sources — included when VR_EMU_TMS9918_MODE >= 1 (F18A or V9938)
if(NOT DEFINED VR_EMU_TMS9918_MODE)
  set(VR_EMU_TMS9918_MODE 0)
endif()

if(VR_EMU_TMS9918_MODE GREATER_EQUAL 1)
  list(APPEND VR_TMS_SOURCES
    gpu/vrEmuTms9918Gpu.c
    gpu/tms9900.c)
  if(VR_TMS_PICO_BUILD)
    list(APPEND VR_TMS_SOURCES
      gpu/platform/thumb9900${VR_TMS_ASM_SUFFIX}.S)
  endif()
endif()

if(VR_EMU_TMS9918_MODE EQUAL 2)
  list(APPEND VR_TMS_SOURCES modes/vrEmuTmsV9938.c)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/gpu/v9938cmd.c)
    list(APPEND VR_TMS_SOURCES gpu/v9938cmd.c)
  endif()
endif()

add_library(${LIBRARY} STATIC ${VR_TMS_SOURCES})

if(WIN32)
  if(BUILD_SHARED_LIBS)
    add_definitions(-DVR_TMS9918_EMU_COMPILING_DLL)
  endif()
endif()

target_include_directories(${LIBRARY} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${LIBRARY} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/gpu
  ${CMAKE_CURRENT_SOURCE_DIR}/modes)

target_compile_definitions(${LIBRARY} PRIVATE
  VR_EMU_TMS9918_MODE=${VR_EMU_TMS9918_MODE})

# Only link Pico-specific libraries when building for a Pico target
if(VR_TMS_PICO_BUILD)
  target_link_libraries(${LIBRARY} PRIVATE pico_stdlib hardware_dma)
  if(VR_EMU_TMS9918_MODE GREATER_EQUAL 1)
    target_link_libraries(${LIBRARY} PRIVATE hardware_flash)
  endif()
endif()
