// Nautilus (Qu-Bit) v1.2.0 — interface: pages, event queue, UI and pot monitor
// Global object at 0x24079af0 (0x9cc bytes). The UI draws on the LEDs (canvas 0).
#pragma once

#include <stddef.h>
#include "daisy_seed.h"
#include "daisysp.h"
#include "nautilus_hw.h"
#include "nautilus_engine.h"
#include "clock_lfos.h"

namespace nautilus
{
using namespace daisy;
using daisysp::fclamp;

static constexpr int kNumColors = 6; // "Color 0".."Color 5" in options.json

// Main page. Vtable 0x2402dcf4.
class MainPage : public UiPage
{
  public:
    bool OnButton(uint16_t buttonID,
                  uint8_t  numberOfPresses,
                  bool     isRetriggering) override; // @0x240043a4
    bool OnPotMoved(uint16_t potID, float newPosition) override; // @0x24004280
    void Draw(const UiCanvasDescriptor &canvas) override;      // @0x2400661c

    NautilusHw     *hw_;                     // +0x08
    NautilusEngine *engine_;                 // +0x0c
    uint32_t        purge_time_;             // +0x10 time of the last edge of the purge gate
    size_t          last_divisions_;         // +0x14 Draw: to detect changes of sensors
    uint32_t        divisions_time_;         // +0x18 time of the last change of sensors
    float           last_time_mult_;         // +0x1c Draw: to detect changes of resolution
    uint32_t        time_mult_time_;         // +0x20 time of the last change of resolution
    uint32_t        sensors_moved_time_;     // +0x24 time of the last movement of pot 0
    uint32_t        resolution_moved_time_;  // +0x28 time of the last movement of pot 2
    bool            usb_busy_;               // +0x2c written by TimerCallback
    uint8_t         pad_2d[0x3];             // +0x2d TODO(recon)
    uint32_t        reset_time_;             // +0x30 time of Shift + Purge
    bool            saved_f8_changed_;       // +0x34 shift + pot 8
    uint8_t         free_time_;              // +0x35 shift + Delay Mode: "Freerunning Delay" (Settings)
    uint8_t         pad_36[0x2];
    Color           colors_[kNumColors];     // +0x38
};

// Calibration page: opens if shift is held at power-up (see HeldAtBoot). Vtable 0x2402dd5c.
class CalPage : public UiPage
{
  public:
    bool OnButton(uint16_t buttonID,
                  uint8_t  numberOfPresses,
                  bool     isRetriggering) override; // @0x24004240
    // @0x2400419c: unlike UiPage (true), it lets the event through
    bool OnPotMoved(uint16_t potID, float newPosition) override { return false; }
    void Draw(const UiCanvasDescriptor &canvas) override; // @0x24005044

    NautilusHw     *hw_;     // +0x08
    NautilusEngine *engine_; // +0x0c
};

// Reads the pots for PotMonitor
struct PotBackend
{
    float GetPotValue(uint16_t potId) { return hw->knobs[potId].Value(); }

    NautilusHw *hw;
};

struct Interface
{
    // Inlined in main (@0x2400eb4c..0x2400ec40)
    __attribute__((always_inline)) inline void Init(NautilusHw *hw, NautilusEngine *engine, bool test_page);

    MainPage                          main_page; // +0x000
    CalPage                           cal_page;  // +0x080
    UiEventQueue                      queue;     // +0x090
    UI                                ui;        // +0x7a8
    NautilusHw                       *hw;        // +0x964
    NautilusEngine                   *engine;    // +0x968
    PotBackend                        pot_backend; // +0x96c
    PotMonitor<PotBackend, KNOB_LAST> pots;      // +0x970
    // Releasing these buttons does not generate an event (after the 3 s combo). Only 5 are used.
    bool suppress_release[8];                    // +0x9c4

    void ProcessControls(); // @0x2400d448
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(MainPage, hw_) == 0x08, "layout");
static_assert(offsetof(MainPage, sensors_moved_time_) == 0x24, "layout");
static_assert(offsetof(MainPage, usb_busy_) == 0x2c, "layout");
static_assert(offsetof(MainPage, free_time_) == 0x35, "layout");
static_assert(offsetof(MainPage, colors_) == 0x38, "layout");
static_assert(sizeof(MainPage) == 0x80, "layout");
static_assert(sizeof(CalPage) == 0x10, "layout");
static_assert(offsetof(Interface, queue) == 0x90, "layout");
static_assert(offsetof(Interface, ui) == 0x7a8, "layout");
static_assert(offsetof(Interface, hw) == 0x964, "layout");
static_assert(offsetof(Interface, pots) == 0x970, "layout");
static_assert(sizeof(Interface) == 0x9cc, "layout");
#pragma GCC diagnostic pop

// main.cpp
extern NautilusHw hw;
extern float      meter_l, meter_r;
extern float      out_env_l, out_env_r;

// LED canvas: the UI clears and flushes the PCA9685 buffer
// @0x240047b8 (clearFunction)
static void ClearLeds(const UiCanvasDescriptor &canvas);
// @0x24004568 (flushFunction)
static void FlushLeds(const UiCanvasDescriptor &canvas);

// @0x24004280
bool MainPage::OnPotMoved(uint16_t potID, float newPosition)
{
    const bool shift = hw_->buttons[BUTTON_SHIFT].Pressed();
    int        target;
    switch(potID)
    {
        case PARAM_SENSORS:
            hw_->params[PARAM_SENSORS].knob = newPosition;
            sensors_moved_time_             = System::GetNow();
            break;
        case PARAM_DISPERSAL: hw_->params[PARAM_DISPERSAL].knob = newPosition; break;
        case PARAM_RESOLUTION:
            if(shift)
            {
                engine_->reverb_amount = fclamp(newPosition, 0.f, 1.f);
            }
            else
            {
                hw_->params[PARAM_RESOLUTION].knob = newPosition;
                resolution_moved_time_             = System::GetNow();
            }
            break;
        case PARAM_REVERSAL: hw_->params[PARAM_REVERSAL].knob = newPosition; break;
        case PARAM_DEPTH: hw_->params[PARAM_DEPTH].knob = newPosition; break;
        case PARAM_FEEDBACK: hw_->params[PARAM_FEEDBACK].knob = newPosition; break;
        case PARAM_MIX: hw_->params[PARAM_MIX].knob = newPosition; break;
        case PARAM_CHROMA:
            if(shift)
                // "LED Dimmer". The manual says they turn off at the left; the minimum is 15 %
                hw_->brightness = fclamp(newPosition, 0.15f, 1.f);
            else
                hw_->params[PARAM_CHROMA].knob = newPosition;
            break;
        case KNOB_ATTENUVERTER_1:
            if(shift)
            {
                // Detent at 0.95..1.05 → exactly 1.0. GCC moves the comparison onto newPosition
                // (0.18666667 and 0.21333332 in the binary) and computes the formula first.
                engine_->input_level = newPosition * 3.75f + 0.25f;
                if(engine_->input_level > 0.95f && engine_->input_level < 1.05f)
                    engine_->input_level = 1.f;
                saved_f8_changed_ = true;
            }
            else
            {
                target = hw_->atten_target[0];
                if(target >= 0)
                    hw_->params[target].atten = newPosition * 2.f - 1.f;
            }
            break;
        case KNOB_ATTENUVERTER_2:
            target = hw_->atten_target[1];
            if(target >= 0)
                hw_->params[target].atten = newPosition * 2.f - 1.f;
            break;
    }
    return true;
}

// @0x240043a4 — numberOfPresses: 1 on press, 0 on release (ProcessControls)
bool MainPage::OnButton(uint16_t buttonID,
                        uint8_t  numberOfPresses,
                        bool     isRetriggering)
{
    const bool shift = hw_->buttons[BUTTON_SHIFT].Pressed();
    if(numberOfPresses == 1)
    {
        switch(buttonID)
        {
            case BUTTON_FREEZE:
            {
                NautilusEngine *e = engine_;
                if(!e->frozen && !e->freeze_request)
                {
                    e->freeze_request = true;
                }
                else
                {
                    e->frozen         = false;
                    e->freeze_request = false;
                    e->RequestDelayMode(e->delay_mode);
                }
                break;
            }
            case BUTTON_PURGE:
                if(shift)
                {
                    engine_->ResetModes();
                    reset_time_ = System::GetNow();
                }
                else
                {
                    engine_->purge_request = true;
                    purge_time_            = System::GetNow();
                }
                break;
        }
    }
    else
    {
        switch(buttonID)
        {
            case BUTTON_FEEDBACK:
            {
                NautilusEngine *e = engine_;
                if(shift)
                {
                    // Shift + Feedback: stereo or mono input
                    if(e->input_mode == 0)
                        e->input_mode = 1;
                    else
                        e->input_mode = 0;
                }
                else if(e->feedback_mode == FEEDBACK_NORMAL)
                    e->RequestFeedbackMode(FEEDBACK_PING_PONG);
                else if(e->feedback_mode == FEEDBACK_PING_PONG)
                    e->RequestFeedbackMode(FEEDBACK_CASCADE);
                else if(e->feedback_mode == FEEDBACK_CASCADE)
                    e->RequestFeedbackMode(FEEDBACK_ADRIFT);
                else
                    e->RequestFeedbackMode(FEEDBACK_NORMAL);
                break;
            }
            case BUTTON_PURGE:
            {
                NautilusEngine *e = engine_;
                e->purge_released = true;
                if(shift)
                {
                    // Shift + Purge on release: also resets the saved settings.
                    // It is the manual 1.2.1 "Reset Factory Settings: Tap+Purge" (the 1.2.0 one
                    // said Tap+Freeze). The manual mentions the LED brightness, but the
                    // binary zeroes the reverb and leaves the brightness alone.
                    e->input_mode  = 0;
                    free_time_     = 0;
                    e->input_level = 1.f;
                    e->SetReverbAmount(0.f);
                    saved_f8_changed_ = true;
                }
                break;
            }
            case BUTTON_DELAY_MODE:
                if(!shift)
                {
                    NautilusEngine *e    = engine_;
                    int             next = e->delay_mode;
                    next++;
                    if(next >= 4)
                        next = DELAY_FADE;
                    e->RequestDelayMode(next, e->clear_on_mode_change);
                }
                else
                {
                    if(free_time_)
                        free_time_ = 0;
                    else
                        free_time_ = 1;
                }
                break;
        }
    }
    return true;
}

// @0x2400661c — main page LEDs
void MainPage::Draw(const UiCanvasDescriptor &canvas)
{
    if(hw_->buttons[BUTTON_SHIFT].TimeHeldMs() > 150.f)
    {
        // Shift: alternate button functions
        hw_->SetColor(0, Color(0.7f, 1.f, 1.f) * 0.5f);
        hw_->SetColor(3, Color(0.7f, 1.f, 1.f) * ((System::GetNow() & 0x1ff) < 255 ? 0.5f : 0.f));
        hw_->SetColor(2, engine_->input_mode == 0 ? colors_[0] : colors_[1]);
        hw_->SetColor(1, free_time_ == 0 ? colors_[0] : colors_[1]);
    }
    else
    {
        Color white(0.7f, 1.f, 1.f);
        hw_->SetColor(0, engine_->freeze_request || engine_->frozen ? white : colors_[0]);
        hw_->SetColor(2, colors_[engine_->feedback_mode]);
        hw_->SetColor(1, colors_[engine_->delay_mode]);
        // Purge: 120 ms flash, or steady while it lasts with Continuous Purge
        const bool purging = engine_->continuous_purge
                                 ? engine_->purge_request
                                 : System::GetNow() - purge_time_ < 120;
        if(purging)
            hw_->SetColor(3, white);
        else
            hw_->SetRgb(3, 0.f, 0.f, 0.f);
    }

    // Output meters: gradient from the base color to the Chroma mode color according to depth
    meter_l += (out_env_l * out_env_l * 4.f - meter_l) * 0.25f;
    meter_r += (out_env_r * out_env_r * 4.f - meter_r) * 0.25f;
    int   n_l, n_r;
    float frac_l, frac_r;
    if(meter_l >= 0.6f)
    {
        n_l    = 2;
        frac_l = 0.9999001f;
    }
    else
    {
        n_l    = (int)(meter_l * 5.f);
        frac_l = meter_l * 5.f - n_l;
    }
    if(meter_r >= 0.6f)
    {
        n_r    = 2;
        frac_r = 0.9999001f;
    }
    else
    {
        n_r    = (int)(meter_r * 5.f);
        frac_r = meter_r * 5.f - n_r;
    }

    const uint8_t leds_l[4]        = {6, 5, 4, 7};
    const uint8_t leds_r[4]        = {11, 10, 9, 8};
    const int     chroma_color[6] = {0, 1, 3, 2, 4, 5};
    Color         grad[4];
    grad[3] = colors_[chroma_color[engine_->fb_fx[0].chroma[0].mode]];
    grad[0] = Color::Blend(colors_[1], grad[3], hw.params[PARAM_DEPTH].Value() * 0.75f);
    grad[1] = Color::Blend(grad[0], grad[3], 0.33f);
    grad[2] = Color::Blend(grad[0], grad[3], 0.66f);
    for(int i = 0; i < n_l; i++)
        hw_->SetColor(leds_l[i], grad[i]);
    for(int i = 0; i < n_r; i++)
        hw_->SetColor(leds_r[i], grad[i]);
    hw_->SetColor(leds_l[n_l], grad[n_l] * frac_l);
    hw_->SetColor(leds_r[n_r], grad[n_r] * frac_r);

    // Phase of each active voice (bottom to top), in the reading direction
    for(int v = 0; v < kNumVoices; v++)
    {
        if(v < (int)engine_->divisions)
        {
            float a = clock_lfos.voice_out[0][v];
            float b = clock_lfos.voice_out[1][v];
            if(!engine_->reversed[0][v])
                a = 1.f - a;
            if(!engine_->reversed[1][v])
                b = 1.f - b;
            hw_->SetColor(leds_l[3 - v], grad[3 - v] * (a * 0.6f));
            hw_->SetColor(leds_r[3 - v], grad[3 - v] * (b * 0.6f));
        }
    }

    // Moving sensors or resolution marks the change for 125 ms
    const uint32_t now = System::GetNow();
    if(now - sensors_moved_time_ < 125 && last_divisions_ != engine_->divisions)
        divisions_time_ = now;
    last_divisions_ = engine_->divisions;
    if(now - resolution_moved_time_ < 125 && last_time_mult_ != engine_->time_mult)
        time_mult_time_ = now;
    last_time_mult_ = engine_->time_mult;
    if(now - divisions_time_ < 125 || now - time_mult_time_ < 125)
    {
        hw_->SetColor(12, Color(0.7f, 1.f, 1.f));
        hw_->SetColor(6, Color(0.7f, 1.f, 1.f) * 0.8f);
        hw_->SetColor(11, Color(0.7f, 1.f, 1.f) * 0.8f);
    }

    // Shift + attenuverter 1: input level on LEDs 7, 4, 5 and 6
    if(hw_->buttons[BUTTON_SHIFT].Pressed())
    {
        if(saved_f8_changed_)
        {
            if(engine_->input_level != 1.f)
            {
                const uint8_t leds[4] = {7, 4, 5, 6};
                for(int i = 0; i < 4; i++)
                {
                    const int n = (int)engine_->input_level;
                    if(i < n)
                        hw_->SetColor(leds[i], Color(0.7f, 1.f, 1.f));
                    else if(i == n)
                        hw_->SetColor(leds[i], Color(0.7f, 1.f, 1.f) * (engine_->input_level - i));
                    else
                        hw_->SetRgb(leds[i], 0.f, 0.f, 0.f);
                }
            }
            else
            {
                hw_->SetColor(7, colors_[0]);
                hw_->SetColor(4, colors_[0]);
                hw_->SetColor(5, colors_[0]);
                hw_->SetColor(6, colors_[0]);
            }
        }
    }
    else
    {
        saved_f8_changed_ = false;
    }

    // USB busy: LEDs 7 and 8 blink
    if(usb_busy_)
    {
        const bool blink = System::GetNow() & 64;
        hw_->SetColor(7, blink ? Color() : Color(0.7f, 1.f, 1.f) * 0.6f);
        hw_->SetColor(8, blink ? Color() : Color(0.7f, 1.f, 1.f) * 0.6f);
    }

    // Shift + Purge: the button LEDs alternate for ~0.5 s
    if(now - reset_time_ < 511)
    {
        const bool phase = (uint8_t)System::GetNow() < 127;
        hw_->SetColor(0, phase ? Color(0.7f, 1.f, 1.f) * 0.8f : Color());
        hw_->SetColor(1, phase ? Color() : Color(0.7f, 1.f, 1.f) * 0.8f);
        hw_->SetColor(2, phase ? Color(0.7f, 1.f, 1.f) * 0.8f : Color());
        hw_->SetColor(3, phase ? Color() : Color(0.7f, 1.f, 1.f) * 0.8f);
    }

    // Test ping: a quarter of the tempo pulse per LED pair
    if(engine_->ping)
    {
        const float p = clock_lfos.tempo_phase;
        hw_->SetRgb(7, 0.f, p < 0.25f ? 1.f : 0.f, 0.f);
        hw_->SetRgb(8, 0.f, p < 0.25f ? 1.f : 0.f, 0.f);
        hw_->SetRgb(4, 0.f, p > 0.25f && p < 0.5f ? 1.f : 0.f, 0.f);
        hw_->SetRgb(9, 0.f, p > 0.25f && p < 0.5f ? 1.f : 0.f, 0.f);
        hw_->SetRgb(5, 0.f, p > 0.5f && p < 0.75f ? 1.f : 0.f, 0.f);
        hw_->SetRgb(10, 0.f, p > 0.5f && p < 0.75f ? 1.f : 0.f, 0.f);
        hw_->SetRgb(6, 0.f, p > 0.75f ? 1.f : 0.f, 0.f);
        hw_->SetRgb(11, 0.f, p > 0.75f ? 1.f : 0.f, 0.f);
    }
}

// @0x24004240 — Purge (on press or on release) leaves calibration
bool CalPage::OnButton(uint16_t buttonID,
                       uint8_t  numberOfPresses,
                       bool     isRetriggering)
{
    if(buttonID == BUTTON_PURGE)
    {
        engine_->ResetModes();
        Close();
    }
    return false;
}

// @0x24005044 — input test: on each LED, green = CV and blue = pot; the button
// LEDs show the button (blue) and its gate (green). The clock gate lights the red
// of LEDs 7 and 8.
void CalPage::Draw(const UiCanvasDescriptor &canvas)
{
    const float clk = hw_->gates[GATE_CLOCK].State() ? 1.f : 0.f;
    float       knob[KNOB_LAST];
    float       cv[PARAM_LAST];
    for(int i = 0; i < PARAM_LAST; i++)
        cv[i] = fclamp(hw_->cvs[i].Value(), 0.f, 1.f);
    for(int i = 0; i < KNOB_LAST; i++)
        knob[i] = hw_->knobs[i].Value();

    hw_->SetRgb(7, clk, knob[PARAM_RESOLUTION], knob[KNOB_ATTENUVERTER_1]);
    hw_->SetRgb(4, 0.f, cv[PARAM_MIX], knob[PARAM_MIX]);
    hw_->SetRgb(5, 0.f, cv[PARAM_DISPERSAL], knob[PARAM_DISPERSAL]);
    hw_->SetRgb(6, 0.f, cv[PARAM_SENSORS], knob[PARAM_SENSORS]);
    hw_->SetRgb(8, clk, cv[PARAM_RESOLUTION], knob[KNOB_ATTENUVERTER_2]);
    hw_->SetRgb(9, 0.f, cv[PARAM_CHROMA], knob[PARAM_CHROMA]);
    hw_->SetRgb(10, 0.f, cv[PARAM_FEEDBACK], knob[PARAM_FEEDBACK]);
    hw_->SetRgb(11, 0.f, cv[PARAM_DEPTH], knob[PARAM_DEPTH]);
    hw_->SetRgb(12, 0.f, cv[PARAM_REVERSAL], knob[PARAM_REVERSAL]);

    hw_->SetRgb(0,
                0.f,
                hw_->gates[GATE_FREEZE].State() ? 1.f : 0.f,
                hw_->buttons[BUTTON_FREEZE].Pressed() ? 1.f : 0.f);
    hw_->SetRgb(3,
                0.f,
                hw_->gates[GATE_PURGE].State() ? 1.f : 0.f,
                hw_->buttons[BUTTON_PURGE].Pressed() ? 1.f : 0.f);
    hw_->SetRgb(1, 0.f, 0.f, hw_->buttons[BUTTON_DELAY_MODE].Pressed() ? 1.f : 0.f);
    hw_->SetRgb(2, 0.f, 0.f, hw_->buttons[BUTTON_FEEDBACK].Pressed() ? 1.f : 0.f);
}

// Inlined in main (@0x2400eb4c..0x2400ec40)
void Interface::Init(NautilusHw *hw_, NautilusEngine *engine_, bool test_page)
{
    hw = hw_;
    for(auto &s : suppress_release)
        s = false;
    engine = engine_;

    UiCanvasDescriptor leds;
    leds.id_                = 0;
    leds.handle_            = nullptr;
    leds.updateRateMs_      = 16;
    leds.screenSaverTimeOut = 0;
    leds.screenSaverOn      = false;
    leds.clearFunction_     = ClearLeds;
    leds.flushFunction_     = FlushLeds;
    UI::SpecialControlIds ids;
    ui.Init(queue, ids, {leds}, 0);

    pot_backend.hw = hw;
    pots.Init(queue, pot_backend, 250, 1.f / 512.f, 1.f / 256.f);

    // Default colors (can be changed with "Color N" in options.json). Written with the same
    // conversion as options.json, so "#rrggbb" there gives exactly the same color.
    // The original used (0, 0, 1), (0, 1, 0), (0.92, 0.764, 0.2), (0.61, 0.2, 0.92),
    // (0, 1, 1) and (1, 0, 0).
    constexpr float k = 1.f / 255.f;
    main_page.colors_[0] = Color(0x20 * k, 0xc3 * k, 0xfe * k); // #20c3fe
    main_page.colors_[1] = Color(0x77 * k, 0xf9 * k, 0x86 * k); // #77f986
    main_page.colors_[2] = Color(0xff * k, 0xe4 * k, 0x5c * k); // #ffe45c
    main_page.colors_[3] = Color(0xab * k, 0x85 * k, 0xff * k); // #ab85ff
    main_page.colors_[4] = Color(0xf0 * k, 0xd2 * k, 0xd1 * k); // #f0d2d1
    main_page.colors_[5] = Color(0xf6 * k, 0x10 * k, 0x67 * k); // #f61067
    main_page.usb_busy_  = false;
    main_page.free_time_ = 0;
    main_page.hw_        = hw;
    cal_page.hw_         = hw;
    main_page.engine_    = engine;
    cal_page.engine_     = engine;

    ui.OpenPage(main_page);
    // Shift held at power-up (checked by main, see HeldAtBoot)
    if(test_page)
        ui.OpenPage(cal_page);
}

// @0x2400d448 — called by AudioCallback on every block
void Interface::ProcessControls()
{
    // Shift + buttons 1 and 2 for 3 s: toggles the test ping
    if(hw->buttons[BUTTON_SHIFT].TimeHeldMs() > 3000.f
       && hw->buttons[BUTTON_DELAY_MODE].TimeHeldMs() > 3000.f
       && hw->buttons[BUTTON_FEEDBACK].TimeHeldMs() > 3000.f)
    {
        if(!suppress_release[BUTTON_DELAY_MODE] && !suppress_release[BUTTON_FEEDBACK])
            engine->ping = !engine->ping;
        suppress_release[BUTTON_FEEDBACK] = true;
        suppress_release[BUTTON_DELAY_MODE] = true;
    }

    // Shift generates no events: it is queried directly
    for(uint16_t i = 0; i < BUTTON_LAST; i++)
    {
        const Switch &b = hw->buttons[i];
        if(b.FallingEdge() && i != BUTTON_SHIFT)
        {
            if(suppress_release[i])
            {
                suppress_release[i] = false;
                continue;
            }
            queue.AddButtonReleased(i);
        }
        if(b.RisingEdge() && i != BUTTON_SHIFT)
            queue.AddButtonPressed(i, 1);
    }

    pots.Process();
}

} // namespace nautilus
