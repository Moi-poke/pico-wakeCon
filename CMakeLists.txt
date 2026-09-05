# pico-wakecon: Switch2 wake (BLE beacon) + Switch1 Pro Controller (Classic)
# on a single Pico 2 W. Capture Joy-Con wake adv, replay it, then act as
# Switch 1 Pro Controller over Classic Bluetooth.

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
project(pico-wakecon C CXX ASM)
pico_sdk_init()

add_executable(pico-wakecon
    src/main.c
    src/cap.c
    src/spi.c
    src/hid.c
    src/store.c
    src/link.c
    src/ui.c
)

pico_set_program_name(pico-wakecon "pico-wakecon")
pico_set_program_version(pico-wakecon "0.1")

# stdio goes to USB CDC. UART0 (GP0/GP1) is driven directly for logs/commands.
pico_enable_stdio_uart(pico-wakecon 0)
pico_enable_stdio_usb(pico-wakecon 1)

target_include_directories(pico-wakecon PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(pico-wakecon
    pico_stdlib
    hardware_uart
    pico_btstack_classic
    pico_btstack_ble
    pico_btstack_cyw43
    pico_cyw43_arch_none
    pico_unique_id
    pico_btstack_flash_bank
)

pico_add_extra_outputs(pico-wakecon)
