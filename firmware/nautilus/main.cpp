// Nautilus (Qu-Bit) v1.2.0 — reconstruction from Nautilus_v1_2_0-wht3.bin
// Every non-trivial block carries "// @0x2400xxxx" with its source address.
// The globals follow the original's definition order (StaticInit @0x24010d00).

// Order of the globals in .bss. With -fdata-sections and section anchors (on by
// default on ARM), GCC emits each global in its own .bss.<name> section and sorts them by that
// name (strcmp). The original follows neither the definition order (nor StaticInit's):
// it comes from their names, which are unknown. It is fixed here with the position of each
// one starting at 0x2403de54.
#define BSS_ORDER(n) __attribute__((section(".bss.nautilus." #n)))

#include "daisy_seed.h"
#include "daisysp.h"
#include "nautilus_hw.h"
#include "nautilus_engine.h"
#include "clock_lfos.h"
#include "ui.h"
#include "usb_drive.h"

using namespace daisy;
using namespace daisysp;

namespace nautilus
{
const char *kModuleName      = "Qu-Bit Nautilus"; // .data 0x2403d804
const char *kFirmwareVersion = "v1.2.0";          // .data 0x2403d808

float          BSS_ORDER(07) meter_l;  // 0x2405e4b0 meter level (MainPage::Draw)
float          BSS_ORDER(08) meter_r;  // 0x2405e4b4
float          BSS_ORDER(09) peak_l;   // 0x2405e4b8 left output peak follower
UsbDrive       BSS_ORDER(10) usb;      // 0x2405e4bc
float          BSS_ORDER(11) peak_r;   // 0x2405e4e4
FatFSInterface BSS_ORDER(12) fsi;      // 0x2405e4e8
USBHostHandle  BSS_ORDER(13) usb_host; // 0x2405e968

float BSS_ORDER(04) resolution_hyst; // 0x2403f084 hysteresis of the resolution index (AudioCallback)

// Output peaks: written by AudioCallback and read by MainPage::Draw. They are two globals:
// the original loads each one's address separately (from an array it would load only one).
float BSS_ORDER(18) out_env_l; // 0x2405f990
float BSS_ORDER(19) out_env_r; // 0x2405f994

ClockLfos BSS_ORDER(20) clock_lfos; // 0x2405f998

// SDRAM: Buffer Locked Freeze loops (10 s) and, per channel, 4 voices × 2 × 960000 samples
// (GCC places them in reverse definition order)
float DSY_SDRAM_BSS voice_buffer[kNumGroups][kNumVoices * 2 * 960000]; // 0xc03a9800
float DSY_SDRAM_BSS freeze_buffer[2][480000];                       // 0xc0000000

NautilusHw     BSS_ORDER(22) hw;        // 0x24078450
Interface      BSS_ORDER(27) interface; // 0x24079af0
NautilusEngine BSS_ORDER(21) engine;    // 0x2405faa8

// Tap tempo / external clock (0x24079ac0). Median of the last 3 intervals.
struct TempoState
{
    uint32_t last_us = 0;                          // +0x00
    uint32_t interval_us;                          // +0x04
    float    bpm_hist[3] = {120.f, 120.f, 120.f};  // +0x08
    float    bpm_last;                             // +0x14
    float    bpm = 120.f;                          // +0x18 median
    int32_t  samples_last;                         // +0x1c
    int32_t  samples = 24000;                      // +0x20 median (at 48 kHz)
    int32_t  samples_hist[3] = {24000, 24000, 24000}; // +0x24
};
TempoState BSS_ORDER(26) tempo;

// Persistent settings (QSPI 0x400). Saved every 5 s if they changed.
struct Settings
{
    uint8_t delay_mode    = 0;   // engine.delay_mode
    int     feedback_mode = 0;   // engine.feedback_mode
    float   input_level   = 1.f; // engine.input_level
    uint8_t free_time     = 0;   // interface.main_page.free_time_
    int     input_mode    = 0;   // engine.input_mode
    float   reverb_amount = 0.f; // engine.reverb_amount

    bool operator!=(const Settings &a) const
    {
        return delay_mode != a.delay_mode || feedback_mode != a.feedback_mode
               || input_level != a.input_level || free_time != a.free_time
               || input_mode != a.input_mode || reverb_amount != a.reverb_amount;
    }
};
PersistentStorage<Settings> BSS_ORDER(25) settings_storage(hw.seed.qspi); // 0x24079a84

} // namespace nautilus

using namespace nautilus;

#include "options.h"

namespace nautilus
{
// @0x240047b8
static void ClearLeds(const UiCanvasDescriptor &canvas)
{
    hw.leds.SetAllTo((uint8_t)0);
}

// @0x24004568
static void FlushLeds(const UiCanvasDescriptor &canvas)
{
    hw.leds.SwapBuffersAndTransmit();
}
} // namespace nautilus

// USB host callbacks: they only record the event; main processes it
static void UsbConnect(void *data)
{
    usb.SetEvent(UsbDrive::EVENT_CONNECT);
}
static void UsbDisconnect(void *data)
{
    usb.SetEvent(UsbDrive::EVENT_DISCONNECT);
}
static void UsbClassActive(void *data)
{
    usb.SetEvent(UsbDrive::EVENT_CLASS_ACTIVE);
}
static void UsbError(void *data) {}

// @0x24004200
static void UnmountUsb()
{
    f_mount(0, "", 0);
}

// @0x24004770 — 120 Hz timer
static void TimerCallback(void *data)
{
    interface.ui.Process();
    interface.main_page.usb_busy_ = usb.IsBusy();
}

// Tap tempo: shift or the clock gate. Filters out variations below 2.5 ms.
static void Tap()
{
    const uint32_t now = System::GetUs();
    const uint32_t us  = (now - tempo.last_us) / (System::GetTickFreq() / 1000000);
    const int32_t  d   = tempo.interval_us - us;
    if(d < -2500 || d > 2500)
        tempo.interval_us = us;

    const float bpm   = 6e7f / (float)tempo.interval_us;
    tempo.bpm_hist[0] = tempo.bpm_hist[1];
    tempo.bpm_hist[1] = tempo.bpm_hist[2];
    tempo.bpm_hist[2] = bpm;
    tempo.bpm_last    = bpm;
    const float a = tempo.bpm_hist[0], b = tempo.bpm_hist[1], c = bpm;
    tempo.bpm     = a > b ? (c > b ? fminf(c, a) : b) : (c > a ? fminf(b, c) : a);

    const int32_t s       = (int32_t)((float)tempo.interval_us * 0.048f);
    tempo.samples_hist[0] = tempo.samples_hist[1];
    tempo.samples_hist[1] = tempo.samples_hist[2];
    tempo.samples_hist[2] = s;
    tempo.samples_last    = s;
    const int32_t sa = tempo.samples_hist[0], sb = tempo.samples_hist[1];
    tempo.samples    = sa > sb ? (s > sb ? (s < sa ? s : sa) : sb)
                               : (s > sa ? (sb < s ? sb : s) : sa);
    tempo.last_us    = now;

    // Except in the dotted resolutions, resets the phases if they are not at an edge
    if(engine.time_mult != 0.75f && engine.time_mult != 1.5f
       && clock_lfos.tempo_phase < 0.95f && clock_lfos.tempo_phase > 0.05f)
    {
        clock_lfos.clock[2].SetPhase(0.f);
        clock_lfos.clock[0].SetPhase(0.f);
        clock_lfos.clock[1].SetPhase(0.f);
    }
    clock_lfos.clock[2].SetFreq(tempo.bpm * (1.f / 60.f));
}

// Index with ±0.3 hysteresis (resolution)
static inline float HysteresisIndex(float x, float prev)
{
    return (float)(int)(x + 0.5f + (x - prev > 0.f ? -0.3f : 0.3f));
}

// @0x2400d83c
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    for(auto &k : hw.knobs)
        k.Process();
    for(auto &c : hw.cvs)
        c.Process();
    for(auto &b : hw.buttons)
        b.Debounce();
    for(int i = 0; i < PARAM_LAST; i++)
        hw.params[i].cv = hw.cvs[i].Value() - hw.cv_offset[i];

    interface.ProcessControls();

    if(hw.gates[GATE_CLOCK].Trig() || hw.buttons[BUTTON_SHIFT].RisingEdge())
        Tap();

    // LED phase frequency: one cycle per repetition of each tap
    for(int v = 0; v < kNumVoices; v++)
    {
        const PitchTap &a = engine.taps[0].tap[v];
        const PitchTap &b = engine.taps[1].tap[v];
        clock_lfos.voice[0][v].SetFreq(48000.f / a.delay[a.active]);
        clock_lfos.voice[1][v].SetFreq(48000.f / b.delay[b.active]);
    }

    if(hw.gates[GATE_FREEZE].Trig())
    {
        if(!engine.frozen && !engine.freeze_request)
        {
            engine.freeze_request = true;
        }
        else
        {
            engine.frozen         = false;
            engine.freeze_request = false;
            engine.RequestDelayMode(engine.delay_mode);
        }
    }

    // Purge: a rising edge requests it; on falling, Continuous Purge stops repeating
    static bool BSS_ORDER(24) purge_gate; // 0x24079a80
    const bool  purge = hw.gates[GATE_PURGE].State();
    if(purge)
    {
        if(!purge_gate)
        {
            engine.purge_request             = true;
            interface.main_page.purge_time_ = System::GetNow();
        }
    }
    else if(purge_gate)
    {
        engine.purge_released = true;
    }
    purge_gate = purge;

    float mix = hw.params[PARAM_MIX].Value();
    if(mix < 0.04f)
        mix = 0.f;
    else if(mix > 0.96f)
        mix = 1.f;

    int divisions = (int)(hw.params[PARAM_SENSORS].Value() * 4.f + 1.f);
    if(divisions > 4)
        divisions = 4;
    if(divisions < 1)
        divisions = 1;
    engine.divisions      = divisions;
    engine.mix_param      = mix;
    engine.feedback_param = fclamp(hw.params[PARAM_FEEDBACK].Value() * 1.08f, 0.f, 1.f);
    engine.reversal       = fclamp(hw.params[PARAM_REVERSAL].Value() * 1.1f, 0.f, 1.f);
    engine.depth          = hw.params[PARAM_DEPTH].Value();
    engine.dispersal
        = fclamp(hw.params[PARAM_DISPERSAL].Value() * 1.15f - 0.05f, 0.f, 1.f);

    if(!interface.main_page.free_time_)
    {
        // Synced: resolution multiplies or divides the tempo
        hw.cvs[PARAM_RESOLUTION].SetCoeff(0.5f);
        engine.SetPeriod(tempo.samples);
        const float div[6]   = {1.f, 1.5f, 2.f, 3.f, 4.f, 8.f};
        const float mult[11] = {1.f,
                                0.75f,
                                0.5f,
                                0.333f,
                                0.25f,
                                0.16667f,
                                0.125f,
                                0.0625f,
                                0.03125f,
                                0.015625f,
                                0.0078125f};
        const float res = hw.params[PARAM_RESOLUTION].Value();
        const float x   = res * 2.f - 1.f;
        if(res < 0.5f)
        {
            // The global is clamped (not a temporary): so GCC does not turn it into vminnm and,
            // like the original, stores 5.0 and uses index 5 in the upper-limit branch.
            resolution_hyst = HysteresisIndex(x * -6.f, resolution_hyst);
            if(resolution_hyst > 5.f)
                resolution_hyst = 5.f;
            engine.time_mult = div[(int)resolution_hyst];
        }
        else
        {
            resolution_hyst = HysteresisIndex(x * 11.f, resolution_hyst);
            if(resolution_hyst > 10.f)
                resolution_hyst = 10.f;
            engine.time_mult = mult[(int)resolution_hyst];
        }
    }
    else
    {
        // Free time: resolution sets the period (100 samples to 4 s)
        engine.time_mult = 1.f;
        hw.cvs[PARAM_RESOLUTION].SetCoeff(0.001f);
        const float x = 1.f - hw.params[PARAM_RESOLUTION].Value();
        engine.SetPeriod(x * x * x * 191900.f + 100.f);
    }

    int chroma = (int)(hw.params[PARAM_CHROMA].Value() * 6.f - 0.5f);
    if(chroma < 0)
        chroma = 0;
    else if(chroma > 5)
        chroma = 5;
    switch(chroma)
    {
        case 0: engine.SetChromaMode(Chroma::MODE_LOWPASS); break;
        case 1: engine.SetChromaMode(Chroma::MODE_HIGHPASS); break;
        case 2: engine.SetChromaMode(Chroma::MODE_CRUSH); break;
        case 3: engine.SetChromaMode(Chroma::MODE_DRIVE); break;
        case 4: engine.SetChromaMode(Chroma::MODE_FOLD); break;
        case 5: engine.SetChromaMode(Chroma::MODE_CLIP); break;
    }

    engine.Process(in, out, size);

    // Output peak followers (decay ~0.9998845 per sample). Written as
    // `>=` → assign / else decay: GCC branches with bge and reloads the constants in the loop,
    // like the original; with a NaN at the output the peak decays instead of staying NaN.
    for(size_t i = 0; i < size; i++)
    {
        float l = fabsf(out[0][i]);
        if(l >= peak_l)
            peak_l = l;
        else
            peak_l *= 0.9998845f;
        if(peak_l < 1e-30f)
            peak_l = 0.f;
        out_env_l = peak_l;
        float r = fabsf(out[1][i]);
        if(r >= peak_r)
            peak_r = r;
        else
            peak_r *= 0.9998845f;
        if(peak_r < 1e-30f)
            peak_r = 0.f;
        out_env_r = peak_r;
    }
}

// @0x240049bc — Sonar output (DAC, 12 bits) and clock phases
static void DacCallback(uint16_t **out, size_t size)
{
    ClockLfos &c = clock_lfos;
    for(size_t i = 0; i < size; i++)
    {
        c.tempo_phase   = c.clock[2].Process();
        c.clock_out[0]  = c.clock[0].Process();
        c.clock_out[1]  = c.clock[1].Process();
        // When the tempo wraps, resyncs the phases of the voices that are not close
        // to an edge. The condition goes inside the loop: written as an if wrapping the
        // loops, GCC predicts it as a loop guard (27 %) and moves the block out of line.
        c.tempo_wrapped = c.tempo_phase < c.prev_tempo_phase;
        for(int v = 0; v < kNumVoices; v++)
            for(int g = 0; g < kNumGroups; g++)
            {
                auto       &p   = c.voice[g][v];
                const float out_v = c.voice_out[g][v];
                if(c.tempo_wrapped && (c.clock[2].GetFreq() < p.GetFreq() || out_v < 0.05f || out_v > 0.95f))
                    p.SetPhase(0.f);
            }
        c.prev_tempo_phase = c.tempo_phase;
        for(int v = 0; v < kNumVoices; v++)
        {
            c.voice_out[0][v] = c.voice[0][v].Process();
            c.voice_out[1][v] = c.voice[1][v].Process();
        }
        if(c.tempo_wrapped)
            engine.OnTempoTick();

        // Gate of each active voice (inverted if it plays in reverse)
        const int divisions                    = engine.divisions;
        bool      gate[kNumVoices][kNumGroups];
        for(int v = 0; v < kNumVoices; v++)
            for(int g = 0; g < kNumGroups; g++)
            {
                const float out_v = c.voice_out[g][v];
                gate[v][g]        = v < divisions
                             && (engine.reversed[g][v] ? out_v < 0.5f : out_v >= 0.5f);
            }

        if(sonar_mode == SONAR_STEPPED_VOLTAGE)
        {
            const int step  = 0x1000 / divisions / 2;
            uint16_t  value = 0;
            for(int v = 0; v < kNumVoices; v++)
                for(int g = 0; g < kNumGroups; g++)
                    if(gate[v][g])
                        value += step;
            out[0][i] = value >= 0xfff ? 0xfff : value;
        }
        else if(sonar_mode == SONAR_MASTER_CLOCK)
        {
            out[0][i] = c.tempo_phase < 0.5f ? 0xfff : 0;
        }
        else if(sonar_mode == SONAR_VARIABLE_CLOCK)
        {
            out[0][i] = c.voice_out[0][0] < 0.5f ? 0xfff : 0;
        }
    }
}

// Same buffer as libDaisy's dsy_patch_sm_dac_buffer (0xc0 bytes: the next object
// in .sram1_bss is at 0x30000248); the DAC uses a single channel and only the first half.
uint16_t DMA_BUFFER_MEM_SECTION dac_buffer[2][48]; // 0x30000188

// A button counts as held at power-up only if it reads as pressed for kBootHoldMs in a row.
// When it is not pressed, the first read returns at once and the boot is not delayed.
static constexpr int kBootHoldMs = 200;
static bool HeldAtBoot(Switch &button)
{
    for(int ms = 0; ms < kBootHoldMs; ms++)
    {
        if(!button.RawState())
            return false;
        System::Delay(1);
    }
    return button.RawState();
}

// @0x2400eaa0
int main(void)
{
    hw.Init();
    memset((void *)0xc0000000, 0, 0x4000000); // whole SDRAM

    usb.hw_ = &hw;
    USBHostHandle::Config usb_cfg;
    usb_cfg.connect_callback      = UsbConnect;
    usb_cfg.disconnect_callback   = UsbDisconnect;
    usb_cfg.class_active_callback = UsbClassActive;
    usb_cfg.error_callback        = UsbError;
    usb_cfg.userdata              = nullptr;
    usb_host.Init(usb_cfg);
    // libDaisy v7.1.0 no longer registers the mass storage class in Init
    usb_host.RegisterClass(USBH_MSC_CLASS);

    FatFSInterface::Config fsi_cfg;
    fsi_cfg.media = FatFSInterface::Config::MEDIA_USB;
    fsi.Init(fsi_cfg);
    f_mount(&fsi.GetUSBFileSystem(), fsi.GetUSBPath(), 0);

    usb.connected_  = false;
    usb.mounting_   = false;
    usb.mounted_    = false;
    usb.mount_time_ = 0;
    usb.on_mount_   = LoadOptions;
    usb.on_unmount_ = UnmountUsb;
    usb.SetEvent(UsbDrive::EVENT_NONE);

    // Buttons held at power-up. The original reads each pin once: a single spurious read
    // opened the input test page (Shift) or recalibrated the CV inputs with whatever was
    // patched at that moment (Freeze or Purge). Here they must be held for kBootHoldMs.
    const bool test_page = HeldAtBoot(hw.buttons[BUTTON_SHIFT]);
    bool       calibrate = HeldAtBoot(hw.buttons[BUTTON_FREEZE]);
    if(!calibrate)
        calibrate = HeldAtBoot(hw.buttons[BUTTON_PURGE]);

    interface.Init(&hw, &engine, test_page);

    sonar_mode = SONAR_STEPPED_VOLTAGE;
    resolution_hyst = 0.f;

    clock_lfos.prev_tempo_phase = 0.f;
    clock_lfos.tempo_phase      = 0.f;
    clock_lfos.clock[0].Init(hw.dac_samplerate, 2.f, 0.f);
    clock_lfos.clock[1].Init(hw.dac_samplerate, 2.f, 0.f);
    clock_lfos.clock[2].Init(hw.dac_samplerate, 2.f, 0.f);
    for(int v = 0; v < kNumVoices; v++)
    {
        clock_lfos.voice[0][v].Init(hw.dac_samplerate, 2.f, 0.f);
        clock_lfos.voice[1][v].Init(hw.dac_samplerate, 2.f, 0.f);
    }

    memset(voice_buffer[0], 0, sizeof(voice_buffer[0]));
    memset(voice_buffer[1], 0, sizeof(voice_buffer[1]));
    engine.Init(hw.seed.AudioSampleRate(),
                voice_buffer[0],
                voice_buffer[1],
                freeze_buffer[0],
                freeze_buffer[1]);

    hw.LoadCalibration();

    Settings defaults;
    settings_storage.Init(defaults, 0x400);
    Settings &s = settings_storage.GetSettings();
    engine.pending_feedback_mode   = s.feedback_mode;
    engine.pending_delay_mode      = s.delay_mode;
    engine.input_level             = s.input_level;
    engine.input_mode              = s.input_mode;
    interface.main_page.free_time_ = s.free_time;
    engine.pending_clear           = false;
    engine.feedback_mode_pending   = true;
    engine.SetReverbAmount(s.reverb_amount); // the original clamps it to [0, 1] when loading it
    engine.delay_mode_pending      = true;

    hw.seed.StartAudio(AudioCallback);
    hw.seed.dac.Start(dac_buffer[0], 48, DacCallback);

    TimerHandle::Config tim_cfg;
    tim_cfg.periph     = TimerHandle::Config::Peripheral::TIM_5;
    tim_cfg.dir        = TimerHandle::Config::CounterDir::UP;
    tim_cfg.period     = System::GetPClk2Freq() / 120;
    tim_cfg.enable_irq = true;
    hw.tim.Init(tim_cfg);
    hw.tim.SetCallback(TimerCallback);
    hw.tim.Start();

    const uint32_t boot_time = System::GetNow();
    uint32_t       last_save = boot_time;
    for(;;)
    {
        const uint32_t now = System::GetNow();
        usb_host.Process();
        usb.Process(usb_host);

        // Settings: every 5 s, and only written if they changed
        if(now - last_save > 5000)
        {
            Settings &cur = settings_storage.GetSettings();
            cur.reverb_amount = engine.reverb_amount;
            cur.feedback_mode = engine.feedback_mode;
            cur.delay_mode    = engine.delay_mode;
            cur.input_level   = engine.input_level;
            cur.free_time     = interface.main_page.free_time_;
            cur.input_mode    = engine.input_mode;
            settings_storage.Save();
            last_save = now;
        }

        if(now - boot_time > 500 && calibrate)
        {
            hw.CalibrateCvs();
            calibrate = false;
        }

        // Purge: switches each tap to its clean half and clears the other one
        if(engine.clear_request)
        {
            for(int g = 0; g < kNumGroups; g++)
                for(int v = 0; v < kNumVoices; v++)
                    engine.taps[g].tap[v].SwapBuffer();
            engine.purge_done = true;
            for(int g = 0; g < kNumGroups; g++)
                for(int v = 0; v < kNumVoices; v++)
                    engine.taps[g].tap[v].ClearSpare();
            engine.clear_request = false;
        }
    }
}
