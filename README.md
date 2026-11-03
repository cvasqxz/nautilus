# Nautilus firmware reconstruction

A source-level reconstruction of the firmware for the **Qu-Bit Nautilus** (v1.2.0), a
stereo delay Eurorack module built on the Electrosmith Daisy Seed (STM32H750).

The code was rebuilt from the released binary (`Nautilus_v1_2_0-wht3.bin`). It targets the same
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
| `led_color.h` | RGB color for the LEDs |
| `phasor.h` | Phase accumulator used by the delay taps and the clock phases |
| `ui.h` | Main and calibration pages, LED drawing and control handling |
| `options.h` | Loading of `options.json` from the USB drive |
| `usb_drive.h` | USB drive mounting state machine |

### Differences from the original's libraries

The original was linked against libDaisy and DaisySP with a few local changes. This
build uses both libraries unmodified and carries those changes in the firmware instead:

| Change in the original | In this build |
|---|---|
| `daisy::Color` from libDaisy `bbfa87cb` (#639): constructors, setters, `operator+`, `Blend` | `led_color.h`, same interface and behavior |
| `daisysp::Phasor` modified by Qu-Bit: caches `2π/sample_rate`, wraps negative phases, `SetPhase` | `phasor.h`, same layout and behavior |
| USB host driver from libDaisy `4e73c969` (`IsPortEnabled`, `IsDeviceConnected`) | The v7.1.0 driver; `usb_drive.h` reads the port and device state from the ST middleware, and `main` registers the mass storage class |
| SDRAM remapped to `0x60000000` (FMC bank swap) | libDaisy's default address, `0xC0000000` |

The v7.1.0 USB host driver also resets the host stack when it gets stuck in the abort
state, which the original's driver does not do.

## Changes from the original firmware

This branch (`dev`) starts from the reconstruction and changes the firmware's behavior
where the original has bugs or limitations. Each change is checked in differential emulation
against the official binary: the output may only differ where the change is meant to act.

| Change | Effect |
|---|---|
| Default LED colors | `#20c3fe`, `#77f986`, `#ffe45c`, `#ab85ff`, `#f0d2d1`, `#f61067` |
| Reverb diffusion follows the preset | The original ramps the allpass coefficient towards the reverb amount instead of the preset's diffusion (0.6 / 0.5 / 0.66). With the knob near the top the input diffusers turn into integrators and the reverb buffer clips constantly. |
| Reverb delay lines stored as `float` | The original stores them as 16-bit integers (as in Rings), which adds quantization noise around -75 dBFS to the tail |
| Stereo reverb input | The original sums L + R into one chain of input diffusers. Here each channel has its own (the right ones with different lengths) and feeds one branch of the tank, so the early reflections stay on the side of the source and the late tail spreads across both channels. |
| Buttons held at power-up must be held for 200 ms | The original reads Shift, Freeze and Purge once at power-up. A single spurious read opened the input test page (Shift) or recalibrated the CV inputs with whatever was patched (Freeze or Purge). |
| Unused oscilloscope FIFOs removed | Two `FIFO<float, 16000>` that nothing reads (~125 KB of AXI SRAM); they make room for the `float` reverb buffer |

With these changes the globals no longer sit at the original addresses.

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

# 1. libDaisy
make -C libs/libDaisy GCC_PATH=$GCC_PATH

# 2. DaisySP, built with -ffast-math like the original
make -C libs/DaisySP GCC_PATH=$GCC_PATH OPT="-O3 -ffast-math"

# 3. Firmware
cd firmware/nautilus
make GCC_PATH=$GCC_PATH
```

The image is written to `firmware/nautilus/build/nautilus.bin`.

If you already cloned without submodules, run `git submodule update --init --recursive`.

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
