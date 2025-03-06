cmake_minimum_required(VERSION 3.10)

# Project name - use a simple name instead of path
project(usb_payload C CXX ASM)

# Set the output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Compiler toolchain settings for cross-compilation
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Tell CMake we're cross-compiling
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Define the toolchain executables
set(TOOLCHAIN_PREFIX arm-none-eabi-)
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)  # Use GCC for assembly too
set(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_OBJDUMP ${TOOLCHAIN_PREFIX}objdump)
set(CMAKE_SIZE ${TOOLCHAIN_PREFIX}size)
set(CMAKE_AR ${TOOLCHAIN_PREFIX}gcc-ar)
set(CMAKE_RANLIB ${TOOLCHAIN_PREFIX}gcc-ranlib)

# Prevent CMake from testing the compilers
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)
set(CMAKE_ASM_COMPILER_FORCED TRUE)

# Target-specific options
set(CPU_FLAGS "-mthumb -march=armv7e-m -mtune=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard")
add_compile_definitions(STM32H730xx)

# Compiler flags
set(CMAKE_C_FLAGS "${CPU_FLAGS} -O0")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_ASM_FLAGS "${CPU_FLAGS}")

# Debug build settings
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g)
    add_link_options(-g)
endif()

# Linker settings
set(CMAKE_EXE_LINKER_FLAGS "${CPU_FLAGS} -specs=nano.specs -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,-T${CMAKE_SOURCE_DIR}/link.ld -Wl,--no-warn-rwx-segments")

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/source
    ${CMAKE_SOURCE_DIR}/STM32CubeH7/Drivers/CMSIS/Include
    ${CMAKE_SOURCE_DIR}/STM32CubeH7/Drivers/CMSIS/Device/ST/STM32H7xx/Include
    ${CMAKE_SOURCE_DIR}/STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Inc
)

# Source files
file(GLOB_RECURSE APP_SOURCES 
    "source/*.c"
    "source/*.cpp"
    "source/*.s"
    "source/libalarmo/*.c"
    "source/libalarmo/*.cpp"
    "source/libalarmo/*.s"
)

# STM32 HAL source files
set(HAL_SOURCES
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_adc_ex.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_adc.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_adc.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma_ex.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_dma.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_gpio.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_mdma.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_mdma.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_pwr.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_rcc.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sram.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_fmc.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim_ex.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_tim.c
    STM32CubeH7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c
)

# Create the executable
add_executable(${CMAKE_PROJECT_NAME} ${APP_SOURCES} ${HAL_SOURCES})

# Link libraries
target_link_libraries(${CMAKE_PROJECT_NAME} c m nosys)

# Post-build commands to create binary file
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${CMAKE_PROJECT_NAME}${CMAKE_EXECUTABLE_SUFFIX} ${CMAKE_PROJECT_NAME}.bin
    COMMENT "Creating binary file ${CMAKE_PROJECT_NAME}.bin"
)

# Add encryption step
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND python ${CMAKE_SOURCE_DIR}/ciphtool.py encrypt ${CMAKE_PROJECT_NAME}.bin a.bin
    COMMENT "Encrypting to a.bin"
)

# Custom target for cleaning the build
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/*
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}${CMAKE_EXECUTABLE_SUFFIX}
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/a.bin
    COMMENT "Cleaning build directory"
)

# PNG to header conversion (if needed)
file(GLOB PNG_FILES "${CMAKE_SOURCE_DIR}/data/*.png")
foreach(PNG_FILE ${PNG_FILES})
    get_filename_component(PNG_NAME ${PNG_FILE} NAME_WE)
    set(HEADER_FILE "${CMAKE_BINARY_DIR}/${PNG_NAME}_png.h")
    
    add_custom_command(
        OUTPUT ${HEADER_FILE}
        COMMAND python ${CMAKE_SOURCE_DIR}/imgconv.py ${PNG_FILE} > ${HEADER_FILE}
        DEPENDS ${PNG_FILE}
        COMMENT "Converting ${PNG_NAME}.png to header"
    )
    
    set_source_files_properties(${HEADER_FILE} PROPERTIES GENERATED TRUE)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${HEADER_FILE})
endforeach()