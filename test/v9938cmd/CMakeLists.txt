# The V9938 command engine: every command, mode and logical operation, through the ports.
set(PROGRAM v9938cmd_test)

add_executable(${PROGRAM} v9938cmd_test.c)
target_link_libraries(${PROGRAM} PRIVATE pico9918_core)

target_include_directories(${PROGRAM} PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}/src)
