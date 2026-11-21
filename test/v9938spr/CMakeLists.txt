# V9938 sprites, both modes, driven through the host ports and checked on the rendered line.
set(PROGRAM v9938spr_test)

add_executable(${PROGRAM} v9938spr_test.c)
target_link_libraries(${PROGRAM} PRIVATE pico9918_core)

target_include_directories(${PROGRAM} PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}/src)
