// Sonar output (not in the original): a random walk over the notes of a scale, at 1 V/octave.
//
// The original's Stepped Voltage (one equal step per open gate) only had 2N + 1 levels and
// changed on every gate edge. Here the gate edges of the delay voices are only the chances
// to change: on each one, the note changes with a probability of 25 %. When it changes, it
// jumps to any note of the range with a probability of 25 %; otherwise it moves up or down
// by up to 7 degrees of the scale and bounces off the ends of the range (C1 to C4, 1 V to
// 4 V). The randomness comes from the hardware RNG of the STM32H7. The original's two
// clock modes (picked in options.json) are gone.
#pragma once

#include <stdint.h>
#include "per/rng.h"

namespace nautilus
{
static constexpr int kSonarCodesPerVolt = 4095 / 5; // 819 (exact): 0..4095 = 0..5 V
static constexpr int kSonarLowNote      = 12;       // C1 (1 V); 0 V = C0
static constexpr int kSonarHighNote     = 48;       // C4 (4 V)
static constexpr int kSonarMaxStep      = 7;        // degrees of the scale

// Probabilities as fractions of 2^16
static constexpr uint32_t kSonarChangeChance = 65536 / 4; // 25 %
static constexpr uint32_t kSonarJumpChance   = 65536 / 4; // 25 %

// Shift + Freeze steps through them; the number and colour of each are shown on the Freeze
// LED (1 = colour 0, ..., 5 = colour 4)
struct SonarScale
{
    uint8_t size;
    uint8_t degrees[7];
};
static constexpr SonarScale kSonarScales[] = {
    {7, {0, 2, 4, 5, 7, 9, 11}}, // 1 major
    {7, {0, 2, 3, 5, 7, 8, 10}}, // 2 natural minor
    {5, {0, 2, 4, 7, 9}},        // 3 major pentatonic
    {5, {0, 3, 5, 7, 10}},       // 4 minor pentatonic
    {6, {0, 3, 5, 6, 7, 10}},    // 5 blues
};
static constexpr int kNumSonarScales = sizeof(kSonarScales) / sizeof(kSonarScales[0]);

// Most notes in the range: 3 octaves of the largest scale, plus the top C
static constexpr int kSonarMaxNotes = (kSonarHighNote - kSonarLowNote) / 12 * 7 + 1;

class SonarWalk
{
  public:
    void Init(int scale)
    {
        rng_   = 0x9e3779b9u;
        scale_ = -1;
        SetScale(scale);
        degree_ = (num_notes_ - 1) / 2;
    }

    // Rebuilds the notes of the range when the scale changes (keeps the degree in range)
    void SetScale(int scale)
    {
        if(scale == scale_)
            return;
        scale_              = scale;
        const SonarScale &s = kSonarScales[scale];
        num_notes_          = 0;
        for(int n = kSonarLowNote; n <= kSonarHighNote; n++)
            for(int d = 0; d < s.size; d++)
                if(n % 12 == s.degrees[d])
                    notes_[num_notes_++] = n;
        if(degree_ >= num_notes_)
            degree_ = num_notes_ - 1;
    }

    // Called on every gate edge of the delay voices
    void Trigger()
    {
        const uint32_t r = Next();
        if((r & 0xffff) >= kSonarChangeChance)
            return;
        const uint32_t r2   = Next();
        const int      last = num_notes_ - 1;
        if((r2 & 0xffff) < kSonarJumpChance)
            degree_ = (int)(((r2 >> 16) * (uint32_t)num_notes_) >> 16);
        else
        {
            // Uniform in -1..1, rounded to a step of -kSonarMaxStep..kSonarMaxStep
            const float u    = (float)(r2 >> 16) * (2.f / 65535.f) - 1.f;
            const float s    = u * kSonarMaxStep;
            int         deg  = degree_ + (int)(s < 0.f ? s - 0.5f : s + 0.5f);
            if(deg < 0)
                deg = -deg;
            if(deg > last)
                deg = 2 * last - deg;
            degree_ = deg < 0 ? 0 : deg > last ? last : deg;
        }
    }

    // DAC code of the current note
    uint16_t Code() const
    {
        const int n = notes_[degree_];
        return (uint16_t)((n * 4095 * 2 + 60) / 120);
    }

  private:
    // xorshift32, reseeded from the hardware RNG whenever it has a value ready, so that it
    // never waits for the RNG (this runs in the DAC interrupt)
    uint32_t Next()
    {
        if(daisy::Random::IsReady())
            rng_ ^= daisy::Random::GetValue();
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        if(rng_ == 0)
            rng_ = 0x9e3779b9u;
        return rng_;
    }

    uint32_t rng_;
    int      scale_;
    int      num_notes_;
    int      degree_;
    uint8_t  notes_[kSonarMaxNotes];
};

} // namespace nautilus
