# Nautilus alternative firmware

Alternative firmware for the **Qu-Bit Nautilus**, the stereo delay Eurorack module built on
the Electrosmith Daisy Seed.

It started as a source-level reconstruction of the official v1.2.0 firmware and has since
changed how several controls work: Depth is a tone tilt for the repeats, Mix and Chroma are
independent dry and wet levels, the Sonar output plays in tune with an oscillator, and the
module no longer needs a USB stick. The delay engine, the delay and feedback modes, freeze,
purge and the reverb character are the same as in the official firmware.

This is an independent project. It is not affiliated with or endorsed by Qu-Bit
Electronix.

## What changes when you play it

### Depth: dark or bright repeats

Chroma's six effects are gone. **Depth** now tilts the tone of the repeats:

- **Centre**: the repeats are untouched.
- **Turning left**: every repeat comes back darker, like an old tape echo.
- **Turning right**: every repeat comes back thinner and brighter.

The filter sits inside the feedback, so the effect builds up on every pass. Changes are
smooth, so Depth can be swept by hand or with its CV input. The output meter LEDs show the
setting: they turn from green (flat) towards purple (dark) or yellow (bright).

### Mix and Chroma: a dry/wet mixer

- **Mix** sets the level of the dry signal.
- **Chroma** sets the level of the delayed (wet) signal.

They are independent, like two faders on a mixer, and each one follows its CV input. Both at
12 o'clock sound like the official firmware with Mix at the centre. Both fully up give
about 6 dB more level, so watch the output.

### Sonar: quantized pitch CV

The Sonar output keeps its evolving stepped voltage, built from the rhythm of the delay
voices (Sensors, Dispersal and Reversal shape the pattern, and it follows the tap or Clock
In tempo). Every step is now **quantized to a note at 1 V/octave**, so you can patch Sonar
into an oscillator's pitch input and hear a melody in tune.

- **Shift + Freeze** picks the scale: 1 chromatic, 2 major, 3 natural minor, 4 major
  pentatonic, 5 minor pentatonic, 6 blues.
- After a change, the **Freeze LED blinks the scale number** in that scale's colour. While
  you hold Shift, the Freeze LED shows the colour of the current scale.
- The scale is remembered after power-off.
- The range is 0 to 5 V (five octaves), with 0 V as the root. Tune the root on the
  oscillator.

The Sonar clock modes of the official firmware are gone.

### Tap tempo with Shift

Shift is both the tap tempo button and the modifier for the second functions. In the
official firmware every Shift press counted as a tap, so after setting the reverb with
Shift + Resolution, for example, the next tap measured the whole pause and the tempo
jumped. Now a Shift press that you use for a combination does not count as a tap. A plain
tap is taken when you release Shift, timed from the moment you pressed it. Clock In works
as before.

### Reverb

The reverb (Shift + Resolution) sounds cleaner and changes more smoothly:

- **Smooth changes**: turning the amount up or down no longer cuts the tail that is
  already playing.
- **No jump when you grab the knob**: the amount continues from its current value, wherever
  Resolution happens to be, and the knob ends still reach zero and full.
- **Cleaner at high settings**: it no longer clips and grows harsh near the top, and its
  tail has no low-level quantization noise.
- **Stereo input**: a sound on the left keeps its early reflections on the left (and the
  same for the right) before the tail spreads across both sides.

### No USB stick

The module no longer reads `options.json` from a USB stick. Every option of the official
firmware stays at its default value:

| Option | Value |
|---|---|
| Shimmer / De-Shimmer interval | one octave up / down |
| Reverb preset | Normal |
| Freeze mix behavior | Normal |
| Quantize freeze | on |
| Buffer locked freeze | on |
| Clear on mode change | off |
| Continuous purge | off |
| Attenuverter targets | Dispersal and Feedback |
| LED colours | `#20c3fe`, `#77f986`, `#ffe45c`, `#ab85ff`, `#f0d2d1`, `#f61067` |

Firmware updates still go through USB with the Daisy bootloader (see Flashing).

### Other changes

- **No LED dimmer**: Shift + Chroma used to set the LED brightness. The LEDs now stay at full
  brightness.
- **Power-up buttons must be held**: the input test page (Shift) and the CV calibration
  (Freeze or Purge) only open if the button is held for 200 ms at power-up. In the official
  firmware a single noisy reading could open the test page or recalibrate the CV inputs by
  accident.

## Controls

| Control | Alone | With Shift held |
|---|---|---|
| Sensors, Dispersal, Reversal, Feedback | as in the official firmware | — |
| Resolution | tempo multiplier or division of the delay time | reverb amount |
| Depth | tone tilt of the repeats (centre = flat) | — |
| Mix | dry level | — |
| Chroma | wet level | — |
| Attenuverter 1 | CV amount for Dispersal | input level |
| Attenuverter 2 | CV amount for Feedback | — |
| Shift | tap tempo (on release) | — |
| Freeze | freeze | next Sonar scale |
| Feedback button | feedback mode | stereo / mono input |
| Delay Mode button | delay mode | free-running delay on / off |
| Purge | purge | reset modes; on release, also reset the settings |

### At power-up

- **Hold Shift**: input test page. Each LED shows a knob (blue) and its CV input (green).
  Sonar alternates between 1 V and 3 V once per second, two octaves apart, so you can check
  its tuning with a tuner or a meter. Press Purge to leave.
- **Hold Freeze or Purge**: recalibrate the CV inputs. Unplug every CV cable first: the
  module takes the voltage of each input at rest as its zero, and keeps it after power-off.

## Building

### Requirements

- **GNU Arm Embedded Toolchain 10-2020-q4-major** (GCC 10.2.1), the one shipped with the
  Daisy Toolchain and the one used for development. Other versions should work too.
- `make`, `git`, and `dfu-util` for flashing.

### Steps

```sh
git clone --recurse-submodules https://github.com/cvasqxz/nautilus
cd nautilus

# If the toolchain is not in the PATH, point GCC_PATH to its bin/ directory
export GCC_PATH=/path/to/gcc-arm-none-eabi-10-2020-q4-major/bin

# 1. libDaisy
make -C libs/libDaisy GCC_PATH=$GCC_PATH

# 2. DaisySP, built with -ffast-math like the official firmware
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
