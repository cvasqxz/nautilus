// Tilt filter for the feedback path. Replaces Chroma (chroma.h, kept for reference but not
// built): Depth at the centre leaves the repeats untouched, turning it left makes every
// repeat darker and turning it right makes every repeat thinner. It sits where each Chroma
// sat (one per voice in the feedback, one per channel at the input of the reverse readers),
// so the tilt builds up on every pass through the delay.
#pragma once

#include <stdint.h>
#include "daisysp.h"

namespace nautilus
{
using daisysp::fclamp;

// x·(x²+27)/(9x²+27): rational approximation of tanh (Chroma's soft clip)
inline float RationalTanh(float x)
{
    const float x2 = x * x;
    return (x2 + 27.f) * x / (x2 * 9.f + 27.f);
}

// Half-width of the flat zone around the centre of Depth (in tilt units, -1..1)
constexpr float kTiltDeadZone = 0.04f;

// Depth (0..1) to tilt: -1 = darkest, 0 = flat (with a small dead zone so that the pot can
// rest there), +1 = brightest
inline float DepthToTilt(float depth)
{
    const float t = depth * 2.f - 1.f;
    if(t > -kTiltDeadZone && t < kTiltDeadZone)
        return 0.f;
    return fclamp((t > 0.f ? t - kTiltDeadZone : t + kTiltDeadZone) / (1.f - kTiltDeadZone),
                  -1.f,
                  1.f);
}

class Tilt
{
  public:
    void Init()
    {
        lp_      = 0.f;
        saturate = false;
    }

    // `t`: tilt, already smoothed by the engine (the same for every instance)
    float Process(float in, float t)
    {
        // The soft clip of Chroma (Saturation option) is kept as it was
        const float x = saturate ? fclamp(RationalTanh(in), -1.8f, 1.8f) : in;
        daisysp::fonepole(lp_, x, kSplit);
        const float lo = lp_, hi = x - lp_;
        // lo + hi == x, so with the tilt at 0 the signal passes unchanged
        return t < 0.f ? lo + hi * (1.f + t) : lo * (1.f - t) + hi;
    }

    bool saturate;

  private:
    // One-pole split at ~640 Hz (48 kHz): below it is "low", above it "high"
    static constexpr float kSplit = 0.08f;

    float lp_;
};

// Takes the place of a Chroma (0x138 bytes) so that the layout of the engine does not change
struct TiltSlot : Tilt
{
    uint8_t pad[0x138 - sizeof(Tilt)];
};
static_assert(sizeof(TiltSlot) == 0x138, "layout");

} // namespace nautilus
