// Nautilus (Qu-Bit) v1.2.0 — user options in /options.json (USB stick)
// LoadOptions @0x2400e120 (mount callback), WriteVersionFile @0x2400e0cc.
//
// Expected format:
//   { "nautilus": [ { "name": "Mix Mode", "value": "Linear" }, ... ] }
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "daisy_seed.h"
#include "core_json.h"
#include "nautilus_hw.h"
#include "nautilus_engine.h"
#include "ui.h"

namespace nautilus
{
using namespace daisy;

static constexpr int    kMaxOptions      = 32;
static constexpr int    kOptionStrLen    = 64;
static constexpr size_t kOptionsFileSize = 4096;

// Defined in main.cpp
extern NautilusHw     hw;
extern NautilusEngine engine;
extern Interface      interface;
extern FatFSInterface fsi;
extern const char    *kModuleName;
extern const char    *kFirmwareVersion;

// "Sonar" output (DacCallback @0x240049bc)
enum SonarMode
{
    SONAR_STEPPED_VOLTAGE,
    SONAR_MASTER_CLOCK,
    SONAR_VARIABLE_CLOCK,
};
int BSS_ORDER(01) sonar_mode; // 0x2403de54

FIL  BSS_ORDER(02) options_file;                              // 0x2403de58
char BSS_ORDER(03) options_json[kOptionsFileSize];            // 0x2403e084
FIL  BSS_ORDER(28) version_file;                              // 0x2407a4bc

// Names and values in a single object (0x24078a80): the original walks both with one
// pointer and reaches the names as values − 0x800; with two separate arrays GCC cannot
// relate their addresses and loads each one separately.
struct OptionTable
{
    char names[kMaxOptions][kOptionStrLen];  // +0x000
    char values[kMaxOptions][kOptionStrLen]; // +0x800
};
OptionTable BSS_ORDER(23) option_table;

// @0x2400e0cc. inline (weak symbol) and out of line: the original never tail-calls it,
// and on ARM GCC does not tail-call weak symbols.
inline __attribute__((noinline)) void WriteVersionFile(FIL *fp, const char *path)
{
    if(f_open(fp, path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
    {
        char line[512];
        UINT bw = 0;
        sprintf(line, "%s Version: %s\n", kModuleName, kFirmwareVersion);
        f_write(fp, line, strlen(line), &bw);
        f_close(fp);
    }
}

// Index of the option, or -1 (inlined at each use)
static inline int FindOption(const char *name)
{
    for(int i = 0; i < kMaxOptions; i++)
        if(strcmp(name, option_table.names[i]) == 0)
            return i;
    return -1;
}

// Values of "Attenuverter 1/2 Target"; if not recognized, `fallback`
static inline int ParseParamName(const char *v, int fallback)
{
    if(strcmp(v, "sensors") == 0)
        return PARAM_SENSORS;
    if(strcmp(v, "dispersal") == 0)
        return PARAM_DISPERSAL;
    if(strcmp(v, "resolution") == 0)
        return PARAM_RESOLUTION;
    if(strcmp(v, "reversal") == 0)
        return PARAM_REVERSAL;
    if(strcmp(v, "depth") == 0)
        return PARAM_DEPTH;
    if(strcmp(v, "feedback") == 0)
        return PARAM_FEEDBACK;
    if(strcmp(v, "mix") == 0)
        return PARAM_MIX;
    if(strcmp(v, "chroma") == 0)
        return PARAM_CHROMA;
    return fallback;
}

// Changes an attenuverter's target and passes it its current amount
static inline void SetAttenuverterTarget(int which, int target)
{
    const int   old = hw.atten_target[which];
    const float amt = hw.params[old].atten;
    hw.atten_target[which]  = target;
    hw.params[old].atten    = 1.f;
    hw.params[target].atten = amt;
}

// "true"/"false"; any other value leaves the option as it was
static inline void ParseBool(const char *v, bool &out)
{
    if(strcmp(v, "true") == 0)
        out = true;
    else if(strcmp(v, "false") == 0)
        out = false;
}

// @0x2400e120 — UsbDrive::on_mount_
static void LoadOptions()
{
    f_mount(&fsi.GetUSBFileSystem(), fsi.GetUSBPath(), 0);
    memset(options_json, 0, sizeof(options_json));

    if(f_open(&options_file, "/options.json", FA_READ) == FR_OK)
    {
        const FSIZE_t size = f_size(&options_file);
        if(size > kOptionsFileSize)
        {
            f_close(&options_file);
            WriteVersionFile(&version_file, "Nautilus_Version.txt");
            return;
        }
        bool ok;
        {
            // br shares its stack slot with the loop's `query`
            UINT br = 0;
            f_read(&options_file, options_json, size, &br);
            const FRESULT res = f_close(&options_file);
            ok                = size == br && res == FR_OK;
        }
        if(ok)
        {
            const size_t len = strlen(options_json);
            WriteVersionFile(&version_file, "Nautilus_Version.txt");
            if(JSON_Validate(options_json, len) != JSONSuccess)
                return;

            for(int i = 0; i < kMaxOptions; i++)
            {
                char   query[20];
                char  *value;
                size_t value_len;
                sprintf(query, "nautilus[%d].name", i);
                if(JSON_Search(options_json, len, query, strlen(query), &value, &value_len)
                   != JSONSuccess)
                    continue;
                char saved       = value[value_len];
                value[value_len] = '\0';
                strcpy(option_table.names[i], value);
                value[value_len] = saved;

                sprintf(query, "nautilus[%d].value", i);
                if(JSON_Search(options_json, len, query, strlen(query), &value, &value_len)
                   != JSONSuccess)
                    continue;
                saved            = value[value_len];
                value[value_len] = '\0';
                strcpy(option_table.values[i], value);
                value[value_len] = saved;
            }

            int i;
            if((i = FindOption("Transpose Up")) >= 0)
            {
                const int semitones = atoi(option_table.values[i]);
                if(semitones > 0)
                {
                    engine.transpose_up = semitones;
                    engine.RequestDelayMode(engine.delay_mode);
                }
            }
            if((i = FindOption("Transpose Down")) >= 0)
            {
                const int semitones = atoi(option_table.values[i]);
                if(semitones > 0)
                {
                    engine.transpose_down = -(float)semitones;
                    engine.RequestDelayMode(engine.delay_mode);
                }
            }
            if((i = FindOption("Attenuverter 1 Target")) >= 0)
                SetAttenuverterTarget(0, ParseParamName(option_table.values[i], PARAM_DISPERSAL));
            if((i = FindOption("Attenuverter 2 Target")) >= 0)
                SetAttenuverterTarget(1, ParseParamName(option_table.values[i], PARAM_FEEDBACK));
            if((i = FindOption("Freeze Mix Behavior")) >= 0)
            {
                const char *v = option_table.values[i];
                if(strcmp(v, "Normal") == 0)
                {
                    engine.freeze_always_wet = false;
                    engine.freeze_punch_in   = false;
                }
                else if(strcmp(v, "Punch In") == 0)
                {
                    engine.freeze_always_wet = false;
                    engine.freeze_punch_in   = true;
                }
                else if(strcmp(v, "Always Wet") == 0)
                {
                    engine.freeze_always_wet = true;
                    engine.freeze_punch_in   = false;
                }
            }
            if((i = FindOption("Quantize Freeze")) >= 0)
                ParseBool(option_table.values[i], engine.quantize_freeze);
            if((i = FindOption("Sonar Output")) >= 0)
            {
                const char *v = option_table.values[i];
                if(strcmp(v, "Stepped Voltage") == 0)
                    sonar_mode = SONAR_STEPPED_VOLTAGE;
                else if(strcmp(v, "Master Clock") == 0)
                    sonar_mode = SONAR_MASTER_CLOCK;
                else if(strcmp(v, "Variable Clock") == 0)
                    sonar_mode = SONAR_VARIABLE_CLOCK;
            }
            if((i = FindOption("Clear on Mode Change")) >= 0)
                ParseBool(option_table.values[i], engine.clear_on_mode_change);
            if((i = FindOption("Buffer Locked Freeze")) >= 0)
                ParseBool(option_table.values[i], engine.buffer_locked_freeze);
            if((i = FindOption("Continuous Purge")) >= 0)
                engine.continuous_purge = strcmp(option_table.values[i], "true") == 0;
            if((i = FindOption("Mix Mode")) >= 0)
            {
                const char *v = option_table.values[i];
                if(strcmp(v, "Linear") == 0)
                    engine.mix_mode = MIX_LINEAR;
                else if(strcmp(v, "Constant Power") == 0)
                    engine.mix_mode = MIX_CONSTANT_POWER;
                else if(strcmp(v, "Transition") == 0)
                    engine.mix_mode = MIX_TRANSITION;
                else if(strcmp(v, "Wet Amount") == 0)
                    engine.mix_mode = MIX_WET_AMOUNT;
            }
            if((i = FindOption("Reverb Preset")) >= 0)
            {
                const char *v = option_table.values[i];
                if(strcmp(v, "Normal") == 0)
                    engine.reverb_preset = REVERB_NORMAL;
                else if(strcmp(v, "Bright") == 0)
                    engine.reverb_preset = REVERB_BRIGHT;
                else if(strcmp(v, "Dark") == 0)
                    engine.reverb_preset = REVERB_DARK;
            }

            // "Color 0".."Color 5": "#rrggbb"
            for(int c = 0; c < kNumColors; c++)
            {
                FixedCapStr<16> key("Color ");
                key.AppendInt(c);
                if((i = FindOption(key.Cstr())) < 0)
                    continue;
                const char *v = option_table.values[i];
                if(v[0] != '#' || strlen(v + 1) != 6)
                    continue;
                unsigned int r = 0, g = 0, b = 0;
                sscanf(v + 1, "%02x%02x%02x", &r, &g, &b);
                Color color;
                color.Init(r * (1.f / 255.f), g * (1.f / 255.f), b * (1.f / 255.f));
                interface.main_page.colors_[c] = color;
            }
            return;
        }
    }
    WriteVersionFile(&version_file, "Nautilus_Version.txt");
}

} // namespace nautilus
