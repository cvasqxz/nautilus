// Nautilus (Qu-Bit) v1.2.0 — reverse reader (8 in the engine: 2 groups × 4, engine + 0x1cc)
// Size 0x3c. Inlined in NautilusEngine::Process.
//
// Records the input into a circular buffer and plays it backwards in segments of `len`
// samples, with a parabolic window 4·p·(1−p). When the length changes it crossfades between
// the previous segment and the new one.
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

namespace nautilus
{

struct Reverser
{
    // Inlined in NautilusEngine::Init: half-buffer segments (10 s)
    void Init(float *buf, size_t sz)
    {
        buffer           = buf;
        size             = sz;
        xfade            = 0.f;
        frac[0]          = 0.f;
        frac[1]          = 0.f;
        pos              = 0;
        sel              = 0;
        xfade_inc        = 1.f / 960.f;
        const int half   = sz / 2;
        len[0]           = half;
        len[1]           = half;
        inv_len[0]       = 1.f / (float)half;
        inv_len[1]       = 1.f / (float)half;
        SetLength(half);
        field_30         = false;
        field_34         = 0;
        field_38         = 0;
    }

    // Interpolated sample of segment `s` (mirrored read from the start of the segment)
    // and its phase within the segment.
    float Read(int s, float &phase) const
    {
        const uint32_t l     = len[s];
        const uint32_t start = l * (pos / l);
        int32_t        j     = 2 * start - pos;
        if(j < 0)
            j += size;
        phase         = (float)(int32_t)(pos - start) * inv_len[s];
        const float a = buffer[j];
        const float b = buffer[(uint32_t)(j - 1) % size];
        return a + (b - a) * frac[s];
    }

    float Process(float in)
    {
        const uint32_t l   = len[sel];
        uint32_t       end = l * (size / l);
        if(end > size)
            end = size;
        if(++pos > end)
            pos = 0;
        buffer[pos] = in;

        if(!xfading)
        {
            float       p;
            const float v = Read(sel, p);
            return p * 4.f * (1.f - p) * v;
        }

        float       pc, po;
        const float vc = Read(sel, pc);
        const float x  = xfade;
        xfade          = x + xfade_inc;
        const float vo = Read(!sel, po);
        // The current-read term goes first: GCC contracts it into an FMA, like the original.
        const float out
            = ((1.f - x) * pc * (1.f - pc) * vc + po * x * (1.f - po) * vo) * 4.f;
        if(!(xfade < 1.f))
        {
            sel     = !sel;
            xfading = false;
            xfade   = 0.f;
        }
        return out;
    }

    // New segment length (in samples). Ignored during a crossfade.
    void SetLength(float length)
    {
        const float max = (float)(size - 1);
        if(!(length > max))
            length = fmaxf(length, 1.f);
        else
            length = max;
        if(xfading)
            return;
        if(len[sel] == (uint32_t)length)
            return;
        const int     s  = !sel;
        const int32_t il = (int32_t)length;
        len[s]           = il;
        frac[s]          = length - (float)il;
        inv_len[s]       = 1.f / length;
        xfade            = 0.f;
        xfading          = true;
    }

    float   *buffer;     // +0x00
    uint32_t size;       // +0x04
    uint32_t pos;        // +0x08
    uint32_t len[2];     // +0x0c
    float    frac[2];    // +0x14
    float    inv_len[2]; // +0x1c
    uint8_t  sel;        // +0x24
    bool     xfading;    // +0x25
    uint8_t  pad_26[2];
    float    xfade;      // +0x28
    float    xfade_inc;  // +0x2c
    bool     field_30;    // +0x30 Init: false. TODO(recon): never seen in use
    uint8_t  pad_31[3];
    uint32_t field_34;    // +0x34 Init: 0. TODO(recon)
    uint32_t field_38;    // +0x38 Init: 0. TODO(recon)
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(Reverser, sel) == 0x24, "layout");
static_assert(offsetof(Reverser, xfade_inc) == 0x2c, "layout");
static_assert(sizeof(Reverser) == 0x3c, "layout");
#pragma GCC diagnostic pop

} // namespace nautilus
