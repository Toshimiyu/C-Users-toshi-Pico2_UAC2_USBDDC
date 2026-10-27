#
# Copyright (c) 2026 Toshimiyu 
#
# Released under the MIT license
# https://opensource.org/licenses/mit-license.php
#

# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

# Compiler Optimization
add_compile_options(-Ofast -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -Wall)

# Keep the RP2350 QMI flash clock at clk_sys / 3 from boot stage 2 onward.
add_compile_definitions(PICO_FLASH_SPI_CLKDIV=3)

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
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

# set boardtype
set(PICO_BOARD pico2 CACHE STRING "Board type")

# UAC2 (TUD_AUDIO20_* API) には新しめの TinyUSB が必要。リポジトリ直下の tinyusb/ があれば
# SDK 同梱版の代わりにそれを使う（-DPICO_TINYUSB_PATH=... や環境変数での指定も可）。
if(NOT PICO_TINYUSB_PATH AND NOT DEFINED ENV{PICO_TINYUSB_PATH} AND EXISTS ${CMAKE_CURRENT_LIST_DIR}/tinyusb)
    set(PICO_TINYUSB_PATH ${CMAKE_CURRENT_LIST_DIR}/tinyusb)
endif()

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)
# include(example_auto_set_url.cmake)

project(Pico2_UAC2_USBDDC_VerFinal_11FFT_NoOverSampling C CXX ASM)

# Use bundled pico‑extras from the repository
# add_subdirectory(pico-extras)
# set(PICO_EXTRAS_PATH ${CMAKE_CURRENT_SOURCE_DIR}/pico-extras)
# include(pico_extras_import.cmake)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add Source Directory
add_subdirectory(src)

# Compile Definitions
# (pico-extras usb_device / LUFA 用の PICO_USBDEV_* 定義は TinyUSB 移行により不要)
target_compile_definitions(Pico2_UAC2_USBDDC_VerFinal_11FFT_NoOverSampling PRIVATE
    AUDIO_FREQ_MAX=192000

    # USB bring-up level: 0 = USB off (original boot flow only) / 1 = TinyUSB enumeration only / 2 = full
    USB_BRINGUP_LEVEL=2

    # Core0/Core1 stack 2KB -> 4KB (TFT drawing alone needs ~1.4KB; USB/timer ISRs nest on top of it)
    PICO_STACK_SIZE=0x1000
    PICO_CORE1_STACK_SIZE=0x1000
)
