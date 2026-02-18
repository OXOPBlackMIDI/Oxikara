#pragma once

#include <cstdint>
#include <string>

namespace oxikara::audio {

class MidiOutput {
public:
    MidiOutput() = default;
    ~MidiOutput();

    MidiOutput(const MidiOutput&) = delete;
    MidiOutput& operator=(const MidiOutput&) = delete;

    bool initialize();
    void shutdown();

    void send_short(std::uint32_t msg) const;

    [[nodiscard]] bool is_ready() const;
    [[nodiscard]] std::string backend_name() const;

private:
    enum class Backend {
        None,
        Kdmapi,
        WinMm,
    };

    Backend backend_ = Backend::None;

    void* omni_module_ = nullptr;
    bool owns_omni_module_ = false;

    void* winmm_module_ = nullptr;
    bool owns_winmm_module_ = false;

    void* hmo_ = nullptr;

    bool resolve_kdmapi();
    bool resolve_winmm();

    bool (*kdm_init_)() = nullptr;
    bool (*kdm_stop_)() = nullptr;
    void (*kdm_reset_)() = nullptr;
    bool (*kdm_available_)() = nullptr;
    void (*kdm_short_msg_)(std::uint32_t) = nullptr;

    std::uint32_t (*mm_out_open_)(void**, unsigned int, std::uintptr_t, std::uintptr_t, unsigned long) = nullptr;
    std::uint32_t (*mm_out_close_)(void*) = nullptr;
    std::uint32_t (*mm_out_short_msg_)(void*, unsigned long) = nullptr;
    std::uint32_t (*mm_out_reset_)(void*) = nullptr;
};

} // namespace oxikara::audio
