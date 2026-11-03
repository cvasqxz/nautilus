// Nautilus (Qu-Bit) v1.2.0 — USB memory stick for options.json
// Global state at 0x2405e4bc (0x28 bytes). FatFSInterface at 0x2405e4e8 and
// USBHostHandle at 0x2405e968 are separate globals (see main.cpp).
#pragma once

#include <stddef.h>
#include "daisy_seed.h"
#include "nautilus_hw.h"

namespace nautilus
{
using namespace daisy;

class UsbDrive
{
  public:
    enum Event
    {
        EVENT_NONE,
        EVENT_CONNECT,      // @0x240041e8
        EVENT_CLASS_ACTIVE, // @0x240041b8
        EVENT_DISCONNECT,   // @0x240041d0
    };

    // Called by the USB host callbacks (interrupt context)
    void SetEvent(Event e)
    {
        prev_event_      = event_;
        event_           = e;
        last_event_time_ = System::GetNow();
    }

    // One pass of the main loop (inlined in main @0x24010640..0x240106f0)
    void Process(USBHostHandle &host)
    {
        switch(event_)
        {
            case EVENT_CLASS_ACTIVE:
                mounting_ = true;
                if(on_mount_)
                    on_mount_();
                mounted_    = true;
                mount_time_ = System::GetNow();
                SetEvent(EVENT_NONE);
                break;
            case EVENT_DISCONNECT:
                // mounting_/mounted_ are left as they were
                if(on_unmount_)
                    on_unmount_();
                connected_ = false;
                SetEvent(EVENT_NONE);
                break;
            case EVENT_CONNECT:
                connected_    = true;
                connect_time_ = System::GetNow();
                SetEvent(EVENT_NONE);
                break;
            default: break;
        }

        // Watchdog: with no events for 5 s a re-enumeration is forced.
        // Until a mount has ever happened, it repeats on every pass.
        if(System::GetNow() - last_event_time_ > 5000)
        {
            if(!mounted_)
                host.ReEnumerate();
            if(host.IsPortEnabled() != host.IsDeviceConnected())
                host.ReEnumerate();
        }
    }

    // Activity indicator for the UI (TimerCallback @0x24004770)
    bool IsBusy()
    {
        if(!connected_)
            return false;
        const uint32_t now = System::GetNow();
        if(mounting_ && !mounted_)
            return true;
        return now - mount_time_ < 500;
    }

    NautilusHw *hw_;                 // +0x00
    void (*on_mount_)();             // +0x04
    void (*on_unmount_)();           // +0x08
    bool              mounting_;     // +0x0c
    bool              mounted_;      // +0x0d
    uint32_t          mount_time_;   // +0x10
    uint32_t          last_event_time_; // +0x14
    int               event_;        // +0x18
    int               prev_event_;   // +0x1c
    uint32_t          connect_time_; // +0x20
    bool              connected_;    // +0x24
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(UsbDrive, mount_time_) == 0x10, "layout");
static_assert(offsetof(UsbDrive, event_) == 0x18, "layout");
static_assert(offsetof(UsbDrive, connected_) == 0x24, "layout");
static_assert(sizeof(UsbDrive) == 0x28, "layout");
#pragma GCC diagnostic pop

} // namespace nautilus
