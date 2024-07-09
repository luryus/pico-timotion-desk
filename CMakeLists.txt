cmake_minimum_required(VERSION 3.13)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules")

include(pico-sdk/pico_sdk_init.cmake)
include(GetGitRevisionDescription)

project(desk VERSION 0.1)

pico_sdk_init()

add_compile_definitions(U8X8_WITH_USER_PTR=1)
set(U8G2_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/u8g2)
set(U8G2_SRC_PATH ${U8G2_ROOT_PATH}/csrc)
file(GLOB U8G2_SRC_FILES CONFIGURE_DEPENDS ${U8G2_SRC_PATH}/*.c)


get_git_head_revision(GIT_REFSPEC GIT_SHA1)
configure_file("version.h.in" "version.h" @ONLY)

set(SOURCE_FILES
  main.cc deskstate.cc deskcmd.cc disp.cc movestatemachine.cc buttonsm.cc
  memslots.cc
  ${U8G2_SRC_FILES}
)

add_executable(desk
    ${SOURCE_FILES}
)
pico_enable_stdio_usb(desk 1)

set_source_files_properties(${SOURCE_FILES}
    PROPERTIES COMPILE_OPTIONS -Wall -Wpedantic -Wextra
)
target_include_directories(desk
    PRIVATE ${U8G2_SRC_PATH}
)
target_link_libraries(desk
    pico_stdlib
    pico_multicore
    pico_flash
    pico_util
    hardware_pio
    hardware_i2c
    hardware_exception
    hardware_clocks)

pico_add_extra_outputs(desk)

pico_set_linker_script(desk ${CMAKE_CURRENT_LIST_DIR}/memmap_custom.ld)

pico_generate_pio_header(desk ${CMAKE_CURRENT_LIST_DIR}/keys.pio)