// Nautilus (Qu-Bit) v1.2.0 — RGB color for the LEDs
// Same interface and behavior as daisy::Color after libDaisy bbfa87cb (#639), which the
// original uses on top of libDaisy v7.1.0: zero-initialized, constructible from r, g, b,
// with setters, a saturating operator+ and Blend. Init(r, g, b) clamps to [0, 1] like
// daisy::Color::Init.
#pragma once

namespace nautilus
{

class Color
{
  public:
    Color() : red_(0.f), green_(0.f), blue_(0.f) {}
    Color(float r, float g, float b) : red_(r), green_(g), blue_(b) {}
    // User-provided, as in daisy::Color: it makes Color non-trivial, so it is passed by
    // reference to a temporary instead of in registers, like in the original.
    ~Color() {}

    // Out of line, as daisy::Color::Init is in the original (a libDaisy function)
    __attribute__((noinline)) void Init(float red, float green, float blue)
    {
        red_   = Clamp01(red);
        green_ = Clamp01(green);
        blue_  = Clamp01(blue);
    }

    inline float Red() const { return red_; }
    inline float Green() const { return green_; }
    inline float Blue() const { return blue_; }

    inline void SetRed(const float amt) { red_ = amt; }
    inline void SetGreen(const float amt) { green_ = amt; }
    inline void SetBlue(const float amt) { blue_ = amt; }

    /** Returns a scaled color by a float */
    Color operator*(float scale)
    {
        Color c;
        c.Init(red_ * scale, green_ * scale, blue_ * scale);
        return c;
    }

    /** Adds another color to this one, saturating at 1 */
    Color operator+(Color rhs)
    {
        float r = red_ + rhs.Red();
        float g = green_ + rhs.Green();
        float b = blue_ + rhs.Blue();
        if(r > 1.f)
            r = 1.f;
        if(g > 1.f)
            g = 1.f;
        if(b > 1.f)
            b = 1.f;
        return Color(r, g, b);
    }

    /** Returns a color that is blended between a and b */
    static Color Blend(const Color a, const Color b, const float amt)
    {
        float scalar = amt > 1.f ? 1.f : amt < 0.f ? 0.f : amt;
        float nr     = a.Red() + (b.Red() - a.Red()) * scalar;
        float ng     = a.Green() + (b.Green() - a.Green()) * scalar;
        float nb     = a.Blue() + (b.Blue() - a.Blue()) * scalar;
        return Color(nr, ng, nb);
    }

  private:
    // Same expression as daisy::clamp
    static inline float Clamp01(float in)
    {
        return (in < 0.f) ? 0.f : (in > 1.f) ? 1.f : in;
    }

    float red_, green_, blue_;
};

} // namespace nautilus
