#
# Copyright (c) 2026 Toshimiyu 
#
# Released under the MIT license
# https://opensource.org/licenses/mit-license.php
#

# CMSIS-DSP 必要ソースを選択（使う関数に応じて最小限にする）
file(GLOB DSP_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/TransformFunctions/arm_cfft_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/TransformFunctions/arm_cfft_init_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/TransformFunctions/arm_cfft_radix8_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/TransformFunctions/arm_rfft_fast_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/TransformFunctions/arm_rfft_fast_init_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/TransformFunctions/arm_bitreversal.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/TransformFunctions/arm_bitreversal2.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/CommonTables/arm_common_tables.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/CommonTables/arm_const_structs.c
        )

# Add executable. Default name is the project name, version 0.1
add_executable(Pico2_UAC2_USBDDC_VerFinal_11FFT_NoOverSampling
        main.c
        general_func.c
        usb_device_control.c
        uac2_callbacks.c 
        usb_descriptors.c
        core1.c
        i2s_pio_interface.c
        transmit_to_dac.c
        ringbuffer.c
        debug_with_gpio.c
        debug_gauge.c
        display_rta.c
        spectrum.c
        ${DSP_SRC}
)

# UART console is unused; do not pull pico_stdio_uart/hardware_uart into the image.
pico_enable_stdio_uart(Pico2_UAC2_USBDDC_VerFinal_11FFT_NoOverSampling 0)

# 起動時にプログラムをSRAMへコピーして実行する（RP2350）
pico_set_binary_type(Pico2_UAC2_USBDDC_VerFinal_11FFT_NoOverSampling copy_to_ram)

pico_set_program_name(Pico2_UAC2_USBDDC_VerFinal_11FFT_NoOverSampling "Pico2_UAC2_USBDDC_VerFinal_11FFT_NoOverSampling")
pico_set_program_version(Pico2_UAC2_USBDDC_VerFinal_11FFT_NoOverSampling "0.1")

# Generate PIO header
pico_generate_pio_header(Pico2_UAC2_USBDDC_VerFinal_11FFT_NoOverSampling ${CMAKE_CURRENT_LIST_DIR}/i2s.pio)

# Add the standard library to the build
target_link_libraries(Pico2_UAC2_USBDDC_VerFinal_11FFT_NoOverSampling
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(Pico2_UAC2_USBDDC_VerFinal_11FFT_NoOverSampling PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../pico-extras
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_LIST_DIR}/../CMSIS/Core/Include
        ${CMAKE_CURRENT_LIST_DIR}/../CMSIS/DSP/Include
        )

# Add any user requested libraries
target_link_libraries(Pico2_UAC2_USBDDC_VerFinal_11FFT_NoOverSampling 
        hardware_pio 
        tinyusb_device
        pico_multicore
        hardware_clocks
        hardware_dma
        hardware_pwm
        pico_float
        hardware_spi
        pico_unique_id
)

pico_set_float_implementation(Pico2_UAC2_USBDDC_VerFinal_11FFT_NoOverSampling pico_vfp)

target_link_options(Pico2_UAC2_USBDDC_VerFinal_11FFT_NoOverSampling PRIVATE
        "LINKER:-u,__wrap_printf"
        "LINKER:-u,__wrap_puts"
        "LINKER:-u,__wrap_putchar"
        "LINKER:-u,__wrap_getchar"
        "LINKER:-u,__wrap_vprintf"
        "LINKER:-u,__wrap_powf"
        "LINKER:-u,__wrap_log10f"
)

pico_add_extra_outputs(Pico2_UAC2_USBDDC_VerFinal_11FFT_NoOverSampling)
