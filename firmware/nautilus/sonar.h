// Sonar output quantizer (not in the original). The original's Stepped Voltage (one equal
// step per open gate, sized by Sensors) is quantized at 1 V/octave to the nearest note of
// the chosen scale, so that it can drive the pitch of an oscillator in tune. The DAC
// (0..4095) comes out as 0..5 V (manual: "Sonar CV output range: 0V to +5V"): 60 semitones
// over the full range, 0 V = the root. The original's two clock modes (picked in
// options.json) are gone.
#pragma once

#include <math.h>
#include <stdint.h>

namespace nautilus
{
static constexpr int kSonarCodesPerVolt = 4095 / 5; // 819 (exact)
static constexpr int kSonarSemitones    = 5 * 12;   // 60 semitones over the full range

// Shift + Freeze steps through them; the number and colour of each are shown on the Freeze
// LED (1 = colour 0, ..., 6 = colour 5)
struct SonarScale
{
    uint8_t size;
    uint8_t degrees[12];
};
static constexpr SonarScale kSonarScales[] = {
    {12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}}, // 1 chromatic
    {7, {0, 2, 4, 5, 7, 9, 11}},                  // 2 major
    {7, {0, 2, 3, 5, 7, 8, 10}},                  // 3 natural minor
    {5, {0, 2, 4, 7, 9}},                         // 4 major pentatonic
    {5, {0, 3, 5, 7, 10}},                        // 5 minor pentatonic
    {6, {0, 3, 5, 6, 7, 10}},                     // 6 blues
};
static constexpr int kNumSonarScales = sizeof(kSonarScales) / sizeof(kSonarScales[0]);

// Nearest note of the scale (the lower one on a tie), as a DAC code
inline uint16_t QuantizeSonar(int value, int scale)
{
    const float       st   = value * ((float)kSonarSemitones / 4095.f);
    const SonarScale &sc   = kSonarScales[scale];
    const int         oct  = (int)st / 12;
    int               best = 0;
    float             dist = 1e9f;
    for(int o = oct; o <= oct + 1; o++)
        for(int d = 0; d < sc.size; d++)
        {
            const int n = 12 * o + sc.degrees[d];
            if(n > kSonarSemitones)
                continue;
            const float e = fabsf(n - st);
            if(e < dist)
            {
                dist = e;
                best = n;
            }
        }
    return (uint16_t)((best * 4095 * 2 + kSonarSemitones) / (kSonarSemitones * 2));
}

} // namespace nautilus
