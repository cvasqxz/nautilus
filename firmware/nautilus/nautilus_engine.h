// Nautilus (Qu-Bit) v1.2.0 — DSP engine
// Global object at 0x2405faa8. Ctor @0x24009ca8, Init inlined in main (@0x2400ef00..0x240104c0),
// Process @0x24009f0c.
//
// Each channel (A = left, B = right) has 4 voices. Each voice is a delay tap with
// pitch shift (PitchTap) and a reverse reader (Reverser) that share the input; the
// feedback goes through one Chroma per voice. Then come DcBlock, the limiter, the
// "Buffer Locked Freeze" looper, the dry/wet mix and the reverb.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "daisysp.h"
#include "pitch_tap.h"
#include "chroma.h"
#include "reverb.h"
#include "reverser.h"
#include "freeze_looper.h"

namespace nautilus
{

static constexpr int kNumVoices = 4; // voices per group
static constexpr int kNumGroups = 2;

// Group of taps. Its constructor initializes the shared sine table
// (in the engine ctor: 2-iteration loop with the guard 0x2405f98c).
struct TapBank
{
    TapBank() { SineLut::Init(); }

    // Transposition in semitones (Shimmer / De-Shimmer); 0 disables it.
    void SetTranspose(float semis)
    {
        // Octaves before the test (GCC turns it into semis != 0): this keeps t·ln2 in another
        // block so it is not folded with 1/12, as in the original (2^(semis/12) with two products)
        const float t = semis / 12.f;
        if(t != 0.f)
        {
            const float r = expf(t * 0.6931472f);
            for(int v = 0; v < kNumVoices; v++)
                tap[v].SetTranspose(semis, r);
        }
        else
        {
            for(int v = 0; v < kNumVoices; v++)
                tap[v].ResetTranspose();
        }
    }

    PitchTap tap[kNumVoices];
};

// One Chroma per voice
struct ChromaBank
{
    Chroma chroma[kNumVoices];
};

// "Mix Mode" in options.json
enum MixMode
{
    MIX_LINEAR,
    MIX_CONSTANT_POWER,
    MIX_TRANSITION,
    MIX_WET_AMOUNT,
};

// "Reverb Preset" in options.json
enum ReverbPreset
{
    REVERB_NORMAL,
    REVERB_BRIGHT,
    REVERB_DARK,
};

// Delay modes (Delay Mode button)
enum DelayMode
{
    DELAY_FADE,
    DELAY_DOPPLER,
    DELAY_SHIMMER,
    DELAY_DESHIMMER,
};

// Feedback modes (Feedback button)
enum FeedbackMode
{
    FEEDBACK_NORMAL,
    FEEDBACK_PING_PONG,
    FEEDBACK_CASCADE,
    FEEDBACK_ADRIFT,
};

struct NautilusEngine
{
    // Requests a delay mode change: Process lowers the level, applies the mode and raises
    // it again. Transpose Up/Down in options.json and settings loading use it to reapply it.
    // `clear`: clear the buffers when applying it (Delay Mode button with Clear on Mode Change).
    void RequestDelayMode(uint8_t mode, bool clear = false)
    {
        delay_mode_pending = true;
        pending_delay_mode = mode;
        pending_clear      = clear;
    }

    // Same for the feedback mode. Inlined in MainPage::OnButton and CalPage::OnButton.
    void RequestFeedbackMode(int mode)
    {
        feedback_mode_pending = true;
        pending_feedback_mode = mode;
        if(clear_on_mode_change)
            pending_clear = true;
    }

    // Shift + Purge and leaving calibration: default modes and input level
    void ResetModes()
    {
        input_level = 1.f;
        RequestDelayMode(DELAY_FADE);
        RequestFeedbackMode(FEEDBACK_NORMAL);
    }

    void SetReverbAmount(float a) { reverb_amount = fclamp(a, 0.f, 1.f); }

    // Inlined in main (@0x2400ef00..0x240104c0). main clears the SDRAM buffers beforehand.
    void Init(float sr, float *voices_a, float *voices_b, float *freeze_l, float *freeze_r)
    {
        buffer_a             = voices_a;
        buffer_b             = voices_b;
        field_158            = 0;
        purge_request        = false;
        clear_request        = false;
        purge_released       = false;
        period               = sr * 0.5f;
        transpose_down       = -12.f;
        bpm                  = 120.f;
        field_154            = 4;
        transpose_up         = 12.f;
        purge_level          = 0.f;
        freeze_always_wet    = false;
        freeze_punch_in      = false;
        quantize_freeze      = true;
        clear_on_mode_change = false;
        divisions            = 1;
        write_gain           = 0.f;
        depth                = 0.f;
        feedback             = 0.f;
        mix                  = 0.f;
        buffer_locked_freeze = true;
        reversal_count       = 0.f;
        const float ratios[8] = {4.f, 2.f, 1.5f, 1.f, 0.75f, 0.5f, 0.25f, 0.125f};
        time_mult             = 1.f;
        times_dirty           = true;
        for(int g = 0; g < kNumGroups; g++)
            for(int v = 0; v < kNumVoices; v++)
                voice_ratio[g][v] = ratios[v];
        sample_rate = sr;
        limiter[0].Init();
        limiter[1].Init();
        voice_len      = 960000;
        mix_param      = 0.f;
        feedback_param = 0.f;
        reversal       = 0.f;
        dispersal      = 0.f;
        field_1bb4     = 0.f;

        // Per voice: 2·voice_len samples (tap and reverse reader) in each channel
        for(int v = 0; v < kNumVoices; v++)
        {
            float *a = buffer_a + voice_len * v * 2;
            float *b = buffer_b + voice_len * v * 2;
            taps[0].tap[v].Init(a, voice_len, sample_rate);
            reader[0][v].Init(a + voice_len, voice_len);
            taps[1].tap[v].Init(b, voice_len, sample_rate);
            reader[1][v].Init(b + voice_len, voice_len);
            fb_fx[0].chroma[v].Init(sample_rate);
            fb_fx[1].chroma[v].Init(sample_rate);
            in_fx[0].chroma[v].Init(sample_rate);
            in_fx[1].chroma[v].Init(sample_rate);
            level[v]     = 0.f;
            revmix[0][v] = 0.f;
            revmix[1][v] = 0.f;
            fwd[0][v]    = 0.f;
            fwd[1][v]    = 0.f;
        }
        memset(wet_l, 0, sizeof(wet_l) * 4); // wet_l, wet_r, out_l, out_r
        dc[0].Init(sample_rate);
        dc[1].Init(sample_rate);
        reverb.Init(reverb_buffer, sr);
        reverb_time        = reverb_amount * 0.78f + 0.2f;
        pending_delay_mode = delay_mode;
        mode_fade          = 1.f;
        feedback_mode_fade = 1.f;
        cascade_gain       = 1.f;
        reverb_preset      = REVERB_NORMAL;
        ping               = false;
        feedback_mode         = FEEDBACK_NORMAL;
        ratio_row             = 0;
        pending_feedback_mode = FEEDBACK_NORMAL;
        delay_mode_pending    = false;
        feedback_mode_pending = false;
        mix_mode              = MIX_LINEAR;
        input_mode            = 0;
        field_1bc0            = 1;
        reverb_lp             = 0.75f;
        reverb_diffusion      = 0.6f;

        // Test ping: 349.23 Hz sine (F4) with a short envelope
        ping_osc.Init(sample_rate);
        ping_osc.SetWaveform(daisysp::Oscillator::WAVE_SIN);
        ping_osc.SetFreq(349.23f);
        ping_env.Init(sample_rate);
        ping_env.SetTime(daisysp::ADENV_SEG_ATTACK, 0.01f);
        ping_env.SetTime(daisysp::ADENV_SEG_DECAY, 0.15f);

        freeze_buffer[0] = freeze_l;
        freeze_buffer[1] = freeze_r;
        looper[0].Init(freeze_buffer[0], 480000, sample_rate);
        looper[1].Init(freeze_buffer[1], 480000, sample_rate);
        period      = sample_rate * 0.5f;
        bpm         = 120.f;
        times_dirty = true;
        ratio_row   = 4;
    }

    // On every tempo pulse (DacCallback): quantized freeze, ping and relocking of the
    // freeze loopers every (multiplier) pulses.
    void OnTempoTick()
    {
        if(quantize_freeze && freeze_request)
        {
            frozen         = true;
            freeze_request = false;
            taps[0].SetTranspose(0.f);
            taps[1].SetTranspose(0.f);
        }
        if(ping)
            ping_env.Trigger();
        uint32_t beats;
        if(time_mult == 1.5f || time_mult == 0.75f)
            beats = 2;
        else
            beats = time_mult > 1.f ? (uint32_t)time_mult - 1 : 0;
        if(++relock_count > beats)
        {
            looper[0].relocking = true;
            looper[1].relocking = true;
            relock_count        = 0;
        }
    }

    // Tempo period in samples (clock or free time); recomputes the times
    void SetPeriod(float samples)
    {
        const int s = (int)samples;
        period      = s >= 0 ? (float)s : 1.f;
        times_dirty = true;
    }

    // @0x2400cef4: AudioCallback calls it on every block with (int)(chroma·6 − 0.5)
    void SetChromaMode(int mode)
    {
        for(int s = 0; s < 2; s++)
            for(int v = 0; v < kNumVoices; v++)
            {
                fb_fx[s].chroma[v].SetMode(mode);
                in_fx[s].chroma[v].SetMode(mode);
            }
    }

    // @0x24009f0c
    void Process(const float *const *in, float **out, size_t size)
    {
        // Reverse voices: reversal assigns 2·divisions readers (with hysteresis)
        for(int v = 0; v < kNumVoices; v++)
        {
            reversed[0][v] = false;
            reversed[1][v] = false;
        }
        const int   n2 = divisions * 2;
        const float x  = (float)n2 * reversal;
        reversal_count = (float)(int)(x + 0.5f + (x - reversal_count > 0.f ? -0.3f : 0.3f));
        if(n2 > 0)
        {
            const int n = (int)reversal_count;
            bool      flags[16];
            for(int i = 0; i < n2; i++)
                flags[i] = n > i;
            for(int i = 0; i < n2; i += 2)
            {
                reversed[0][i / 2] = flags[i];
                reversed[1][i / 2] = flags[i + 1];
            }
        }

        if(times_dirty)
        {
            UpdateTimes();
            times_dirty = false;
        }

        for(int v = 0; v < kNumVoices; v++)
        {
            fb_fx[0].chroma[v].SetAmount(depth);
            fb_fx[1].chroma[v].SetAmount(depth);
            in_fx[0].chroma[v].SetAmount(depth);
            in_fx[1].chroma[v].SetAmount(depth);
        }

        float feedback_target = feedback_param;
        float mix_target      = mix_param;
        if(!quantize_freeze && freeze_request)
        {
            frozen = true;
            for(int g = 0; g < kNumGroups; g++)
                for(int v = 0; v < kNumVoices; v++)
                    taps[g].tap[v].ResetTranspose();
            freeze_request = false;
        }
        if(frozen)
        {
            if(!buffer_locked_freeze)
                feedback_target = 1.f;
            if(freeze_always_wet)
                mix_target = 1.f;
            else if(freeze_punch_in && mix_target == 0.f)
                mix_target = 1.f;
        }

        const bool cascade = feedback_mode == FEEDBACK_CASCADE || feedback_mode == FEEDBACK_ADRIFT;
        float      tap_out[kNumGroups][kNumVoices];
        float      rev_out[kNumGroups][kNumVoices];
        for(int g = 0; g < kNumGroups; g++)
            for(int v = 0; v < kNumVoices; v++)
                tap_out[g][v] = rev_out[g][v] = 0.f;

        // Purge: lowers the level, the main loop clears the buffers and it rises again.
        // With Continuous Purge it keeps clearing while the gate is high.
        float purge_target = 1.f, purge_rate = 0.0002f;
        if(purge_request)
        {
            if(!purge_done)
            {
                purge_target = 0.f;
                purge_rate   = 0.0007f;
            }
            else if(!continuous_purge)
            {
                purge_request = false;
                purge_done    = false;
            }
            else if(!purge_released)
            {
                purge_target = 0.f;
                purge_rate   = 0.0007f;
            }
            else
            {
                purge_request  = false;
                purge_done     = false;
                purge_released = false;
            }
        }

        const float write_target   = frozen && !buffer_locked_freeze ? 0.f : 1.f;
        const float level_target   = 1.f - (float)(divisions - 1) * 0.1667f;
        const float cascade_target = cascade ? 0.f : 1.f;

        float dry_l[128], dry_r[128];
        for(size_t i = 0; i < size; i++)
        {
            float in_l, in_r;
            if(ping)
            {
                in_l = in_r = ping_osc.Process() * ping_env.Process();
            }
            else
            {
                in_l = in[0][i];
                in_r = in[1][i];
            }
            dry_l[i] = in_l;
            dry_r[i] = in_r;

            out_level += (level_target - out_level) * 0.005f;
            purge_level += purge_rate * (purge_target - purge_level);
            feedback += (feedback_target - feedback) * 0.0005f;
            write_gain += (write_target - write_gain) * 0.0005f;
            cascade_gain += (cascade_target - cascade_gain) * 0.005f;

            // Delay mode and feedback mode change, with a fade
            if(delay_mode_pending)
            {
                mode_fade *= 0.995f;
                if(feedback_mode_pending)
                    feedback_mode_fade *= 0.995f;
                else
                    feedback_mode_fade += (1.f - feedback_mode_fade) * 0.005f;
                if(mode_fade < 0.0003f)
                {
                    pending_clear = pending_clear && clear_on_mode_change;
                    if(pending_clear)
                    {
                        purge_request  = true;
                        pending_clear  = false;
                        purge_released = true;
                    }
                    delay_mode = pending_delay_mode;
                    ApplyDelayMode();
                    delay_mode_pending = false;
                    pending_clear      = false;
                }
            }
            else
            {
                mode_fade += (1.f - mode_fade) * 0.005f;
                if(feedback_mode_pending)
                    feedback_mode_fade *= 0.995f;
                else
                    feedback_mode_fade += (1.f - feedback_mode_fade) * 0.005f;
            }
            if(feedback_mode_pending && feedback_mode_fade < 0.0003f)
            {
                if(clear_on_mode_change && pending_clear)
                {
                    purge_request  = true;
                    pending_clear  = false;
                    purge_released = true;
                }
                feedback_mode         = pending_feedback_mode;
                feedback_mode_pending = false;
            }

            // Input (with its own Chroma) into the reverse readers
            float a, b;
            RouteInput(in_l, in_r, a, b);
            const float ca = in_fx[0].chroma[0].Process(a);
            const float cb = in_fx[1].chroma[0].Process(b);

            float xa = ca, xb = cb;
            for(size_t v = 0; v < kNumVoices; v++)
            {
                level[v] += ((v < divisions ? 1.f : 0.f) - level[v]) * 0.0005f;
                revmix[0][v] += ((float)reversed[0][v] - revmix[0][v]) * 0.0004f;
                revmix[1][v] += ((float)reversed[1][v] - revmix[1][v]) * 0.0004f;
                rev_out[0][v]
                    = revmix[0][v] * level[v] * reader[0][v].Process(xa * write_gain);
                rev_out[1][v]
                    = revmix[1][v] * level[v] * reader[1][v].Process(xb * write_gain);

                // With little feedback, a reverse voice mutes its normal tap
                const bool quiet = feedback < 0.02f;
                if(quiet && revmix[0][v] > 0.5f)
                    fwd[0][v] *= 0.9995f;
                else
                    fwd[0][v] += (1.f - fwd[0][v]) * 0.0005f;
                fwd[1][v] += ((quiet && revmix[1][v] > 0.5f ? 0.f : 1.f) - fwd[1][v]) * 0.0005f;

                tap_out[0][v] = fwd[0][v] * taps[0].tap[v].Process();
                tap_out[1][v] = fwd[1][v] * taps[1].tap[v].Process();

                // In Cascade/Adrift only the first voice receives the input
                xa = ca * cascade_gain;
                xb = cb * cascade_gain;
            }

            // Feedback: Chroma per voice and write into the tap buffer
            float sum_a = 0.f, sum_b = 0.f;
            for(size_t v = 0; v < kNumVoices; v++)
            {
                float ia, ib;
                RouteInput(in_l, in_r, ia, ib);
                if(v > 0 && cascade)
                    ia = ib = 0.f;

                // Source of each Chroma by mode. Ping Pong, Cascade and Adrift cross
                // the feedback between channels; Cascade and Adrift also chain voices.
                const int src = v == 0 ? kNumVoices - 1 : v - 1;
                float     oa, ob, fa, fb;
                switch(feedback_mode)
                {
                    case FEEDBACK_PING_PONG:
                        oa = fb_fx[0].chroma[v].Process(tap_out[0][v] * level[v]);
                        ob = fb_fx[1].chroma[v].Process(tap_out[1][v] * level[v]);
                        fa = feedback * ob;
                        fb = feedback * oa;
                        break;
                    case FEEDBACK_CASCADE:
                        oa = fb_fx[0].chroma[v].Process(tap_out[0][src]);
                        ob = fb_fx[1].chroma[v].Process(tap_out[1][src]);
                        fa = feedback * ob;
                        fb = feedback * oa;
                        oa *= level[v];
                        ob *= level[v];
                        break;
                    case FEEDBACK_ADRIFT:
                        if(v & 1)
                        {
                            oa = fb_fx[0].chroma[v].Process(tap_out[1][src]);
                            ob = fb_fx[1].chroma[v].Process(tap_out[0][src]);
                        }
                        else
                        {
                            oa = fb_fx[0].chroma[v].Process(tap_out[0][src]);
                            ob = fb_fx[1].chroma[v].Process(tap_out[1][src]);
                        }
                        fa = feedback * ob;
                        fb = feedback * oa;
                        oa *= level[v];
                        ob *= level[v];
                        break;
                    default:
                        oa = fb_fx[0].chroma[v].Process(tap_out[0][v] * level[v]);
                        ob = fb_fx[1].chroma[v].Process(tap_out[1][v] * level[v]);
                        fa = feedback * oa;
                        fb = feedback * ob;
                        break;
                }

                const float g
                    = write_gain * purge_level * mode_fade * feedback_mode_fade;
                taps[0].tap[v].Write((fa + (rev_out[0][v] + (1.f - revmix[0][v]) * ia)) * g
                                     + (1.f - write_gain) * tap_out[0][v]);
                taps[1].tap[v].Write((fb + (rev_out[1][v] + (1.f - revmix[1][v]) * ib)) * g
                                     + (1.f - write_gain) * tap_out[1][v]);
                sum_a += oa + rev_out[0][v];
                sum_b += ob + rev_out[1][v];
            }
            const float wl = dc[0].Process(sum_a * purge_level);
            const float wr = dc[1].Process(sum_b * purge_level);
            wet_l[i]       = out_level * mode_fade * feedback_mode_fade * wl;
            wet_r[i]       = out_level * mode_fade * feedback_mode_fade * wr;
        }

        limiter[0].ProcessBlock(wet_l, size, 1.2f);
        limiter[1].ProcessBlock(wet_r, size, 1.2f);
        if(buffer_locked_freeze)
        {
            looper[0].frozen   = frozen;
            looper[1].frozen   = frozen;
            const uint32_t len = (uint32_t)(period * time_mult);
            looper[0].SetLength(len);
            looper[1].SetLength(len);
            looper[0].Process(wet_l, out_l, size);
            looper[1].Process(wet_r, out_r, size);
        }
        else
        {
            memcpy(out_l, wet_l, size * sizeof(float));
            memcpy(out_r, wet_r, size * sizeof(float));
        }

        Mix(out, size, mix_target, dry_l, dry_r);
        ProcessReverb(out, size);

        if(purge_request && purge_level < 0.0003f)
            clear_request = true;
    }

    // Recomputes the times of the 8 taps and readers (tempo, resolution or dispersal changed)
    void UpdateTimes()
    {
        for(int g = 0; g < kNumGroups; g++)
            for(int v = 0; v < kNumVoices; v++)
            {
                int row = ratio_row;
                if(row < 1 || row > 4)
                    row = 0;
                float d = dispersal;
                if(v != 0)
                    d = d * 0.9375f + 0.0625f;
                const float r
                    = 1.f + (float)(int)(d * 16.f) * ((ratio_[g][row][v] - 1.f) / 16.f);
                taps[g].tap[v].SetDelay(period * time_mult * r);
                taps[g].tap[v].UpdateModFreq();
                reader[g][v].SetLength(period * time_mult * r);
            }
    }

    void SetSlewing(bool s)
    {
        for(int g = 0; g < kNumGroups; g++)
            for(int v = 0; v < kNumVoices; v++)
                taps[g].tap[v].slewing = s;
    }

    void SetSaturate(bool s)
    {
        for(int v = 0; v < kNumVoices; v++)
        {
            fb_fx[0].chroma[v].saturate = s;
            fb_fx[1].chroma[v].saturate = s;
            in_fx[0].chroma[v].saturate = s;
            in_fx[1].chroma[v].saturate = s;
        }
    }

    __attribute__((always_inline)) void ApplyDelayMode()
    {
        switch(delay_mode)
        {
            case DELAY_FADE:
                SetSlewing(false);
                taps[0].SetTranspose(0.f);
                taps[1].SetTranspose(0.f);
                SetSaturate(false);
                break;
            case DELAY_DOPPLER:
                SetSlewing(true);
                taps[0].SetTranspose(0.f);
                taps[1].SetTranspose(0.f);
                SetSaturate(false);
                break;
            case DELAY_SHIMMER:
                SetSlewing(true);
                if(!frozen)
                    for(int g = 0; g < kNumGroups; g++)
                        taps[g].SetTranspose((int)transpose_up);
                SetSaturate(true);
                break;
            case DELAY_DESHIMMER:
                SetSlewing(true);
                if(!frozen)
                    for(int g = 0; g < kNumGroups; g++)
                        taps[g].SetTranspose((int)transpose_down);
                SetSaturate(true);
                break;
            default: break;
        }
    }

    // Input into the engine by mode (0 stereo, 1 mono)
    void RouteInput(float l, float r, float &a, float &b) const
    {
        if(input_mode == 0)
        {
            a = input_level * l;
            b = input_level * r;
        }
        else if(input_mode == 1)
        {
            a = input_level * 0.5f * (l + r);
            b = feedback_mode == FEEDBACK_PING_PONG || feedback_mode == FEEDBACK_ADRIFT ? 0.f
                                                                                       : a;
        }
        else
        {
            a = b = 0.f;
        }
    }

    // Dry/wet mix at the output, with a one-block ramp. The wet term goes first: GCC
    // contracts the first product into an FMA, and the original fuses out·mix (not the dry);
    // with the other order the output differs by 1 ULP in some samples.
    void Mix(float **out, size_t size, float mix_target, const float *dry_l, const float *dry_r)
    {
        const float n       = (float)(int)size;
        const float mix_inc = (mix_target - mix) / n;
        float       wet_t, dry_t;
        switch(mix_mode)
        {
            case MIX_LINEAR:
            {
                size_t ramp = size;
                for(size_t i = 0; i < size; i++)
                {
                    if(ramp)
                    {
                        mix += mix_inc;
                        ramp--;
                    }
                    out[0][i] = out_l[i] * mix + dry_l[i] * input_level * (1.f - mix);
                    out[1][i] = out_r[i] * mix + dry_r[i] * input_level * (1.f - mix);
                }
                return;
            }
            case MIX_CONSTANT_POWER:
                wet_t = sinf(mix_target * HALFPI_F);
                dry_t = sinf((1.f - mix_target) * HALFPI_F);
                break;
            case MIX_TRANSITION:
                wet_t = fclamp(mix_target * 2.f, 0.f, 1.f);
                dry_t = fclamp((1.f - mix_target) * 2.f, 0.f, 1.f);
                break;
            case MIX_WET_AMOUNT:
                wet_t = mix_target * 2.f;
                dry_t = 1.f;
                break;
            default: return;
        }
        const float dry_inc = (dry_t - dry_gain) / n;
        const float wet_inc = (wet_t - wet_gain) / n;
        size_t      ramp    = size;
        for(size_t i = 0; i < size; i++)
        {
            if(ramp)
            {
                dry_gain += dry_inc;
                ramp--;
                wet_gain += wet_inc;
                mix += mix_inc;
            }
            out[0][i] = out_l[i] * wet_gain + dry_l[i] * input_level * dry_gain;
            out[1][i] = out_r[i] * wet_gain + dry_r[i] * input_level * dry_gain;
        }
    }

    void ProcessReverb(float **out, size_t size)
    {
        switch(reverb_preset)
        {
            case REVERB_NORMAL:
                reverb_lp        = 0.75f;
                reverb_diffusion = 0.6f;
                reverb_time      = reverb_amount * 0.78f + 0.2f;
                break;
            case REVERB_BRIGHT:
                reverb_lp        = 1.f;
                reverb_diffusion = 0.5f;
                reverb_time      = reverb_amount * 0.78f + 0.2f;
                break;
            case REVERB_DARK:
                reverb_lp        = 0.25f;
                reverb_diffusion = 0.66f;
                reverb_time      = reverb_amount * 0.78f + 0.2f;
                break;
            default: break;
        }
        // Diffusion follows reverb_amount, not reverb_diffusion (that is how the original does it)
        const float inv       = 1.f / (float)(int)size;
        const float amount_in = (reverb_amount - reverb_amount_cur) * inv;
        const float lp_in     = (reverb_lp - reverb_lp_cur) * inv;
        const float time_in   = (reverb_time - reverb_time_cur) * inv;
        const float diff_in   = (reverb_amount - reverb_diffusion_cur) * inv;
        size_t      ramp      = size;
        for(size_t i = 0; i < size; i++)
        {
            if(ramp)
            {
                reverb_amount_cur += amount_in;
                ramp--;
                reverb_lp_cur += lp_in;
                reverb_time_cur += time_in;
                reverb_diffusion_cur += diff_in;
            }
            reverb.set_amount(reverb_amount_cur);
            reverb.set_time(reverb_time_cur);
            reverb.set_lp(reverb_lp_cur);
            reverb.set_diffusion(reverb_diffusion_cur);
            reverb.Process(&out[0][i], &out[1][i]);
        }
    }

    // @0x24009ca8 (ctor): ratio_ is copied from .rodata 0x2402d284
    float ratio_[kNumGroups][5][8] = {
        {{1.f, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 16.f},
         {1.f, 2.f, 3.f, 4.f, 6.f, 8.f, 0.5f, 0.25f},
         {1.f, 4.f, 3.f, 2.f, 1.5f, 0.5f, 0.75f, 0.25f},
         {1.f, 0.5f, 2.f, 0.25f, 1.5f, 0.75f, 0.125f, 0.0625f},
         {0.125f, 2.f, 3.f, 0.33f, 1.f, 1.f, 1.f, 1.f}},
        {{1.f, 3.f, 2.f, 4.f, 6.f, 8.f, 12.f, 16.f},
         {1.f, 0.5f, 3.f, 4.f, 6.f, 8.f, 2.f, 0.25f},
         {1.f, 2.f, 4.f, 3.f, 0.5f, 0.75f, 0.25f, 1.5f},
         {1.f, 0.5f, 2.f, 0.25f, 1.5f, 0.75f, 0.125f, 0.0625f},
         {2.f, 0.25f, 0.375f, 0.1667f, 1.f, 1.f, 1.f, 1.f}},
    };                                 // +0x000
    float   *buffer_a;                 // +0x140 SDRAM 0xc03a9800
    float   *buffer_b;                 // +0x144 SDRAM 0xc20f5800
    uint32_t voice_len;                // +0x148 960000 samples per voice
    float    sample_rate;              // +0x14c
    size_t   divisions;                // +0x150 active voices (sensors: 1..4)
    int      field_154;                // +0x154 Init: 4
    uint32_t field_158;                // +0x158 Init: 0. TODO(recon): main writes it and nobody
                                       // reads it, not in v1.1 nor in the RC1 either
    float    time_mult;                // +0x15c resolution (tempo multiplier)
    float    depth;                    // +0x160 Chroma amount
    float    feedback;                 // +0x164 smoothed
    float    mix;                      // +0x168 smoothed (per-block ramp)
    float    wet_gain;                 // +0x16c
    float    dry_gain;                 // +0x170
    bool     frozen;                   // +0x174
    uint8_t  pad_175[3];
    float    write_gain;               // +0x178 0 with freeze (without locked buffer)
    float    level[kNumVoices];        // +0x17c 1 if the voice is active
    float    revmix[kNumGroups][kNumVoices]; // +0x18c 1 if the voice plays in reverse
    float    fwd[kNumGroups][kNumVoices];    // +0x1ac gain of the normal tap
    Reverser reader[kNumGroups][kNumVoices]; // +0x1cc
    TapBank  taps[kNumGroups];         // +0x3ac
    ChromaBank fb_fx[kNumGroups];      // +0x80c feedback Chroma
    ChromaBank in_fx[kNumGroups];      // +0x11cc input Chroma (only [0] is used)
    daisysp::DcBlock dc[2];            // +0x1b8c
    float    mix_param;                // +0x1ba4 0 if < 0.04
    float    feedback_param;           // +0x1ba8
    float    reversal;                 // +0x1bac
    float    dispersal;                // +0x1bb0
    float    field_1bb4;               // +0x1bb4 Init: 0. TODO(recon): never read in v1.1/RC1/v1.2
    float    bpm;                      // +0x1bb8 Init: 120. TODO(recon): only main writes it (same in v1.1)
    float    period;                   // +0x1bbc tempo in samples
    int      field_1bc0;               // +0x1bc0 Init: 1. TODO(recon): not in v1.1; new in 1.2 and unused
    float    voice_ratio[kNumGroups][kNumVoices]; // +0x1bc4 Init: {4, 2, 1.5, 1}. TODO(recon)
    int      feedback_mode;            // +0x1be4 FeedbackMode (Settings)
    int      ratio_row;                // +0x1be8 row of ratio_ (1..4; any other value → 0)
    uint8_t  delay_mode;               // +0x1bec DelayMode (Settings)
    uint8_t  pad_1bed[0x3];
    int      mix_mode;                 // +0x1bf0 MixMode
    int      input_mode;               // +0x1bf4 0 stereo, 1 mono: the manual's "Summed Mono" (Settings)
    bool     times_dirty;              // +0x1bf8
    bool     purge_request;            // +0x1bf9
    bool     purge_done;               // +0x1bfa set by the main loop after clearing
    uint8_t  pad_1bfb;
    float    purge_level;              // +0x1bfc
    bool     clear_request;            // +0x1c00 handled by the main loop
    bool     continuous_purge;         // +0x1c01 "Continuous Purge"
    bool     purge_released;           // +0x1c02 purge gate released
    bool     reversed[kNumGroups][kNumVoices]; // +0x1c03
    // Gap with no accesses; it is the same in the RC1 (v1.2.0-dev10) and in v1.1.
    uint8_t  pad_1c0b[0x800c - 0x1c0b];
    float    reversal_count;           // +0x800c
    float    out_level;                // +0x8010 compensates for the number of voices
    bool     freeze_request;           // +0x8014
    uint8_t  pad_8015[0x3];
    float    transpose_up;             // +0x8018 "Transpose Up" (default 12)
    float    transpose_down;           // +0x801c "Transpose Down" (default -12)
    bool     freeze_always_wet;        // +0x8020 "Freeze Mix Behavior" = Always Wet
    bool     freeze_punch_in;          // +0x8021 "Freeze Mix Behavior" = Punch In
    bool     quantize_freeze;          // +0x8022 "Quantize Freeze" (default true)
    bool     clear_on_mode_change;     // +0x8023 "Clear on Mode Change"
    bool     buffer_locked_freeze;     // +0x8024 "Buffer Locked Freeze" (default true)
    uint8_t  pad_8025[0x3];
    float    input_level;              // +0x8028 shift + attenuverter 1 (Settings)
    float    wet_l[128];               // +0x802c
    float    wet_r[128];               // +0x822c
    float    out_l[128];               // +0x842c
    float    out_r[128];               // +0x862c
    float   *freeze_buffer[2];         // +0x882c SDRAM 0xc0000000 / 0xc01d4c00
    uint32_t relock_count;             // +0x8834 tempo pulses since the last relock
    bool     delay_mode_pending;       // +0x8838
    uint8_t  pending_delay_mode;       // +0x8839
    bool     pending_clear;            // +0x883a
    uint8_t  pad_883b;
    float    mode_fade;                // +0x883c
    bool     feedback_mode_pending;    // +0x8840
    uint8_t  pad_8841[0x3];
    int      pending_feedback_mode;    // +0x8844
    float    feedback_mode_fade;       // +0x8848
    float    cascade_gain;             // +0x884c
    FreezeLooper looper[2];            // +0x8850
    daisysp::Limiter limiter[2];       // +0x88d8
    Reverb   reverb;                   // +0x88e0
    uint16_t reverb_buffer[32768];     // +0x8914
    float    reverb_amount;            // +0x18914 shift + pot 2 (Settings)
    float    reverb_lp;                // +0x18918
    float    reverb_diffusion;         // +0x1891c unused (see ProcessReverb)
    float    reverb_time;              // +0x18920
    float    reverb_amount_cur;        // +0x18924
    float    reverb_lp_cur;            // +0x18928
    float    reverb_diffusion_cur;     // +0x1892c
    float    reverb_time_cur;          // +0x18930
    int      reverb_preset;            // +0x18934 ReverbPreset
    bool     ping;                     // +0x18938 test input (oscillator × envelope)
    uint8_t  pad_18939[0x7];
    daisysp::Oscillator ping_osc;      // +0x18940
    daisysp::AdEnv      ping_env;      // +0x18970
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(NautilusEngine, buffer_a) == 0x140, "layout");
static_assert(offsetof(NautilusEngine, level) == 0x17c, "layout");
static_assert(offsetof(NautilusEngine, reader) == 0x1cc, "layout");
static_assert(offsetof(NautilusEngine, taps) == 0x3ac, "layout");
static_assert(offsetof(NautilusEngine, fb_fx) == 0x80c, "layout");
static_assert(offsetof(NautilusEngine, in_fx) == 0x11cc, "layout");
static_assert(offsetof(NautilusEngine, dc) == 0x1b8c, "layout");
static_assert(offsetof(NautilusEngine, period) == 0x1bbc, "layout");
static_assert(offsetof(NautilusEngine, feedback_mode) == 0x1be4, "layout");
static_assert(offsetof(NautilusEngine, mix_mode) == 0x1bf0, "layout");
static_assert(offsetof(NautilusEngine, purge_level) == 0x1bfc, "layout");
static_assert(offsetof(NautilusEngine, clear_request) == 0x1c00, "layout");
static_assert(offsetof(NautilusEngine, reversed) == 0x1c03, "layout");
static_assert(offsetof(NautilusEngine, reversal_count) == 0x800c, "layout");
static_assert(offsetof(NautilusEngine, transpose_up) == 0x8018, "layout");
static_assert(offsetof(NautilusEngine, input_level) == 0x8028, "layout");
static_assert(offsetof(NautilusEngine, out_r) == 0x862c, "layout");
static_assert(offsetof(NautilusEngine, delay_mode_pending) == 0x8838, "layout");
static_assert(offsetof(NautilusEngine, pending_feedback_mode) == 0x8844, "layout");
static_assert(offsetof(NautilusEngine, looper) == 0x8850, "layout");
static_assert(offsetof(NautilusEngine, limiter) == 0x88d8, "layout");
static_assert(offsetof(NautilusEngine, reverb) == 0x88e0, "layout");
static_assert(offsetof(NautilusEngine, reverb_amount) == 0x18914, "layout");
static_assert(offsetof(NautilusEngine, reverb_preset) == 0x18934, "layout");
static_assert(offsetof(NautilusEngine, ping_osc) == 0x18940, "layout");
static_assert(offsetof(NautilusEngine, ping_env) == 0x18970, "layout");
static_assert(sizeof(NautilusEngine) == 0x189a8, "layout");
#pragma GCC diagnostic pop

} // namespace nautilus
