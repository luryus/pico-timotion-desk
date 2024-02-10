cmake_minimum_required(VERSION 3.13)

# Use Pico W by default
set(PICO_BOARD pico_w)

include(pico-sdk/pico_sdk_init.cmake)

project(desk)

pico_sdk_init()

add_executable(desk
    main.c
)

target_link_libraries(desk pico_stdlib)

pico_add_extra_outputs(desk)