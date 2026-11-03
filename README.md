# Nautilus firmware reconstruction

A source-level reconstruction of the firmware for the **Qu-Bit Nautilus** (v1.2.0), a
stereo delay Eurorack module built on the Electrosmith Daisy Seed (STM32H750).

The code was rebuilt from the released binary (`nautilus.bin`). It targets the same
toolchain, compiler flags, libraries and memory layout as the original. All globals sit
at the original addresses, and most comments in the source give the address of the
original function or object they correspond to (`@0x2400xxxx`).

The resulting image is not byte-identical to the official one. Some differences in code
generation remain: register allocation, block ordering, inlining and unrolling decisions.
In differential emulation against the official binary, though, the outputs match bit for
bit: audio, the Sonar DAC output, the LED buffers, RAM, the settings written to QSPI and
the operations on the USB drive.

This is an independent project. It is not affiliated with or endorsed by Qu-Bit
Electronix.

## Layout

```
firmware/nautilus/   module source, Makefile and linker script
libs/libDaisy/       libDaisy v7.1.0 (submodule)
libs/DaisySP/        DaisySP dc448d4 (submodule)
libs/coreJSON/       coreJSON a0cd612 (submodule)
patches/             changes the firmware needs in libDaisy and DaisySP
```

The project sits two levels below `libs/` on purpose. The original binary embeds the
path `../../libs/coreJSON/source/core_json.c` in a coreJSON assert.

### Source files

| File | Contents |
|---|---|
| `main.cpp` | Globals, audio and DAC callbacks, tap tempo, settings, USB host callbacks and `main` |
| `nautilus_engine.h` | DSP engine: voices, delay and feedback modes, purge, mix and reverb routing |
| `pitch_tap.h` | Delay tap with pitch shift (Shimmer / De-Shimmer) |
| `reverser.h` | Reverse reader |
| `chroma.h` | Chroma effects in the feedback path |
| `freeze_looper.h` | Looper for the "Buffer Locked Freeze" |
| `reverb.h` | Reverb, ported from Mutable Instruments Rings |
| `clock_lfos.h` | Clock phases for the Sonar output and the LEDs |
| `nautilus_hw.h` | Module hardware: controls, CV calibration, LED drivers |
| `ui.h` | Main and calibration pages, LED drawing and control handling |
| `options.h` | Loading of `options.json` from the USB drive |
| `usb_drive.h` | USB drive mounting state machine |

### Library patches

- **libDaisy:**
  - SDRAM remapped to `0x60000000` (FMC bank swap), with the matching MPU region;
  - the USB host driver from libDaisy `4e73c969`, which the module uses;
  - `Color` constructors, setters, `operator+` and `Blend`.
- **DaisySP:** `Phasor` caches `2π/sample_rate`, accepts negative frequencies (from the
  `fix/pitch_shift` branch) and gains a `SetPhase` method.

## Building

### Requirements

- **GNU Arm Embedded Toolchain 10-2020-q4-major** (GCC 10.2.1), the one shipped with the
  Daisy Toolchain. Other versions build too, but the generated code drifts further from
  the original.
- `make`, `git`, and `dfu-util` for flashing.

### Steps

```sh
git clone --recurse-submodules https://github.com/cvasqxz/nautilus
cd nautilus

# If the toolchain is not in the PATH, point GCC_PATH to its bin/ directory
export GCC_PATH=/path/to/gcc-arm-none-eabi-10-2020-q4-major/bin

# 1. libDaisy, patched
cd libs/libDaisy
git apply ../../patches/libDaisy.patch
make GCC_PATH=$GCC_PATH
cd ../..

# 2. DaisySP, patched and built with -ffast-math
cd libs/DaisySP
git apply ../../patches/DaisySP.patch
make GCC_PATH=$GCC_PATH OPT="-O3 -ffast-math"
cd ../..

# 3. Firmware
cd firmware/nautilus
make GCC_PATH=$GCC_PATH
```

The image is written to `firmware/nautilus/build/nautilus.bin`.

If you already cloned without submodules, run `git submodule update --init --recursive`.
To return a library to its upstream state, run `git checkout -- . && git clean -fdx`
inside its folder.

## Flashing

The firmware is built as `APP_TYPE = BOOT_SRAM`. It runs from SRAM and needs the Daisy
bootloader, which loads it from QSPI at `0x90040000`. With the module in bootloader mode:

```sh
cd firmware/nautilus
make program-dfu
```

The firmware stores its settings at QSPI `0x90000400` and the CV calibration at
`0x90001000`. The bootloader leaves the first 256 KB of QSPI alone, so both survive a
firmware update.

Flashing an unofficial image is at your own risk. To go back to the stock firmware,
install the official release from Qu-Bit.
