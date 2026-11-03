// Nautilus (Qu-Bit) v1.2.0 — module hardware
// Reconstructed from nautilus.bin: ctor @0x24005df0, Init @0x24005edc,
// SetRgb @0x2400489c, SetColor @0x24006494. Global object at 0x24078450 (0x630 bytes).
#pragma once

#include <stddef.h>
#include "daisy_seed.h"
#include "dev/leddriver.h"

namespace nautilus
{
using namespace daisy;

// Parameter indices. They match the order of the values of
// "Attenuverter 1/2 Target" in options.json (@0x2400e120).
enum Param
{
    PARAM_SENSORS,
    PARAM_DISPERSAL,
    PARAM_RESOLUTION,
    PARAM_REVERSAL,
    PARAM_DEPTH,
    PARAM_FEEDBACK,
    PARAM_MIX,
    PARAM_CHROMA,
    PARAM_LAST,
};

// Pots: 0..7 = parameters, 8..9 = attenuverters (MainPage::OnPotMoved @0x24004280)
enum Knob
{
    KNOB_ATTENUVERTER_1 = PARAM_LAST,
    KNOB_ATTENUVERTER_2,
    KNOB_LAST,
};

// Buttons (Debounce in a loop over hw+0x40..0x90 in AudioCallback @0x2400d83c)
enum Button
{
    BUTTON_FREEZE,     // D2  (PC10) +0x40
    BUTTON_FEEDBACK,   // D0  (PB12) +0x50
    BUTTON_DELAY_MODE, // D4  (PC8)  +0x60
    BUTTON_PURGE,      // D3  (PC9)  +0x70
    BUTTON_SHIFT,      // D1  (PC11) +0x80, shift and tap tempo
    BUTTON_LAST,
};

enum Gate
{
    GATE_FREEZE, // D6 (PC12) +0x90 toggles the freeze
    GATE_PURGE,  // D5 (PD2)  +0xa8 purge while high
    GATE_CLOCK,  // D7 (PG10) +0xc0 external clock (same as tap with shift)
    GATE_LAST,
};

static constexpr int kNumRgbLeds = 14; // 42 channels in the map (+0x3c0)

// Value of a parameter before mixing: pot, calibrated CV and attenuverter amount.
struct ParamInput
{
    // Pot + CV·attenuverter, clamped to [0, 1] (inlined in AudioCallback)
    float Value() const { return fmaxf(fminf(knob + cv * atten, 1.f), 0.f); }

    float knob  = 0.f;
    float cv    = 0.f;
    float atten = 1.f;
};

// Calibration of the CV inputs (QSPI 0x1000): resting offset of each CV
struct Calibration
{
    float offset[PARAM_LAST] = {};

    bool operator!=(const Calibration &a) const
    {
        for(int i = 0; i < PARAM_LAST; i++)
            if(offset[i] != a.offset[i])
                return true;
        return false;
    }
};

class NautilusHw
{
  public:
    void Init();

    // @0x24008bf8
    void LoadCalibration()
    {
        PersistentStorage<Calibration> storage(seed.qspi);
        Calibration                    defaults;
        storage.Init(defaults, 0x1000);
        for(int i = 0; i < PARAM_LAST; i++)
            cv_offset[i] = storage.GetSettings().offset[i];
    }

    // Inlined in main (@0x24010720): takes the current reading of each CV as the offset
    // (button 0 or 3 held at power-up, with nothing connected)
    void CalibrateCvs()
    {
        PersistentStorage<Calibration> storage(seed.qspi);
        Calibration                    defaults;
        storage.Init(defaults, 0x1000);
        Calibration &cal = storage.GetSettings();
        for(int i = 0; i < PARAM_LAST; i++)
        {
            cv_offset[i]  = cvs[i].Value();
            cal.offset[i] = cv_offset[i];
        }
        storage.Save();
    }

    // @0x2400489c (in the binary it was specialized with r = 0). The three indices are read
    // before writing: the original loads them before the first strh to the PCA9685 buffer
    // (read inline, int8_t could alias the buffer and GCC does not hoist them).
    void SetRgb(int led, float r, float g, float b)
    {
        const int ir = led_map[led][0], ig = led_map[led][1], ib = led_map[led][2];
        leds.SetLed(ir, r);
        leds.SetLed(ig, g);
        leds.SetLed(ib, b);
    }

    // @0x24006494: applies the global brightness before writing
    void SetColor(int led, Color c)
    {
        Color scaled;
        scaled.Init(brightness * c.Red(),
                    brightness * c.Green(),
                    brightness * c.Blue());
        const int ir = led_map[led][0], ig = led_map[led][1], ib = led_map[led][2];
        leds.SetLed(ir, scaled.Red());
        leds.SetLed(ig, scaled.Green());
        leds.SetLed(ib, scaled.Blue());
    }

    DaisySeed     seed;                       // +0x000
    Switch        buttons[BUTTON_LAST];       // +0x040
    GateIn        gates[GATE_LAST];           // +0x090
    AnalogControl knobs[KNOB_LAST];           // +0x0d8
    AnalogControl cvs[PARAM_LAST];            // +0x218
    uint8_t       reserved_318[0x40];         // +0x318 TODO(recon): unused in the binary
    int           atten_target[2];            // +0x358 (Init: 1 and 5)
    ParamInput    params[PARAM_LAST];         // +0x360
    int8_t        led_map[kNumRgbLeds][3] = { // +0x3c0 (ldrsb)
        {0, 1, 2},
        {3, 4, 5},
        {6, 7, 8},
        {9, 10, 11},
        {12, 13, 14},
        {15, 16, 17},
        {18, 19, 20},
        {21, 22, 23},
        {24, 25, 26},
        {27, 28, 29},
        {30, 31, 32},
        {33, 34, 35},
        {36, 37, 38},
        {39, 40, 41},
    };
    LedDriverPca9685<3, true> leds;           // +0x3ec
    TimerHandle               tim;            // +0x604
    float                     dac_samplerate; // +0x608
    float                     brightness;     // +0x60c
    float cv_offset[PARAM_LAST] = {};         // +0x610, calibration (QSPI 0x1000)
};

// I2C1 at 1 MHz on D11/D12. Constant in .rodata (0x2402dddc): Init receives its address.
static const I2CHandle::Config kLedI2cConfig = {
    I2CHandle::Config::Peripheral::I2C_1,
    {seed::D11, seed::D12},
    I2CHandle::Config::Speed::I2C_1MHZ,
    I2CHandle::Config::Mode::I2C_MASTER,
};

// DMA buffers of the PCA9685s (0x30000000 and 0x300000c4)
LedDriverPca9685<3, true>::DmaBuffer DMA_BUFFER_MEM_SECTION led_dma_buffer_a,
    led_dma_buffer_b;

// @0x24005edc
void NautilusHw::Init()
{
    seed.Init(true);
    seed.SetAudioBlockSize(128);

    buttons[BUTTON_SHIFT].Init(seed::D1);
    buttons[BUTTON_FEEDBACK].Init(seed::D0);
    buttons[BUTTON_DELAY_MODE].Init(seed::D4);
    buttons[BUTTON_PURGE].Init(seed::D3);
    buttons[BUTTON_FREEZE].Init(seed::D2);

    // GateIn::Init(dsy_gpio_pin*, invert = true): the pointer overload is used
    dsy_gpio_pin gate_pin;
    gate_pin = seed::D7;
    gates[GATE_CLOCK].Init(&gate_pin);
    gate_pin = seed::D6;
    gates[GATE_FREEZE].Init(&gate_pin);
    gate_pin = seed::D5;
    gates[GATE_PURGE].Init(&gate_pin);

    // Channel 0: 8-input mux (pots); channels 1..10: direct inputs
    AdcChannelConfig adc_cfg[11];
    adc_cfg[0].InitMux(seed::D15,
                       8,
                       seed::D8,
                       seed::D9,
                       seed::D10,
                       AdcChannelConfig::SPEED_64CYCLES_5);
    adc_cfg[1].InitSingle(seed::D16);
    adc_cfg[2].InitSingle(seed::D17);
    adc_cfg[3].InitSingle(seed::D18);
    adc_cfg[4].InitSingle(seed::D19);
    adc_cfg[5].InitSingle(seed::D20);
    adc_cfg[6].InitSingle(seed::D21);
    adc_cfg[7].InitSingle(seed::D22);
    adc_cfg[8].InitSingle(seed::D24);
    adc_cfg[9].InitSingle(seed::D25);
    adc_cfg[10].InitSingle(seed::D28);
    seed.adc.Init(adc_cfg, 11);

    knobs[PARAM_SENSORS].Init(seed.adc.GetMuxPtr(0, 0), seed.AudioCallbackRate());
    knobs[PARAM_DISPERSAL].Init(seed.adc.GetMuxPtr(0, 1), seed.AudioCallbackRate());
    knobs[PARAM_RESOLUTION].Init(seed.adc.GetMuxPtr(0, 2), seed.AudioCallbackRate());
    knobs[PARAM_REVERSAL].Init(seed.adc.GetMuxPtr(0, 3), seed.AudioCallbackRate());
    knobs[PARAM_DEPTH].Init(seed.adc.GetMuxPtr(0, 4), seed.AudioCallbackRate());
    knobs[PARAM_FEEDBACK].Init(seed.adc.GetMuxPtr(0, 5), seed.AudioCallbackRate());
    knobs[KNOB_ATTENUVERTER_1].Init(seed.adc.GetMuxPtr(0, 6), seed.AudioCallbackRate());
    knobs[PARAM_MIX].Init(seed.adc.GetMuxPtr(0, 7), seed.AudioCallbackRate());
    knobs[PARAM_CHROMA].Init(seed.adc.GetPtr(1), seed.AudioCallbackRate());
    knobs[KNOB_ATTENUVERTER_2].Init(seed.adc.GetPtr(2), seed.AudioCallbackRate());

    cvs[PARAM_DISPERSAL].InitBipolarCv(seed.adc.GetPtr(3), seed.AudioCallbackRate());
    cvs[PARAM_MIX].InitBipolarCv(seed.adc.GetPtr(4), seed.AudioCallbackRate());
    cvs[PARAM_SENSORS].InitBipolarCv(seed.adc.GetPtr(5), seed.AudioCallbackRate());
    cvs[PARAM_REVERSAL].InitBipolarCv(seed.adc.GetPtr(6), seed.AudioCallbackRate());
    cvs[PARAM_CHROMA].InitBipolarCv(seed.adc.GetPtr(7), seed.AudioCallbackRate());
    cvs[PARAM_RESOLUTION].InitBipolarCv(seed.adc.GetPtr(8), seed.AudioCallbackRate());
    cvs[PARAM_FEEDBACK].InitBipolarCv(seed.adc.GetPtr(9), seed.AudioCallbackRate());
    cvs[PARAM_DEPTH].InitBipolarCv(seed.adc.GetPtr(10), seed.AudioCallbackRate());

    // LEDs: three PCA9685s at addresses 0, 1 and 2
    I2CHandle i2c;
    i2c.Init(kLedI2cConfig);
    const uint8_t addr[3] = {0, 1, 2};
    leds.Init(i2c, addr, led_dma_buffer_a, led_dma_buffer_b);

    // PCA9685 OE (PB11) low: outputs enabled
    GPIO oe;
    oe.Init(Pin(PORTB, 11), GPIO::Mode::OUTPUT);
    oe.Write(false);

    // "Sonar" output: DAC at a quarter of the audio rate
    const float sr = seed.AudioSampleRate();
    DacHandle::Config dac_cfg;
    dac_cfg.target_samplerate = sr * 0.25f;
    dac_cfg.chn               = DacHandle::Channel::ONE;
    dac_cfg.mode              = DacHandle::Mode::DMA;
    dac_cfg.bitdepth          = DacHandle::BitDepth::BITS_12;
    dac_cfg.buff_state        = DacHandle::BufferState::ENABLED;
    dac_samplerate            = sr * 0.25f;
    seed.dac.Init(dac_cfg);

    seed.adc.Start();

    atten_target[0] = PARAM_DISPERSAL;
    atten_target[1] = PARAM_FEEDBACK;
    brightness      = 1.f;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(NautilusHw, buttons) == 0x40, "layout");
static_assert(offsetof(NautilusHw, gates) == 0x90, "layout");
static_assert(offsetof(NautilusHw, knobs) == 0xd8, "layout");
static_assert(offsetof(NautilusHw, cvs) == 0x218, "layout");
static_assert(offsetof(NautilusHw, atten_target) == 0x358, "layout");
static_assert(offsetof(NautilusHw, params) == 0x360, "layout");
static_assert(offsetof(NautilusHw, led_map) == 0x3c0, "layout");
static_assert(offsetof(NautilusHw, leds) == 0x3ec, "layout");
static_assert(offsetof(NautilusHw, tim) == 0x604, "layout");
static_assert(offsetof(NautilusHw, brightness) == 0x60c, "layout");
static_assert(offsetof(NautilusHw, cv_offset) == 0x610, "layout");
static_assert(sizeof(NautilusHw) == 0x630, "layout");
#pragma GCC diagnostic pop

} // namespace nautilus
