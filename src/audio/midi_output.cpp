#include "oxikara/audio/midi_output.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#endif

namespace oxikara::audio {

MidiOutput::~MidiOutput()
{
    shutdown();
}

bool MidiOutput::initialize()
{
#if !defined(_WIN32)
    return false;
#else
    shutdown();

    if (resolve_kdmapi()) {
        backend_ = Backend::Kdmapi;
        return true;
    }

    if (resolve_winmm()) {
        backend_ = Backend::WinMm;
        return true;
    }

    return false;
#endif
}

void MidiOutput::shutdown()
{
#if defined(_WIN32)
    if (backend_ == Backend::Kdmapi) {
        if (kdm_reset_ != nullptr) {
            kdm_reset_();
        }
        if (kdm_stop_ != nullptr) {
            kdm_stop_();
        }
    }

    if (backend_ == Backend::WinMm && hmo_ != nullptr) {
        if (mm_out_reset_ != nullptr) {
            mm_out_reset_(hmo_);
        }
        if (mm_out_close_ != nullptr) {
            mm_out_close_(hmo_);
        }
        hmo_ = nullptr;
    }

    if (omni_module_ != nullptr && owns_omni_module_) {
        FreeLibrary(static_cast<HMODULE>(omni_module_));
    }
    if (winmm_module_ != nullptr && owns_winmm_module_) {
        FreeLibrary(static_cast<HMODULE>(winmm_module_));
    }
#endif

    backend_ = Backend::None;
    omni_module_ = nullptr;
    owns_omni_module_ = false;
    winmm_module_ = nullptr;
    owns_winmm_module_ = false;

    kdm_init_ = nullptr;
    kdm_stop_ = nullptr;
    kdm_reset_ = nullptr;
    kdm_available_ = nullptr;
    kdm_short_msg_ = nullptr;

    mm_out_open_ = nullptr;
    mm_out_close_ = nullptr;
    mm_out_short_msg_ = nullptr;
    mm_out_reset_ = nullptr;
}

void MidiOutput::send_short(const std::uint32_t msg) const
{
#if defined(_WIN32)
    if (backend_ == Backend::Kdmapi && kdm_short_msg_ != nullptr) {
        kdm_short_msg_(msg);
    } else if (backend_ == Backend::WinMm && mm_out_short_msg_ != nullptr && hmo_ != nullptr) {
        mm_out_short_msg_(hmo_, static_cast<unsigned long>(msg));
    }
#else
    static_cast<void>(msg);
#endif
}

bool MidiOutput::is_ready() const
{
    return backend_ != Backend::None;
}

std::string MidiOutput::backend_name() const
{
    switch (backend_) {
    case Backend::Kdmapi:
        return "KDMAPI";
    case Backend::WinMm:
        return "WinMM";
    default:
        return "None";
    }
}

bool MidiOutput::resolve_kdmapi()
{
#if !defined(_WIN32)
    return false;
#else
    HMODULE mod = GetModuleHandleA("OmniMIDI");
    if (mod == nullptr) {
        mod = LoadLibraryA("OmniMIDI.dll");
        owns_omni_module_ = (mod != nullptr);
    }
    if (mod == nullptr) {
        return false;
    }

    omni_module_ = mod;

    kdm_init_ = reinterpret_cast<bool (*)()>(GetProcAddress(mod, "InitializeKDMAPIStream"));
    kdm_stop_ = reinterpret_cast<bool (*)()>(GetProcAddress(mod, "TerminateKDMAPIStream"));
    kdm_reset_ = reinterpret_cast<void (*)()>(GetProcAddress(mod, "ResetKDMAPIStream"));
    kdm_available_ = reinterpret_cast<bool (*)()>(GetProcAddress(mod, "IsKDMAPIAvailable"));
    kdm_short_msg_ = reinterpret_cast<void (*)(std::uint32_t)>(GetProcAddress(mod, "SendDirectData"));

    if (kdm_short_msg_ == nullptr || kdm_init_ == nullptr || kdm_stop_ == nullptr) {
        return false;
    }

    if (kdm_available_ != nullptr && !kdm_available_()) {
        return false;
    }

    return kdm_init_();
#endif
}

bool MidiOutput::resolve_winmm()
{
#if !defined(_WIN32)
    return false;
#else
    HMODULE mod = GetModuleHandleA("winmm.dll");
    if (mod == nullptr) {
        mod = LoadLibraryA("winmm.dll");
        owns_winmm_module_ = (mod != nullptr);
    }
    if (mod == nullptr) {
        return false;
    }

    winmm_module_ = mod;

    mm_out_open_ = reinterpret_cast<std::uint32_t (*)(void**, unsigned int, std::uintptr_t, std::uintptr_t, unsigned long)>(
        GetProcAddress(mod, "midiOutOpen"));
    mm_out_close_ =
        reinterpret_cast<std::uint32_t (*)(void*)>(GetProcAddress(mod, "midiOutClose"));
    mm_out_short_msg_ = reinterpret_cast<std::uint32_t (*)(void*, unsigned long)>(
        GetProcAddress(mod, "midiOutShortMsg"));
    mm_out_reset_ =
        reinterpret_cast<std::uint32_t (*)(void*)>(GetProcAddress(mod, "midiOutReset"));

    if (mm_out_open_ == nullptr || mm_out_close_ == nullptr || mm_out_short_msg_ == nullptr || mm_out_reset_ == nullptr) {
        return false;
    }

    void* local_hmo = nullptr;
    const std::uint32_t res = mm_out_open_(&local_hmo, static_cast<unsigned int>(MIDI_MAPPER), 0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR || local_hmo == nullptr) {
        return false;
    }

    hmo_ = local_hmo;
    return true;
#endif
}

} // namespace oxikara::audio
