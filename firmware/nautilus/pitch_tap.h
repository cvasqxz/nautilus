// Nautilus (Qu-Bit) v1.2.0 — delay tap with pitch shift (8 in the engine: 2 groups × 4)
// Process @0x24008d64. Size 0x8c. External buffer in SDRAM.
// Similar to DaisySP's PitchShifter (two phases offset by π with a sine window),
// but with an external buffer, two delays (crossfade/slew) and its own sine table.
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "daisysp.h"
#include "phasor.h"

namespace nautilus
{

// One-cycle sine table, computed once (0x2405e98c; flag at 0x2405f98c)
struct SineLut
{
    static constexpr int kSize = 1024;
    static void          Init()
    {
        if(!initialized)
        {
            for(int i = 0; i < kSize; i++)
                table[i] = sinf((i * (1.f / kSize) - 1.f) * TWOPI_F);
            initialized = true;
        }
    }
    static float table[kSize];
    static bool  initialized;
};
float BSS_ORDER(16) SineLut::table[SineLut::kSize];
bool  BSS_ORDER(17) SineLut::initialized;

// sin(π·phase) with phase in [0, 1]. Like the original, with i = 1023 it reads table[1024].
inline float HalfSine(float phase)
{
    const float    x    = phase * 511.5f;
    uint32_t       i    = (uint32_t)x;
    const float    frac = x - (float)i;
    if(i > SineLut::kSize - 1)
        i = SineLut::kSize - 1;
    return SineLut::table[i] + frac * (SineLut::table[i + 1] - SineLut::table[i]);
}

struct PitchTap
{
    // Interpolated read `pos` samples behind the write position
    float ReadAt(float pos)
    {
        const int32_t  ipos = (int32_t)pos;
        const float    frac = pos - (float)ipos;
        const uint32_t p    = write_pos + ipos;
        const float    a    = buffer[p % buffer_size];
        const float    b    = buffer[(p + 1) % buffer_size];
        return a + frac * (b - a);
    }

    // @0x24008d64 (out of line in the original)
    __attribute__((noinline)) float Process()
    {
        const float phs_a  = phs[0].Process();
        const float phs_b  = phs[1].Process();
        const float gain_a = HalfSine(phs_a);
        const float gain_b = HalfSine(phs_b);
        float       time   = delay[active];
        mod[1]             = time * (1.f - phs_b) - 1.f;
        mod[0]             = (1.f - phs_a) * time - 1.f;

        if(slewing)
        {
            time = delay[active] = time + slew_coeff * (delay[!active] - time);
            // Reads mod[0] first: the original has two copies of this computation (one per
            // branch, with different registers); with both written the same way GCC merges them.
            if(transpose != 0.f)
            {
                const float a = ReadAt(mod[0]);
                const float b = ReadAt(mod[1]);
                // The first product is contracted into an FMA: the original fuses gain_a·a.
                return gain_a * a + gain_b * b;
            }
        }
        else
        {
            if(crossfading)
            {
                // Time change without pitch: linear mix between the previous time and the new one
                const float xf  = xfade;
                const float out = (1.f - xf) * ReadAt(time) + xf * ReadAt(delay[!active]);
                xfade           = xf + xfade_inc;
                if(!(xfade < 1.f))
                {
                    crossfading = false;
                    active      = !active;
                    xfade       = 0.f;
                }
                return out;
            }
            if(transpose != 0.f)
                return gain_b * ReadAt(mod[1]) + gain_a * ReadAt(mod[0]);
        }
        return ReadAt(time);
    }

    // Inlined in NautilusEngine::Init. `size` is the length of the voice's region: the tap uses
    // the first half (10 s at 48 kHz).
    void Init(float *buf, size_t size, float sr)
    {
        sample_rate = sr;
        half_size   = size / 2;
        half[0]     = buf;
        half_index  = 0;
        field_18    = false;
        field_1c    = 0;
        half[1]     = buf + size / 2;
        buffer      = buf;
        buffer_size = size / 2;
        if(buffer_size)
            memset(buf, 0, buffer_size * sizeof(float));
        field_10    = 1;
        xfade_inc   = 1.f / 960.f;
        delay[0]    = (float)(buffer_size - 1);
        delay[1]    = (float)(buffer_size - 1);
        write_pos   = 0;
        field_14    = 0;
        xfade       = 0.f;
        crossfading = false;
        slewing     = false;
        active      = 0;
        transpose   = 12.f;
        phs[0].Init(sample_rate, 0.f, 0.f);
        phs[1].Init(sample_rate, 0.f, PI_F);
        mod[0]     = 0.f;
        mod[1]     = 0.f;
        slew_coeff = 0.00005f;
        UpdateModFreq();
    }

    // Purge: switch to the other half, which is already clear (main loop)
    void SwapBuffer()
    {
        if(++half_index > 1)
            half_index = 0;
        buffer = half[half_index];
    }

    // Clears the half that is not in use (main loop, after SwapBuffer)
    void ClearSpare()
    {
        float *p = half[half_index == 0 ? 1 : 0];
        for(uint32_t i = 0; i < half_size; i++)
            p[i] = 0.f;
    }

    // The write position moves backwards; ReadAt reads forwards from write_pos.
    void Write(float x)
    {
        buffer[write_pos] = x;
        write_pos         = (write_pos == 0 ? buffer_size : write_pos) - 1;
    }

    // New delay time: with slew the value is chased; otherwise, crossfade.
    void SetDelay(float time)
    {
        if(slewing)
        {
            delay[!active] = time > 0.f ? time : 1.f;
        }
        else if(!crossfading && time != delay[active])
        {
            delay[!active] = time > 0.f ? time : 1.f;
            xfade          = 0.f;
            crossfading    = true;
        }
    }

    // Recomputes the phase frequency for the current transposition (2^(t/12)).
    void UpdateModFreq()
    {
        float r = 1.f, freq = transpose;
        if(transpose != 0.f)
        {
            r    = expf(transpose * 0.057762265f);
            freq = (r - 1.f) * sample_rate / delay[active];
        }
        ratio    = r;
        mod_freq = freq;
        phs[0].SetFreq(mod_freq);
        phs[1].SetFreq(mod_freq);
    }

    void SetTranspose(float semis, float r)
    {
        transpose = semis;
        ratio     = r;
        mod_freq  = (r - 1.f) * sample_rate / delay[active];
        phs[0].SetFreq(mod_freq);
        phs[1].SetFreq(mod_freq);
    }

    void ResetTranspose()
    {
        transpose = 0.f;
        mod_freq  = 0.f;
        ratio     = 1.f;
        phs[0].SetFreq(mod_freq);
        phs[1].SetFreq(mod_freq);
    }

    uint8_t         active;      // +0x00 index into delay[]
    bool            crossfading; // +0x01
    bool            slewing;     // +0x02
    uint8_t         pad_03;
    float          *buffer;      // +0x04
    uint32_t        buffer_size; // +0x08
    uint32_t        write_pos;   // +0x0c
    uint32_t        field_10;    // +0x10 Init: 1. TODO(recon)
    uint32_t        field_14;    // +0x14 TODO(recon)
    bool            field_18;    // +0x18 TODO(recon)
    uint8_t         pad_19[3];
    uint32_t        field_1c;    // +0x1c TODO(recon)
    uint32_t        field_20;    // +0x20 TODO(recon)
    float           delay[2];    // +0x24 time in samples (previous and new)
    float           xfade;       // +0x2c
    float           xfade_inc;   // +0x30 Init: 1/960
    float           sample_rate; // +0x34
    float           transpose;   // +0x38 semitones (Init: 12)
    float           ratio;       // +0x3c 2^(transpose/12)
    Phasor          phs[2];      // +0x40 offset by π
    float           mod[2];      // +0x68
    float           mod_freq;    // +0x70
    float           slew_coeff;  // +0x74 Init: 5e-5
    uint32_t        field_78;    // +0x78 TODO(recon)
    uint32_t        half_size;   // +0x7c samples in each half
    uint32_t        half_index;  // +0x80 half in use
    float          *half[2];     // +0x84 the buffer alternates between two halves (purge)
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(PitchTap, delay) == 0x24, "layout");
static_assert(offsetof(PitchTap, transpose) == 0x38, "layout");
static_assert(offsetof(PitchTap, phs) == 0x40, "layout");
static_assert(offsetof(PitchTap, mod) == 0x68, "layout");
static_assert(offsetof(PitchTap, slew_coeff) == 0x74, "layout");
static_assert(sizeof(PitchTap) == 0x8c, "layout");
#pragma GCC diagnostic pop

} // namespace nautilus
