#
# Copyright (c) 2026 Toshimiyu 
#
# Released under the MIT license
# https://opensource.org/licenses/mit-license.php
#

# CMSIS-DSP 必要ソースを選択（使う関数に応じて最小限にする）
file(GLOB DSP_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/FilteringFunctions/arm_fir_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/FilteringFunctions/arm_fir_init_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/FilteringFunctions/arm_fir_interpolate_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/FilteringFunctions/arm_fir_interpolate_init_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/TransformFunctions/arm_cfft_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/TransformFunctions/arm_cfft_init_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/TransformFunctions/arm_cfft_radix8_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/TransformFunctions/arm_rfft_fast_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/TransformFunctions/arm_rfft_fast_init_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/TransformFunctions/arm_bitreversal.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/TransformFunctions/arm_bitreversal2.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/CommonTables/arm_common_tables.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/CommonTables/arm_const_structs.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/SupportFunctions/arm_q31_to_float.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/BasicMathFunctions/arm_add_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/BasicMathFunctions/arm_scale_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/ComplexMathFunctions/arm_cmplx_mult_cmplx_f32.c
        )

# TinyUSB オーディオクラスソースの選択
file(GLOB TUSB_SRC
${CMAKE_CURRENT_SOURCE_DIR}/../tinyusb/src/device/usbd.c
${CMAKE_CURRENT_SOURCE_DIR}/../tinyusb/src/device/usbd_control.c
${CMAKE_CURRENT_SOURCE_DIR}/../tinyusb/src/common/tusb_fifo.c
${CMAKE_CURRENT_SOURCE_DIR}/../tinyusb/src/class/audio/audio_device.c
)

# Add executable. Default name is the project name, version 0.1
add_executable(Pico2_UAC2_USBDDC
        main.c
        general_func.c
        usb_device_control.c
        uac2_callbacks.c 
        usb_descriptors.c
        core1.c
        i2s_pio_interface.c
        transmit_to_dac.c
        upsampling_coef.c
        fft_fir_coef.c
        upsampling.c
        fft_fir_core0.c
        fft_fir_core1.c
        ringbuffer.c
        debug_with_gpio.c
        debug_gauge.c
        ess_specific.c
        nonblocking_i2c.c
        display_rta.c
        spectrum.c
        ${DSP_SRC}
        ${TUSB_SRC}
)

pico_set_program_name(Pico2_UAC2_USBDDC "Pico2_UAC2_USBDDC")
pico_set_program_version(Pico2_UAC2_USBDDC "0.1")

# Generate PIO header
pico_generate_pio_header(Pico2_UAC2_USBDDC ${CMAKE_CURRENT_LIST_DIR}/i2s.pio)

# Add the standard library to the build
target_link_libraries(Pico2_UAC2_USBDDC
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(Pico2_UAC2_USBDDC PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../pico-extras
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_LIST_DIR}/../CMSIS/Core/Include
        ${CMAKE_CURRENT_LIST_DIR}/../CMSIS/DSP/Include
        ${CMAKE_CURRENT_LIST_DIR}/../tinyusb/src
        ${CMAKE_CURRENT_LIST_DIR}/../tinyusb/src/class/audio
        ${CMAKE_CURRENT_LIST_DIR}/../tinyusb/src/class/audio20
        ${CMAKE_CURRENT_LIST_DIR}/../tinyusb/src/class/audio20/uaudio20
        )

# Add any user requested libraries
target_link_libraries(Pico2_UAC2_USBDDC 
        hardware_pio 
        tinyusb_device
        pico_multicore
        hardware_clocks
        hardware_dma
        hardware_pwm
        hardware_uart
        hardware_i2c
        pico_float
        hardware_spi
        pico_unique_id
)

pico_set_float_implementation(Pico2_UAC2_USBDDC pico_vfp)

target_link_options(Pico2_UAC2_USBDDC PRIVATE
        -flto
        "LINKER:-u,__wrap_printf"
        "LINKER:-u,__wrap_puts"
        "LINKER:-u,__wrap_putchar"
        "LINKER:-u,__wrap_getchar"
        "LINKER:-u,__wrap_vprintf"
        "LINKER:-u,__wrap_powf"
        "LINKER:-u,__wrap_log10f"
)

pico_add_extra_outputs(Pico2_UAC2_USBDDC)