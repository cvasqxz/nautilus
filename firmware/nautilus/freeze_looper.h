// Nautilus (Qu-Bit) v1.2.0 — "Buffer Locked Freeze" looper (2 in the engine, engine + 0x8850)
// Size 0x44. Inlined in NautilusEngine::Process. 480000-sample buffer in SDRAM.
//
// Without freeze it records the wet signal into a loop as long as the tempo. With freeze it
// plays the loop (240-sample fades at the edges) and keeps recording into the rest of the
// buffer until it is full. Length changes are applied with a fade out and fade in.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "daisysp.h"

namespace nautilus
{
using daisysp::fclamp;

struct FreezeLooper
{
    // Inlined in NautilusEngine::Init
    void Init(float *buf, size_t size, float sr)
    {
        sample_rate     = sr;
        buffer          = buf;
        buffer_size     = size;
        loop_len        = size;
        write_pos       = 0;
        read_pos        = 0;
        field_18        = 0;
        frozen          = false;
        relocking       = false;
        rec_gain        = 1.f;
        field_24        = 1.f;
        len_fade        = 1.f;
        len_fade_target = 1.f;
        full            = false;
        new_len         = size;
        field_38        = size;
        play_gain       = 0.f;
        thru_gain       = 1.f;
    }

    void SetLength(uint32_t length)
    {
        uint32_t l = length >= buffer_size ? buffer_size - 1 : length;
        if(l == 0)
            l = 1;
        new_len = l;
    }

    void Process(const float *in, float *out, size_t size)
    {
        if(!buffer)
            return;
        const bool f = frozen;
        if(f)
        {
            if(!was_frozen)
            {
                field_1c = false;
                full     = false;
                field_38 = new_len;
                field_18 = loop_len - 1;
            }
        }
        else if(was_frozen)
        {
            full = false;
        }
        was_frozen = f;
        if(loop_len > 6000 && new_len != loop_len)
            len_fade_target = 0.f;

        uint32_t r = read_pos;
        uint32_t w = write_pos;
        for(size_t i = 0; i < size; i++)
        {
            const float fade_in  = r < 240 ? (float)r * (1.f / 239.f) : 1.f;
            const float fade_out = r > loop_len - 240
                                       ? fclamp(1.f - (float)(r + 240 - loop_len) * (1.f / 239.f),
                                                0.f,
                                                1.f)
                                       : 1.f;
            if(f)
                play_gain += (1.f - play_gain) * 0.005f;
            else
                play_gain *= 0.995f;
            thru_gain += ((f ? 0.f : 1.f) - thru_gain) * 0.005f;
            // The loop term goes first: the original contracts it into an FMA with thru·in.
            out[i] = buffer[r] * rec_gain * fade_in * fade_out * play_gain * len_fade
                     + thru_gain * in[i];
            if(!f)
                buffer[w] = rec_gain * in[i];
            else if(!full)
                buffer[w] = in[i];

            len_fade += (len_fade_target - len_fade) * 0.01f;
            if(len_fade < 0.0003f)
            {
                loop_len        = new_len;
                len_fade_target = 1.f;
            }

            if(relocking && loop_len < 4800)
                relocking = false;
            if(relocking)
            {
                rec_gain *= 0.94f;
                if(rec_gain < 0.0003f)
                {
                    relocking = false;
                    r         = 0;
                    if(!f)
                        w = 0;
                    if(new_len != loop_len)
                        loop_len = new_len;
                }
            }
            else
            {
                rec_gain += (1.f - rec_gain) * 0.06f;
            }
            r++;
            w++;

            if(r > loop_len - 1)
            {
                if(new_len != loop_len)
                    loop_len = new_len;
                r = 0;
            }
            if(!f)
            {
                if(w > loop_len - 1)
                    w = 0;
            }
            else if(w > buffer_size - 1)
            {
                full = true;
                w    = 0;
            }
        }
        read_pos  = r;
        write_pos = w;
    }

    float    sample_rate;     // +0x00
    float   *buffer;          // +0x04
    uint32_t buffer_size;     // +0x08
    uint32_t loop_len;        // +0x0c
    uint32_t write_pos;       // +0x10
    uint32_t read_pos;        // +0x14
    uint32_t field_18;        // +0x18 on freeze: loop_len − 1. TODO(recon): never seen in use
    bool     field_1c;        // +0x1c on freeze: false. TODO(recon)
    bool     frozen;          // +0x1d copy of NautilusEngine::frozen
    bool     was_frozen;      // +0x1e
    bool     relocking;       // +0x1f set by NautilusEngine::OnTempoTick (relock)
    float    rec_gain;        // +0x20
    float    field_24;        // +0x24 TODO(recon)
    float    len_fade;        // +0x28
    float    len_fade_target; // +0x2c
    bool     full;            // +0x30 buffer full during the freeze
    uint8_t  pad_31[3];
    uint32_t new_len;         // +0x34
    uint32_t field_38;        // +0x38 on freeze: new_len. TODO(recon): never seen in use
    float    play_gain;       // +0x3c
    float    thru_gain;       // +0x40
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(FreezeLooper, rec_gain) == 0x20, "layout");
static_assert(offsetof(FreezeLooper, full) == 0x30, "layout");
static_assert(offsetof(FreezeLooper, thru_gain) == 0x40, "layout");
static_assert(sizeof(FreezeLooper) == 0x44, "layout");
#pragma GCC diagnostic pop

} // namespace nautilus
