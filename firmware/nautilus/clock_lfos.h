// Nautilus (Qu-Bit) v1.2.0 — clock phases for the DAC and the LEDs
#pragma once

#include "daisysp.h"
#include "nautilus_engine.h"
#include "phasor.h"

namespace nautilus
{


// Clock phases for the DAC and the LEDs (0x2405f998, 0x110 bytes, destructor @0x240041b0).
// AudioCallback sets the frequencies; DacCallback advances them.
struct ClockLfos
{
    ~ClockLfos() {}
    Phasor  clock[3];                       // +0x000 [2] = tempo (bpm / 60)
    Phasor  voice[kNumGroups][kNumVoices];  // +0x03c one per tap: sr / time
    float   clock_out[2];                   // +0x0dc
    float   voice_out[kNumGroups][kNumVoices]; // +0x0e4
    float   tempo_phase;                    // +0x104
    float   prev_tempo_phase;               // +0x108
    bool    tempo_wrapped;                  // +0x10c
};

extern ClockLfos clock_lfos; // main.cpp

} // namespace nautilus
