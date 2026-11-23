// Nautilus (Qu-Bit) v1.2.0 — phase accumulator
// Qu-Bit's modified daisysp::Phasor (Init @0x2402a418, SetFreq @0x2402a440,
// Process @0x2402a454). Compared with DaisySP dc448d4 it caches 2*pi/sample_rate
// (one extra field, 0x14 bytes instead of 0x10), wraps negative phases instead of
// clamping them to 0 (as in DaisySP 90978ea, fix/pitch_shift branch), and adds SetPhase.
//
// The methods are kept out of line, as in the original: inlined, GCC could contract the
// stored increment into an FMA with the phase update and change the result.
#pragma once

#include "daisysp.h"

namespace nautilus
{

class Phasor
{
  public:
    __attribute__((noinline)) void Init(float sample_rate, float freq, float initial_phase)
    {
        freq_           = freq;
        sample_rate_    = sample_rate;
        phs_            = initial_phase;
        two_pi_over_sr_ = TWOPI_F / sample_rate;
        inc_            = two_pi_over_sr_ * freq;
    }

    __attribute__((noinline)) void SetFreq(float freq)
    {
        freq_ = freq;
        inc_  = two_pi_over_sr_ * freq;
    }

    __attribute__((noinline)) float Process()
    {
        float out = phs_ / TWOPI_F;
        phs_ += inc_;
        if(phs_ > TWOPI_F)
            phs_ -= TWOPI_F;
        if(phs_ < 0.0f)
            phs_ += TWOPI_F;
        return out;
    }

    // AudioCallback resets the clock phases to 0 with tap tempo
    inline void SetPhase(float phase) { phs_ = phase; }

    inline float GetFreq() { return freq_; }

  private:
    float freq_;
    float sample_rate_, inc_, phs_;
    float two_pi_over_sr_; // +0x10
};

} // namespace nautilus
