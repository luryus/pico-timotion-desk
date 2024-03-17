cmake_minimum_required(VERSION 3.13)

# Use Pico W by default
#set(PICO_BOARD pico_w)

include(pico-sdk/pico_sdk_init.cmake)

project(desk)

pico_sdk_init()

set(SOURCE_FILES
  main.cc deskstate.cc deskcmd.cc
)

add_executable(desk
    ${SOURCE_FILES}
)
pico_enable_stdio_usb(desk 1)

set_source_files_properties(${SOURCE_FILES}
    PROPERTIES COMPILE_OPTIONS -Wall -Wpedantic -Wextra
)

target_link_libraries(desk
    pico_stdlib
    hardware_pio
    hardware_clocks)

pico_add_extra_outputs(desk)

pico_generate_pio_header(desk ${CMAKE_CURRENT_LIST_DIR}/keys.pio)