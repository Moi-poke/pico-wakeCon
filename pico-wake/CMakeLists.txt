# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if(EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)
project(pico-wake C CXX ASM)
pico_sdk_init()

# pico-wake.c is the empty VS Code template and is intentionally not compiled.
add_executable(pico-wake
    pico_wake.c
    switch2_wake_core.c
    switch2_wake_adv.c
    switch2_wake_input.c
    switch2_wake_store.c
    switch2_wake_att_db.c
    switch2_wake_cmd.c
    switch2_wake_capture.c
    switch2_wake_util.c
    app_config.c
    app_common.c
    app_ports_adv.c
    app_ports_store.c
    app_ports_cmd.c
    app_ports_att.c
    app_att.c
)

pico_set_program_name(pico-wake "pico-wake")
pico_set_program_version(pico-wake "0.2-wake")
pico_enable_stdio_uart(pico-wake 0)
pico_enable_stdio_usb(pico-wake 1)

target_include_directories(pico-wake PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(pico-wake
    pico_stdlib
    hardware_uart
    pico_btstack_ble
    pico_btstack_cyw43
    pico_cyw43_arch_none
    pico_btstack_flash_bank
)

pico_add_extra_outputs(pico-wake)
