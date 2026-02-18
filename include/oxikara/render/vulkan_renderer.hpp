#pragma once

#include <cstdint>
#include <vector>

namespace oxikara::render {

struct RenderNote {
    std::uint8_t channel = 0;
    std::uint8_t note = 0;
    std::uint8_t velocity = 100;
    double start_sec = 0.0;
    double end_sec = 0.0;
    float r = 0.8f;
    float g = 0.8f;
    float b = 0.8f;
};

struct RenderSong {
    std::vector<RenderNote> notes;
    double duration_sec = 0.0;
    std::size_t note_count = 0;
    std::uint8_t min_note = 21;
    std::uint8_t max_note = 108;
};

class VulkanRenderer {
public:
    bool initialize(std::uint32_t width = 1280, std::uint32_t height = 720, const char* title = "Oxikara");
    void set_song(RenderSong song);
    void start_playback();
    void shutdown();
    bool draw_frame();
    [[nodiscard]] double playback_time() const;
    [[nodiscard]] bool should_close() const;
    [[nodiscard]] bool playback_finished() const;

private:
    bool initialized_ = false;

#if defined(OXIKARA_HAS_VULKAN) && defined(OXIKARA_HAS_GLFW)
    struct Impl;
    Impl* impl_ = nullptr;
#endif
};

} // namespace oxikara::render
