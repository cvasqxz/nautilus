// Tilt filter for the feedback path. Replaces Chroma (chroma.h): Depth at the centre leaves
// the repeats untouched, turning it left makes every repeat darker and turning it right
// makes every repeat thinner. It sits where each Chroma sat (one per voice in the feedback,
// one per channel at the input of the reverse readers), so the tilt builds up on every
// pass through the delay.
#pragma once

#include <stdint.h>
#include "chroma.h" // RationalTanh
#include "daisysp.h"

namespace nautilus
{
using daisysp::fclamp;

// Half-width of the flat zone around the centre of Depth (in tilt units, -1..1)
constexpr float kTiltDeadZone = 0.04f;

class Tilt
{
  public:
    void Init()
    {
        tilt_          = 0.f;
        tilt_smoothed_ = 0.f;
        lp_            = 0.f;
        saturate       = false;
    }

    // -1 = darkest, 0 = flat, +1 = brightest
    void  SetTilt(float t) { tilt_ = fclamp(t, -1.f, 1.f); }
    float GetTilt() const { return tilt_; }

    float Process(float in)
    {
        // The soft clip of Chroma (Saturation option) is kept as it was
        const float x = saturate ? fclamp(RationalTanh(in), -1.8f, 1.8f) : in;

        tilt_smoothed_ += (tilt_ - tilt_smoothed_) * 0.001f; // ~20 ms
        lp_ += kSplit * (x - lp_);
        const float lo = lp_, hi = x - lp_;
        const float t  = tilt_smoothed_;
        // lo + hi == x, so with the tilt at 0 the signal passes unchanged
        return t < 0.f ? lo + hi * (1.f + t) : lo * (1.f - t) + hi;
    }

    bool saturate;

  private:
    // One-pole split at ~640 Hz (48 kHz): below it is "low", above it "high"
    static constexpr float kSplit = 0.08f;

    float tilt_;
    float tilt_smoothed_;
    float lp_;
};

// Takes the place of a Chroma (0x138 bytes) so that the layout of the engine does not change
struct TiltSlot : Tilt
{
    uint8_t pad[0x138 - sizeof(Tilt)];
};
static_assert(sizeof(TiltSlot) == 0x138, "layout");

} // namespace nautilus
