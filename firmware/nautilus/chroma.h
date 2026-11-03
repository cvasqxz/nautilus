// Nautilus (Qu-Bit) v1.2.0 — "Chroma" effect (16 in the engine: 4 banks × 4)
// Process @0x24009090. Size 0x138.
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "daisysp.h"
#include "pitch_tap.h" // SineLut

namespace nautilus
{
using daisysp::fclamp;

// x·(x²+27)/(9x²+27): rational approximation of tanh
inline float RationalTanh(float x)
{
    const float x2 = x * x;
    return (x2 + 27.f) * x / (x2 * 9.f + 27.f);
}

// Fast exp() (Schraudolph with a polynomial correction of the mantissa).
// Takes x·2^23/ln2 already converted to an integer: the original multiplies in that order.
static constexpr float kExpScale = 12102203.f; // 2^23 / ln(2)
inline float FastExp(int32_t i)
{
    union
    {
        float   f;
        int32_t i;
    } v;
    const int32_t m = (i >> 7) & 0xffff;
    v.i = i + 0x3f800000
          + ((((((((1277 * m) >> 14) + 14825) * m) >> 14) - 79749) * m) >> 11)
          - 626;
    return v.f;
}

// Two-pole resonant filter (Paul Kellett), with LP/HP/BP outputs
struct ResonantFilter
{
    enum Type
    {
        LOWPASS,
        HIGHPASS,
        BANDPASS,
    };

    float Process(float in)
    {
        fb = res / (1.f - g) + res;
        s1 += g * ((in - s1) + fb * (s1 - s2));
        s2 += g * (s1 - s2);
        switch(type)
        {
            case HIGHPASS: return in - s1;
            case BANDPASS: return s1 - s2;
            case LOWPASS: return s2;
            default: return 0.f;
        }
    }

    ResonantFilter()
    {
        type = LOWPASS;
        g    = 0.5f;
        res  = 0.5f;
        s1   = 0.f;
        s2   = 0.f;
        fb   = 0.f;
        sr   = 48000.f;
    }

    int   type; // +0x00
    float g;    // +0x04
    float res;  // +0x08
    float s1;   // +0x0c
    float s2;   // +0x10
    float fb;   // +0x14
    float sr;   // +0x18
};

struct Chroma
{
    enum Mode
    {
        MODE_LOWPASS,
        MODE_HIGHPASS,
        MODE_CRUSH,
        MODE_DRIVE,
        MODE_FOLD,
        MODE_CLIP,
    };

    // Recomputes the parameters of the current mode. Inlined in Process (on a mode
    // change), in the initialization and in SetMode.
    void UpdateParams()
    {
        switch(mode)
        {
            case MODE_LOWPASS:
            {
                const float x = amount * 255.75f;
                uint32_t    i = (uint32_t)x;
                if(i > SineLut::kSize - 1)
                    i = SineLut::kSize - 1;
                const float s0 = SineLut::table[i];
                const float s1 = SineLut::table[i + 1];
                filter[0].type = ResonantFilter::LOWPASS;
                filter[1].type = ResonantFilter::LOWPASS;
                if(amount > 0.001f)
                    coef_target = (1.f - s0) + ((float)(uint32_t)x - x) * (s1 - s0);
                else
                    coef_target = 0.999f;
                break;
            }
            case MODE_HIGHPASS:
                filter[0].type = ResonantFilter::HIGHPASS;
                filter[1].type = ResonantFilter::HIGHPASS;
                coef_target    = fminf(amount * amount * 0.65f, 0.999f);
                break;
            case MODE_CRUSH:
            {
                const float x    = amount * 3.99f;
                const float xi   = (float)(int32_t)x;
                const float frac = x - xi;
                // GCC moves these comparisons onto amount (1/3.99, 2/3.99, 3/3.99 in the binary)
                if(x < 1.f)
                {
                    crush.SetBitsToCrush(6);
                    crush.SetDownsampleFactor(frac * 0.4f);
                }
                else if(x < 2.f)
                {
                    crush.SetBitsToCrush(8);
                    crush.SetDownsampleFactor(((1.f - x) + xi) * 0.4f);
                }
                else if(x < 3.f)
                {
                    crush.SetBitsToCrush(9);
                    crush.SetDownsampleFactor(frac * 0.4f);
                }
                else
                {
                    crush.SetBitsToCrush(10);
                    crush.SetDownsampleFactor(((1.f - x) + xi) * 0.4f);
                }
                break;
            }
            case MODE_FOLD: fold.SetGain(amount_smoothed * 2.f + 1.f); break;
            case MODE_CLIP:
            {
                const float s  = amount_smoothed;
                const float s2 = s * s;
                clip_amount    = s;
                clip_amount2   = s2;
                const float d  = fclamp(s2 + s2, 0.f, 1.f);
                clip_f80       = 1.f;
                clip_f7c       = fclamp(s2 + s2 - 1.f, 0.f, 1.f) * 8.f + 1.f;
                // clip_k first and clamped to 2 (in the binary, d < 1/86); clip_makeup = 1
                // is replaced if clip_k > 1. Comparing d instead of clip_k makes GCC
                // propagate clip_k = 2 along paths the original does not have.
                clip_k = d * 86.f + 1.f;
                if(clip_k < 2.f)
                    clip_k = 2.f;
                clip_makeup = 1.f;
                if(clip_k > 1.f)
                    clip_makeup = 0.5f - sinf(s * HALFPI_F) * 0.375f;
                clip_quiet = s < 0.02f;
                break;
            }
            default: break;
        }
    }

    // Inlined in NautilusEngine::Init
    void Init(float sr)
    {
        coef_target   = 0.5f;
        coef          = 0.5f;
        fading_out    = false;
        clip_quiet    = false;
        sample_rate   = sr;
        field_68      = sr;
        amount        = 0.f;
        level         = 0.f;
        dry_mix       = 0.f;
        fade          = 0.f;
        clip_amount   = 0.f;
        clip_env      = 0.f;
        filter[0].res = 0.f;
        filter[0].fb  = 0.f;
        filter[1].res = 0.f;
        filter[1].fb  = 0.f;
        mode          = MODE_CRUSH;
        next_mode     = MODE_CRUSH;
        crush.Init();
        crush.SetDownsampleFactor(0.f);
        crush.SetBitsToCrush(0);
        fold.Init();
        UpdateParams();
        comp.Init(sample_rate);
        comp.SetAttack(0.005f);
        comp.SetRelease(0.05f);
        comp.SetRatio(10.f);
        comp.SetThreshold(-24.f);
        tone.Init(sample_rate);
        atone.Init(sample_rate);
        dcblock.Init(sample_rate);
        float tone_freq = 20000.f;
        tone.SetFreq(tone_freq);
        float atone_freq = 15.f;
        atone.SetFreq(atone_freq);
        saturate = false;
    }

    // Inlined in NautilusEngine::Process (once per block, with depth)
    void SetAmount(float a)
    {
        amount = a;
        UpdateParams();
    }

    // Inlined in NautilusEngine::SetChromaMode @0x2400cef4: the actual change happens
    // in Process, after the fade out.
    void SetMode(int m)
    {
        if(m != next_mode)
            fading_out = true;
        next_mode = m;
        UpdateParams();
    }

    // @0x24009090
    float Process(float in)
    {
        const float dry = saturate ? fclamp(RationalTanh(in), -1.8f, 1.8f) : in;
        float       wet = 0.f;

        // Crossfade on mode change
        if(fading_out)
            fade -= 0.0004f;
        else
            fade += 0.0004f;
        if(fade < 0.0001f)
        {
            fade       = 0.f;
            fading_out = false;
            mode       = next_mode;
            UpdateParams();
        }
        else if(fade > 1.f)
        {
            fade = 1.f;
        }
        amount_smoothed += (amount - amount_smoothed) * 0.005f;

        switch(mode)
        {
            case MODE_LOWPASS:
            case MODE_HIGHPASS:
                coef += (coef_target - coef) * 0.001f;
                filter[0].g = coef;
                filter[1].g = coef;
                wet = fclamp(filter[1].Process(filter[0].Process(in)), -4.f, 4.f);
                break;
            case MODE_CRUSH: wet = crush.Process(in); break;
            case MODE_DRIVE:
            {
                const float s = amount_smoothed;
                wet = RationalTanh(in * 3.f * (1.f + s * s * 2.f)) * 0.5f;
                break;
            }
            case MODE_FOLD:
                wet = fold.Process(in + in) * ((1.f - amount_smoothed) * 0.15f + 0.35f);
                break;
            case MODE_CLIP:
            {
                const float s     = amount_smoothed;
                const float boost = s > 0.5f ? s + 0.5f : 1.f;
                const float e = in > 0.f ? 1.f - FastExp((int32_t)(in * -kExpScale * clip_k))
                                         : FastExp((int32_t)(in * kExpScale * clip_k)) - 1.f;
                clip_env += ((clip_quiet ? 0.f : 1.f) - clip_env) * 0.001f;
                wet = boost * clip_makeup * fclamp(clip_f80 * clip_f7c * e, -0.6f, 0.6f)
                      * clip_env;
                break;
            }
            default: wet = 0.f; break;
        }

        if(saturate)
            wet = tone.Process(wet);

        float dry_target;
        if(amount < 0.01f)
        {
            level *= 0.9998f;
            dry_target = 1.f;
        }
        else
        {
            level += (1.f - level) * 0.0002f;
            dry_target = 1.f - fade;
        }
        const float out = (level * fade) * wet;
        dry_mix += (dry_target - dry_mix) * 0.0002f;
        return dcblock.Process(out) + dry_mix * dry;
    }

    float                sample_rate;      // +0x000
    int                  mode;             // +0x004
    int                  next_mode;        // +0x008
    float                amount;           // +0x00c
    ResonantFilter       filter[2];        // +0x010
    daisysp::Decimator   crush;            // +0x048
    float                field_68;         // +0x068 Init: sample rate. TODO(recon): never seen in use
    float                clip_amount;      // +0x06c
    bool                 clip_quiet;       // +0x070
    uint8_t              pad_71[3];
    float                clip_env;         // +0x074
    float                clip_k;           // +0x078
    float                clip_f7c;         // +0x07c
    float                clip_f80;         // +0x080
    float                clip_amount2;     // +0x084
    float                clip_makeup;      // +0x088
    uint32_t             field_8c;         // +0x08c TODO(recon): new in v1.2.0 (not in v1.1 nor in
                                           // the RC1, where Chroma is 0x134 bytes); never accessed
    bool                 fading_out;       // +0x090
    uint8_t              pad_91[3];
    float                fade;             // +0x094
    daisysp::Wavefolder  fold;             // +0x098
    daisysp::Compressor  comp;             // +0x0a0
    daisysp::ATone       atone;            // +0x0e0
    daisysp::Tone        tone;             // +0x0f8
    daisysp::DcBlock     dcblock;          // +0x114
    float                coef_target;      // +0x120
    float                coef;             // +0x124
    float                amount_smoothed;  // +0x128
    bool                 saturate;         // +0x12c
    uint8_t              pad_12d[3];
    float                dry_mix;          // +0x130
    float                level;            // +0x134
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(Chroma, filter) == 0x10, "layout");
static_assert(offsetof(Chroma, crush) == 0x48, "layout");
static_assert(offsetof(Chroma, clip_quiet) == 0x70, "layout");
static_assert(offsetof(Chroma, clip_makeup) == 0x88, "layout");
static_assert(offsetof(Chroma, fade) == 0x94, "layout");
static_assert(offsetof(Chroma, fold) == 0x98, "layout");
static_assert(offsetof(Chroma, comp) == 0xa0, "layout");
static_assert(offsetof(Chroma, atone) == 0xe0, "layout");
static_assert(offsetof(Chroma, tone) == 0xf8, "layout");
static_assert(offsetof(Chroma, dcblock) == 0x114, "layout");
static_assert(offsetof(Chroma, coef_target) == 0x120, "layout");
static_assert(offsetof(Chroma, level) == 0x134, "layout");
static_assert(sizeof(Chroma) == 0x138, "layout");
#pragma GCC diagnostic pop

} // namespace nautilus
