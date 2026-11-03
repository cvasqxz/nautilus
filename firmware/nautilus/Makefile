# Nautilus (Qu-Bit) — firmware v1.2.0 reconstruction
# The original's coreJSON assert uses the path "../../libs/coreJSON/source/core_json.c",
# so the project lives two levels below the folder that contains libs/.

TARGET = nautilus

# Original toolchain: GNU Arm Embedded 10-2020-q4-major (GCC 10.2.1), the Daisy Toolchain one.
# If it is not in the PATH: make GCC_PATH=/path/to/gcc-arm-none-eabi-10-2020-q4-major/bin

# Module code: a single C++ translation unit
CPP_SOURCES = main.cpp

# coreJSON is built inside the project (it comes before the startup code in the binary)
C_SOURCES = ../../libs/coreJSON/source/core_json.c
C_INCLUDES = -I../../libs/coreJSON/source/include

USE_FATFS = 1
APP_TYPE = BOOT_SRAM
LDSCRIPT = STM32H750IB_sram_nautilus.lds
# -O3 without full -ffast-math: the module code uses vcmpe, and expf is not hoisted out of
# loops (-fmath-errno is still on).
# -funsafe-math-optimizations: the original folds x*0.995 < 0.0003 into x < 0.000301508.
# -ffinite-math-only: the clamp in LedDriverPca9685::SetLed comes out as vminnm/vmaxnm.
# DaisySP is built with -ffast-math (separately, see README.md).
OPT = -O3 -funsafe-math-optimizations -ffinite-math-only

LIBDAISY_DIR = ../../libs/libDaisy
DAISYSP_DIR = ../../libs/DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
