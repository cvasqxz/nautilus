// Nautilus (Qu-Bit) v1.2.0 — reverb (in the engine, engine + 0x88e0)
// Process @0x240097e0 (one sample per call).
//
// Port of the Mutable Instruments Rings reverb (rings/dsp/fx/reverb.h and
// rings/dsp/fx/fx_engine.h, from pichenettes/eurorack):
//   Copyright 2014 Emilie Gillet. MIT License.
// Differences seen in the binary with respect to the original:
//   - Process works one sample at a time (without the `while(size--)` loop).
//   - The LFOs are not stmlib::CosineOscillator: they are phase + frequency and cosf();
//     each read advances the phase, and Start also advances it every 32 samples.
//   - Init does not call Clear() (the buffer is in .bss) and sets input_gain = 0.3.
// Changes in this version: the delay lines are stored as float (see DataTypeFloat).
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "daisysp.h"

namespace nautilus
{

#define TAIL , -1

enum LfoIndex
{
    LFO_1,
    LFO_2,
};

// FORMAT_32_BIT (as in Rings' fx_engine.h). The original stores the delay lines as 16-bit
// integers (FORMAT_16_BIT), which adds quantization noise around -75 dBFS to the tail and
// clips the stored samples at +/-1.
struct DataTypeFloat
{
    typedef float T;
    static inline float Decompress(T value) { return value; }
    static inline T     Compress(float value) { return value; }
};

template <size_t size>
class FxEngine
{
  public:
    typedef typename DataTypeFloat::T T;

    void Init(T *buffer) { buffer_ = buffer; }

    struct Empty
    {
    };

    template <int32_t l, typename TT = Empty>
    struct Reserve
    {
        typedef TT Tail;
        enum
        {
            length = l
        };
    };

    template <typename Memory, int32_t index>
    struct DelayLine
    {
        enum
        {
            length = DelayLine<typename Memory::Tail, index - 1>::length,
            base   = DelayLine<Memory, index - 1>::base
                   + DelayLine<Memory, index - 1>::length + 1
        };
    };

    template <typename Memory>
    struct DelayLine<Memory, 0>
    {
        enum
        {
            length = Memory::length,
            base   = 0
        };
    };

    class Context
    {
        friend class FxEngine;

      public:
        inline void Load(float value) { accumulator_ = value; }
        inline void Read(float value, float scale) { accumulator_ += value * scale; }
        inline void Read(float value) { accumulator_ += value; }
        inline void Write(float &value) { value = accumulator_; }
        inline void Write(float &value, float scale)
        {
            value = accumulator_;
            accumulator_ *= scale;
        }

        template <typename D>
        inline void Write(D &d, int32_t offset, float scale)
        {
            T w = DataTypeFloat::Compress(accumulator_);
            if(offset == -1)
                buffer_[(write_ptr_ + D::base + D::length - 1) & MASK] = w;
            else
                buffer_[(write_ptr_ + D::base + offset) & MASK] = w;
            accumulator_ *= scale;
        }

        template <typename D>
        inline void Write(D &d, float scale)
        {
            Write(d, 0, scale);
        }

        template <typename D>
        inline void WriteAllPass(D &d, int32_t offset, float scale)
        {
            Write(d, offset, scale);
            accumulator_ += previous_read_;
        }

        template <typename D>
        inline void WriteAllPass(D &d, float scale)
        {
            WriteAllPass(d, 0, scale);
        }

        template <typename D>
        inline void Read(D &d, int32_t offset, float scale)
        {
            T r;
            if(offset == -1)
                r = buffer_[(write_ptr_ + D::base + D::length - 1) & MASK];
            else
                r = buffer_[(write_ptr_ + D::base + offset) & MASK];
            float r_f      = DataTypeFloat::Decompress(r);
            previous_read_ = r_f;
            accumulator_ += r_f * scale;
        }

        template <typename D>
        inline void Read(D &d, float scale)
        {
            Read(d, 0, scale);
        }

        inline void Lp(float &state, float coefficient)
        {
            state += coefficient * (accumulator_ - state);
            accumulator_ = state;
        }

        template <typename D>
        inline void Interpolate(D &d, float offset, LfoIndex index, float amplitude, float scale)
        {
            offset += amplitude * engine_->LfoValue(index);
            int32_t offset_integral   = static_cast<int32_t>(offset);
            float   offset_fractional = offset - static_cast<float>(offset_integral);
            float   a                 = DataTypeFloat::Decompress(
                buffer_[(write_ptr_ + offset_integral + D::base) & MASK]);
            float b = DataTypeFloat::Decompress(
                buffer_[(write_ptr_ + offset_integral + D::base + 1) & MASK]);
            float x        = a + (b - a) * offset_fractional;
            previous_read_ = x;
            accumulator_ += x * scale;
        }

      private:
        float     accumulator_;
        float     previous_read_;
        FxEngine *engine_;
        T        *buffer_;
        int32_t   write_ptr_;
    };

    inline void SetLfoFrequency(LfoIndex index, float frequency)
    {
        lfo_phase_[index] = 0.f;
        lfo_freq_[index]  = frequency;
    }

    inline float LfoValue(LfoIndex index)
    {
        lfo_phase_[index] += lfo_freq_[index];
        if(lfo_phase_[index] >= 1.f)
            lfo_phase_[index] -= 1.f;
        return cosf(lfo_phase_[index] * TWOPI_F);
    }

    inline void Start(Context *c)
    {
        --write_ptr_;
        if(write_ptr_ < 0)
            write_ptr_ += size;
        c->accumulator_   = 0.0f;
        c->previous_read_ = 0.0f;
        c->engine_        = this;
        c->buffer_        = buffer_;
        c->write_ptr_     = write_ptr_;
        if((write_ptr_ & 31) == 0)
        {
            for(int i = 0; i < 2; i++)
            {
                lfo_phase_[i] += lfo_freq_[i];
                if(lfo_phase_[i] >= 1.f)
                    lfo_phase_[i] -= 1.f;
            }
        }
    }

  private:
    enum
    {
        MASK = size - 1
    };

    int32_t write_ptr_;    // +0x00
    T      *buffer_;       // +0x04
    float   lfo_phase_[2]; // +0x08
    float   lfo_freq_[2];  // +0x10
};

class Reverb
{
  public:
    void Init(float *buffer, float sample_rate)
    {
        engine_.Init(buffer);
        engine_.SetLfoFrequency(LFO_1, 0.5f / sample_rate);
        engine_.SetLfoFrequency(LFO_2, 0.3f / sample_rate);
        lp_         = 0.7f;
        diffusion_  = 0.625f;
        input_gain_ = 0.3f;
        amount_     = 0.f;
    }

    // @0x240097e0
    void Process(float *left, float *right)
    {
        typedef E::Reserve<150,
                E::Reserve<214,
                E::Reserve<319,
                E::Reserve<527,
                E::Reserve<2182,
                E::Reserve<2690,
                E::Reserve<4501,
                E::Reserve<2525,
                E::Reserve<2197,
                E::Reserve<6312> > > > > > > > > > Memory;
        E::DelayLine<Memory, 0> ap1;
        E::DelayLine<Memory, 1> ap2;
        E::DelayLine<Memory, 2> ap3;
        E::DelayLine<Memory, 3> ap4;
        E::DelayLine<Memory, 4> dap1a;
        E::DelayLine<Memory, 5> dap1b;
        E::DelayLine<Memory, 6> del1;
        E::DelayLine<Memory, 7> dap2a;
        E::DelayLine<Memory, 8> dap2b;
        E::DelayLine<Memory, 9> del2;
        E::Context c;

        const float kap    = diffusion_;
        const float klp    = lp_;
        const float krt    = reverb_time_;
        const float amount = amount_;
        const float gain   = input_gain_;

        float lp_1 = lp_decay_1_;
        float lp_2 = lp_decay_2_;

        float wet;
        float apout = 0.0f;
        engine_.Start(&c);

        c.Read(*left + *right, gain);

        // Diffusion with 4 allpasses
        c.Read(ap1 TAIL, kap);
        c.WriteAllPass(ap1, -kap);
        c.Read(ap2 TAIL, kap);
        c.WriteAllPass(ap2, -kap);
        c.Read(ap3 TAIL, kap);
        c.WriteAllPass(ap3, -kap);
        c.Read(ap4 TAIL, kap);
        c.WriteAllPass(ap4, -kap);
        c.Write(apout);

        // Main loop
        c.Load(apout);
        c.Interpolate(del2, 6261.0f, LFO_2, 50.0f, krt);
        c.Lp(lp_1, klp);
        c.Read(dap1a TAIL, -kap);
        c.WriteAllPass(dap1a, kap);
        c.Read(dap1b TAIL, kap);
        c.WriteAllPass(dap1b, -kap);
        c.Write(del1, 2.0f);
        c.Write(wet, 0.0f);

        *left += (wet - *left) * amount;

        c.Load(apout);
        c.Interpolate(del1, 4460.0f, LFO_1, 40.0f, krt);
        c.Lp(lp_2, klp);
        c.Read(dap2a TAIL, kap);
        c.WriteAllPass(dap2a, -kap);
        c.Read(dap2b TAIL, -kap);
        c.WriteAllPass(dap2b, kap);
        c.Write(del2, 2.0f);
        c.Write(wet, 0.0f);

        *right += (wet - *right) * amount;

        lp_decay_1_ = lp_1;
        lp_decay_2_ = lp_2;
    }

    inline void set_amount(float amount) { amount_ = amount; }
    inline void set_input_gain(float input_gain) { input_gain_ = input_gain; }
    inline void set_time(float reverb_time) { reverb_time_ = reverb_time; }
    inline void set_diffusion(float diffusion) { diffusion_ = diffusion; }
    inline void set_lp(float lp) { lp_ = lp; }

  private:
    typedef FxEngine<32768> E;
    E engine_; // +0x00

    float amount_;      // +0x18
    float input_gain_;  // +0x1c
    float reverb_time_; // +0x20
    float diffusion_;   // +0x24
    float lp_;          // +0x28

    float lp_decay_1_; // +0x2c
    float lp_decay_2_; // +0x30
};

static_assert(sizeof(Reverb) == 0x34, "layout");

#undef TAIL

} // namespace nautilus
