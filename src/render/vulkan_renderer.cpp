#include "oxikara/render/vulkan_renderer.hpp"

#include "oxikara/core/log.hpp"
#include "oxikara/render/piano_shaders_spv.hpp"

#if defined(OXIKARA_HAS_VULKAN) && defined(OXIKARA_HAS_GLFW)
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#endif

namespace oxikara::render {

#if defined(OXIKARA_HAS_VULKAN) && defined(OXIKARA_HAS_GLFW)
namespace {

constexpr std::uint32_t kFramesInFlight = 2;

struct Vertex {
    float x = 0.0f;
    float y = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;
    bool complete() const { return graphics.has_value() && present.has_value(); }
};

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

inline bool is_black_key(const int midi_note)
{
    const int semitone = midi_note % 12;
    return semitone == 1 || semitone == 3 || semitone == 6 || semitone == 8 || semitone == 10;
}

inline int white_index_in_octave(const int semitone)
{
    static constexpr std::array<int, 12> lut = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    return lut[semitone];
}

inline double black_center_offset(const int semitone)
{
    switch (semitone) {
    case 1:
        return 0.7;
    case 3:
        return 1.7;
    case 6:
        return 3.7;
    case 8:
        return 4.7;
    case 10:
        return 5.7;
    default:
        return 0.5;
    }
}

inline int white_number(const int midi_note)
{
    const int octave = midi_note / 12;
    const int semitone = midi_note % 12;
    return octave * 7 + white_index_in_octave(semitone);
}

void push_rect(std::vector<Vertex>& vertices, const float x0, const float y0, const float x1, const float y1, const float r, const float g, const float b)
{
    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    const Vertex a{x0, y0, r, g, b};
    const Vertex c{x1, y0, r, g, b};
    const Vertex d{x1, y1, r, g, b};
    const Vertex e{x0, y1, r, g, b};

    vertices.push_back(a);
    vertices.push_back(c);
    vertices.push_back(d);
    vertices.push_back(a);
    vertices.push_back(d);
    vertices.push_back(e);
}

constexpr std::array<std::uint8_t, 7> glyph_rows(const char ch)
{
    switch (ch) {
    case 'A': return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'E': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    case 'F': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    case 'H': return {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    case 'M': return {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    case 'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    case 'O': return {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'P': return {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    case 'R': return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    case 'S': return {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    case 'T': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'Y': return {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    case ':': return {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
    case '.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06};
    case '0': return {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    case '1': return {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    case '2': return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    case '3': return {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    case '4': return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    case '5': return {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
    case '6': return {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    case '7': return {0x1F, 0x01, 0x01, 0x02, 0x04, 0x08, 0x08};
    case '8': return {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    case '9': return {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};
    default: return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    }
}

void push_text(
    std::vector<Vertex>& vertices,
    float x_left,
    const float y_top,
    const float pixel_w,
    const float pixel_h,
    const float spacing,
    const float r,
    const float g,
    const float b,
    const std::string& text)
{
    for (const char ch : text) {
        if (ch == ' ') {
            x_left += pixel_w * (5.0f + spacing);
            continue;
        }
        const auto rows = glyph_rows(ch);
        for (int row = 0; row < 7; ++row) {
            const std::uint8_t bits = rows[static_cast<std::size_t>(row)];
            for (int col = 0; col < 5; ++col) {
                const std::uint8_t mask = static_cast<std::uint8_t>(1u << (4 - col));
                if ((bits & mask) == 0u) {
                    continue;
                }
                const float x0 = x_left + pixel_w * static_cast<float>(col);
                const float y0 = y_top - pixel_h * static_cast<float>(row + 1);
                push_rect(vertices, x0, y0, x0 + pixel_w, y0 + pixel_h, r, g, b);
            }
        }
        x_left += pixel_w * (5.0f + spacing);
    }
}

QueueFamilies find_families(const VkPhysicalDevice dev, const VkSurfaceKHR surface)
{
    QueueFamilies out;
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, props.data());

    for (std::uint32_t i = 0; i < count; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            out.graphics = i;
        }
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &present);
        if (present == VK_TRUE) {
            out.present = i;
        }
        if (out.complete()) {
            break;
        }
    }
    return out;
}

SwapchainSupport query_swapchain(const VkPhysicalDevice dev, const VkSurfaceKHR surface)
{
    SwapchainSupport out;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, surface, &out.capabilities);

    std::uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &fmt_count, nullptr);
    out.formats.resize(fmt_count);
    if (fmt_count > 0) {
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &fmt_count, out.formats.data());
    }

    std::uint32_t pm_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &pm_count, nullptr);
    out.present_modes.resize(pm_count);
    if (pm_count > 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &pm_count, out.present_modes.data());
    }

    return out;
}

VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats.front();
}

VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes)
{
    for (const auto m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
            return m;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& caps, GLFWwindow* window)
{
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }

    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(window, &w, &h);

    VkExtent2D e{};
    e.width = static_cast<std::uint32_t>(w);
    e.height = static_cast<std::uint32_t>(h);
    return e;
}

std::optional<std::uint32_t> find_memory_type(
    const VkPhysicalDevice physical,
    const std::uint32_t type_mask,
    const VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(physical, &mem_props);
    for (std::uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_mask & (1u << i)) != 0u && (mem_props.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace

struct VulkanRenderer::Impl {
    GLFWwindow* window = nullptr;

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphics_queue = VK_NULL_HANDLE;
    VkQueue present_queue = VK_NULL_HANDLE;

    QueueFamilies families;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swap_format = VK_FORMAT_UNDEFINED;
    VkExtent2D swap_extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> image_views;

    VkRenderPass render_pass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
    std::size_t vertex_capacity = 0;

    VkCommandPool command_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers;

    std::array<VkSemaphore, kFramesInFlight> sem_img{};
    std::array<VkSemaphore, kFramesInFlight> sem_render{};
    std::array<VkFence, kFramesInFlight> fences{};
    std::uint32_t frame = 0;

    RenderSong song;
    double start_time = 0.0;
    bool playback_started = false;
    std::size_t first_note_hint = 0;
    std::vector<Vertex> cpu_vertices;
    std::array<float, 128> key_left{};
    std::array<float, 128> key_right{};
    std::array<bool, 128> key_visible{};
    std::array<float, 128> hit_r{};
    std::array<float, 128> hit_g{};
    std::array<float, 128> hit_b{};
    double fps_accum = 0.0;
    std::uint32_t fps_frames = 0;
    double fps_value = 0.0;
    double last_frame_time = 0.0;
    double playhead_sec = 0.0;
    bool paused = false;
    bool prev_space = false;
    bool prev_left = false;
    bool prev_right = false;
    bool prev_mouse_left = false;

    double now() const { return playhead_sec; }

    void build_key_layout()
    {
        key_visible.fill(false);
        key_left.fill(0.0f);
        key_right.fill(0.0f);

        const int min_note = static_cast<int>(song.min_note);
        const int max_note = static_cast<int>(song.max_note);
        if (max_note < min_note) {
            return;
        }

        const int base_white = white_number(min_note);
        const int last_white = white_number(max_note);
        const int white_count = std::max(1, last_white - base_white + 1);
        const float white_w = 2.0f / static_cast<float>(white_count);
        const float black_w = white_w * 0.62f;

        for (int note = min_note; note <= max_note; ++note) {
            const std::size_t idx = static_cast<std::size_t>(note);
            const int semitone = note % 12;
            if (is_black_key(note)) {
                const double white_pos = static_cast<double>(white_number(note) - base_white) + black_center_offset(semitone);
                const float cx = -1.0f + static_cast<float>(white_pos * static_cast<double>(white_w));
                key_left[idx] = cx - black_w * 0.5f;
                key_right[idx] = cx + black_w * 0.5f;
            } else {
                const int w = white_number(note) - base_white;
                const float x0 = -1.0f + static_cast<float>(w) * white_w;
                key_left[idx] = x0;
                key_right[idx] = x0 + white_w;
            }
            key_visible[idx] = true;
        }
    }

    bool ensure_vertex_capacity(const std::size_t needed_vertices)
    {
        if (needed_vertices <= vertex_capacity) {
            return true;
        }

        std::size_t new_capacity = (vertex_capacity == 0) ? 16384 : vertex_capacity;
        while (new_capacity < needed_vertices) {
            new_capacity *= 2;
        }

        if (vertex_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, vertex_buffer, nullptr);
            vertex_buffer = VK_NULL_HANDLE;
        }
        if (vertex_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, vertex_memory, nullptr);
            vertex_memory = VK_NULL_HANDLE;
        }

        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = static_cast<VkDeviceSize>(new_capacity * sizeof(Vertex));
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bci, nullptr, &vertex_buffer) != VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device, vertex_buffer, &req);
        const auto mem_idx = find_memory_type(
            physical,
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (!mem_idx.has_value()) {
            return false;
        }

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = *mem_idx;
        if (vkAllocateMemory(device, &mai, nullptr, &vertex_memory) != VK_SUCCESS) {
            return false;
        }
        if (vkBindBufferMemory(device, vertex_buffer, vertex_memory, 0) != VK_SUCCESS) {
            return false;
        }

        vertex_capacity = new_capacity;
        return true;
    }

    void destroy_swapchain_objects()
    {
        for (auto fb : framebuffers) {
            vkDestroyFramebuffer(device, fb, nullptr);
        }
        framebuffers.clear();

        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        if (pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
            pipeline_layout = VK_NULL_HANDLE;
        }

        if (!command_buffers.empty()) {
            vkFreeCommandBuffers(device, command_pool, static_cast<std::uint32_t>(command_buffers.size()), command_buffers.data());
            command_buffers.clear();
        }

        if (render_pass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, render_pass, nullptr);
            render_pass = VK_NULL_HANDLE;
        }

        for (auto v : image_views) {
            vkDestroyImageView(device, v, nullptr);
        }
        image_views.clear();

        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
    }
};

#endif

bool VulkanRenderer::initialize(const std::uint32_t width, const std::uint32_t height, const char* title)
{
#if defined(OXIKARA_HAS_VULKAN) && defined(OXIKARA_HAS_GLFW)
    if (glfwInit() != GLFW_TRUE) {
        core::log(core::LogLevel::Error, "GLFW init failed.");
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    impl_ = new Impl();
    impl_->window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title, nullptr, nullptr);
    if (impl_->window == nullptr) {
        core::log(core::LogLevel::Error, "Failed to create GLFW window.");
        glfwTerminate();
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    std::uint32_t ext_count = 0;
    const char** exts = glfwGetRequiredInstanceExtensions(&ext_count);
    if (exts == nullptr || ext_count == 0) {
        shutdown();
        return false;
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Oxikara";
    app.applicationVersion = VK_MAKE_VERSION(0, 6, 0);
    app.pEngineName = "Oxikara";
    app.engineVersion = VK_MAKE_VERSION(0, 6, 0);
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = ext_count;
    ici.ppEnabledExtensionNames = exts;

    if (vkCreateInstance(&ici, nullptr, &impl_->instance) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    if (glfwCreateWindowSurface(impl_->instance, impl_->window, nullptr, &impl_->surface) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    std::uint32_t dev_count = 0;
    vkEnumeratePhysicalDevices(impl_->instance, &dev_count, nullptr);
    if (dev_count == 0) {
        shutdown();
        return false;
    }
    std::vector<VkPhysicalDevice> devs(dev_count);
    vkEnumeratePhysicalDevices(impl_->instance, &dev_count, devs.data());

    for (auto d : devs) {
        const QueueFamilies q = find_families(d, impl_->surface);
        if (!q.complete()) {
            continue;
        }
        const SwapchainSupport s = query_swapchain(d, impl_->surface);
        if (s.formats.empty() || s.present_modes.empty()) {
            continue;
        }
        impl_->physical = d;
        impl_->families = q;
        break;
    }

    if (impl_->physical == VK_NULL_HANDLE) {
        shutdown();
        return false;
    }

    std::set<std::uint32_t> uq = {*impl_->families.graphics, *impl_->families.present};
    std::vector<VkDeviceQueueCreateInfo> qinfos;
    const float prio = 1.0f;
    for (auto q : uq) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = q;
        qi.queueCount = 1;
        qi.pQueuePriorities = &prio;
        qinfos.push_back(qi);
    }

    const char* dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = static_cast<std::uint32_t>(qinfos.size());
    dci.pQueueCreateInfos = qinfos.data();
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = dev_exts;

    if (vkCreateDevice(impl_->physical, &dci, nullptr, &impl_->device) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    vkGetDeviceQueue(impl_->device, *impl_->families.graphics, 0, &impl_->graphics_queue);
    vkGetDeviceQueue(impl_->device, *impl_->families.present, 0, &impl_->present_queue);

    const SwapchainSupport sw = query_swapchain(impl_->physical, impl_->surface);
    const VkSurfaceFormatKHR sf = choose_surface_format(sw.formats);
    const VkPresentModeKHR pm = choose_present_mode(sw.present_modes);
    const VkExtent2D ex = choose_extent(sw.capabilities, impl_->window);

    std::uint32_t image_count = sw.capabilities.minImageCount + 1;
    if (sw.capabilities.maxImageCount > 0 && image_count > sw.capabilities.maxImageCount) {
        image_count = sw.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = impl_->surface;
    sci.minImageCount = image_count;
    sci.imageFormat = sf.format;
    sci.imageColorSpace = sf.colorSpace;
    sci.imageExtent = ex;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const std::uint32_t qidx[] = {*impl_->families.graphics, *impl_->families.present};
    if (impl_->families.graphics != impl_->families.present) {
        sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = qidx;
    } else {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    sci.preTransform = sw.capabilities.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = pm;
    sci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(impl_->device, &sci, nullptr, &impl_->swapchain) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    vkGetSwapchainImagesKHR(impl_->device, impl_->swapchain, &image_count, nullptr);
    impl_->images.resize(image_count);
    vkGetSwapchainImagesKHR(impl_->device, impl_->swapchain, &image_count, impl_->images.data());
    impl_->swap_format = sf.format;
    impl_->swap_extent = ex;

    impl_->image_views.reserve(impl_->images.size());
    for (auto img : impl_->images) {
        VkImageViewCreateInfo iv{};
        iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv.image = img;
        iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
        iv.format = impl_->swap_format;
        iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        iv.subresourceRange.baseMipLevel = 0;
        iv.subresourceRange.levelCount = 1;
        iv.subresourceRange.baseArrayLayer = 0;
        iv.subresourceRange.layerCount = 1;
        VkImageView view = VK_NULL_HANDLE;
        if (vkCreateImageView(impl_->device, &iv, nullptr, &view) != VK_SUCCESS) {
            shutdown();
            return false;
        }
        impl_->image_views.push_back(view);
    }

    VkAttachmentDescription color{};
    color.format = impl_->swap_format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{};
    ref.attachment = 0;
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &color;
    rp.subpassCount = 1;
    rp.pSubpasses = &sp;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    if (vkCreateRenderPass(impl_->device, &rp, nullptr, &impl_->render_pass) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    VkShaderModuleCreateInfo vs_ci{};
    vs_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vs_ci.codeSize = shaders::kPianoVertSpv.size() * sizeof(std::uint32_t);
    vs_ci.pCode = shaders::kPianoVertSpv.data();
    VkShaderModule vs = VK_NULL_HANDLE;
    if (vkCreateShaderModule(impl_->device, &vs_ci, nullptr, &vs) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    VkShaderModuleCreateInfo fs_ci{};
    fs_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fs_ci.codeSize = shaders::kPianoFragSpv.size() * sizeof(std::uint32_t);
    fs_ci.pCode = shaders::kPianoFragSpv.data();
    VkShaderModule fs = VK_NULL_HANDLE;
    if (vkCreateShaderModule(impl_->device, &fs_ci, nullptr, &fs) != VK_SUCCESS) {
        vkDestroyShaderModule(impl_->device, vs, nullptr);
        shutdown();
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attrs{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset = 0;
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = sizeof(float) * 2u;

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = static_cast<float>(impl_->swap_extent.height);
    vp.width = static_cast<float>(impl_->swap_extent.width);
    vp.height = -static_cast<float>(impl_->swap_extent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;

    VkRect2D sc{};
    sc.offset = {0, 0};
    sc.extent = impl_->swap_extent;

    VkPipelineViewportStateCreateInfo vp_state{};
    vp_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp_state.viewportCount = 1;
    vp_state.pViewports = &vp;
    vp_state.scissorCount = 1;
    vp_state.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_att;

    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (vkCreatePipelineLayout(impl_->device, &pl, nullptr, &impl_->pipeline_layout) != VK_SUCCESS) {
        vkDestroyShaderModule(impl_->device, fs, nullptr);
        vkDestroyShaderModule(impl_->device, vs, nullptr);
        shutdown();
        return false;
    }

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp_state;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &blend;
    gp.layout = impl_->pipeline_layout;
    gp.renderPass = impl_->render_pass;
    gp.subpass = 0;
    if (vkCreateGraphicsPipelines(impl_->device, VK_NULL_HANDLE, 1, &gp, nullptr, &impl_->pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(impl_->device, fs, nullptr);
        vkDestroyShaderModule(impl_->device, vs, nullptr);
        shutdown();
        return false;
    }

    vkDestroyShaderModule(impl_->device, fs, nullptr);
    vkDestroyShaderModule(impl_->device, vs, nullptr);

    impl_->framebuffers.reserve(impl_->image_views.size());
    for (auto view : impl_->image_views) {
        VkImageView att[] = {view};
        VkFramebufferCreateInfo fbi{};
        fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbi.renderPass = impl_->render_pass;
        fbi.attachmentCount = 1;
        fbi.pAttachments = att;
        fbi.width = impl_->swap_extent.width;
        fbi.height = impl_->swap_extent.height;
        fbi.layers = 1;
        VkFramebuffer fb = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(impl_->device, &fbi, nullptr, &fb) != VK_SUCCESS) {
            shutdown();
            return false;
        }
        impl_->framebuffers.push_back(fb);
    }

    VkCommandPoolCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = *impl_->families.graphics;
    if (vkCreateCommandPool(impl_->device, &cpi, nullptr, &impl_->command_pool) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    impl_->command_buffers.resize(impl_->framebuffers.size());
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = impl_->command_pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = static_cast<std::uint32_t>(impl_->command_buffers.size());
    if (vkAllocateCommandBuffers(impl_->device, &cai, impl_->command_buffers.data()) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (vkCreateSemaphore(impl_->device, &si, nullptr, &impl_->sem_img[i]) != VK_SUCCESS
            || vkCreateSemaphore(impl_->device, &si, nullptr, &impl_->sem_render[i]) != VK_SUCCESS
            || vkCreateFence(impl_->device, &fi, nullptr, &impl_->fences[i]) != VK_SUCCESS) {
            shutdown();
            return false;
        }
    }

    if (!impl_->ensure_vertex_capacity(16384)) {
        shutdown();
        return false;
    }
    impl_->last_frame_time = glfwGetTime();

    initialized_ = true;
    core::log(core::LogLevel::Info, "Vulkan renderer restored and initialized.");
    return true;
#else
    static_cast<void>(width);
    static_cast<void>(height);
    static_cast<void>(title);
    core::log(core::LogLevel::Error, "Vulkan/GLFW not available at build time.");
    return false;
#endif
}

void VulkanRenderer::set_song(RenderSong song)
{
#if defined(OXIKARA_HAS_VULKAN) && defined(OXIKARA_HAS_GLFW)
    if (impl_ != nullptr) {
        impl_->song = std::move(song);
        impl_->first_note_hint = 0;
        impl_->build_key_layout();
    }
#else
    static_cast<void>(song);
#endif
}

void VulkanRenderer::start_playback()
{
#if defined(OXIKARA_HAS_VULKAN) && defined(OXIKARA_HAS_GLFW)
    if (impl_ != nullptr) {
        impl_->start_time = glfwGetTime();
        impl_->playback_started = true;
        impl_->playhead_sec = 0.0;
        impl_->paused = false;
        impl_->prev_space = false;
        impl_->prev_left = false;
        impl_->prev_right = false;
        impl_->prev_mouse_left = false;
        impl_->last_frame_time = impl_->start_time;
        impl_->fps_accum = 0.0;
        impl_->fps_frames = 0;
        impl_->fps_value = 0.0;
    }
#endif
}

void VulkanRenderer::shutdown()
{
#if defined(OXIKARA_HAS_VULKAN) && defined(OXIKARA_HAS_GLFW)
    if (impl_ == nullptr) {
        initialized_ = false;
        return;
    }

    if (impl_->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(impl_->device);
    }

    if (impl_->vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(impl_->device, impl_->vertex_buffer, nullptr);
        impl_->vertex_buffer = VK_NULL_HANDLE;
    }
    if (impl_->vertex_memory != VK_NULL_HANDLE) {
        vkFreeMemory(impl_->device, impl_->vertex_memory, nullptr);
        impl_->vertex_memory = VK_NULL_HANDLE;
    }

    impl_->destroy_swapchain_objects();

    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (impl_->sem_img[i] != VK_NULL_HANDLE) vkDestroySemaphore(impl_->device, impl_->sem_img[i], nullptr);
        if (impl_->sem_render[i] != VK_NULL_HANDLE) vkDestroySemaphore(impl_->device, impl_->sem_render[i], nullptr);
        if (impl_->fences[i] != VK_NULL_HANDLE) vkDestroyFence(impl_->device, impl_->fences[i], nullptr);
    }

    if (impl_->command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(impl_->device, impl_->command_pool, nullptr);
    }
    if (impl_->device != VK_NULL_HANDLE) {
        vkDestroyDevice(impl_->device, nullptr);
    }
    if (impl_->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(impl_->instance, impl_->surface, nullptr);
    }
    if (impl_->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(impl_->instance, nullptr);
    }
    if (impl_->window != nullptr) {
        glfwDestroyWindow(impl_->window);
    }

    glfwTerminate();
    delete impl_;
    impl_ = nullptr;
    initialized_ = false;
#endif
}

bool VulkanRenderer::draw_frame()
{
#if defined(OXIKARA_HAS_VULKAN) && defined(OXIKARA_HAS_GLFW)
    if (!initialized_ || impl_ == nullptr || impl_->window == nullptr) {
        return false;
    }

    glfwPollEvents();
    const double t_now = glfwGetTime();

    const bool space_down = glfwGetKey(impl_->window, GLFW_KEY_SPACE) == GLFW_PRESS;
    const bool left_down = glfwGetKey(impl_->window, GLFW_KEY_LEFT) == GLFW_PRESS;
    const bool right_down = glfwGetKey(impl_->window, GLFW_KEY_RIGHT) == GLFW_PRESS;

    if (space_down && !impl_->prev_space) {
        impl_->paused = !impl_->paused;
        impl_->last_frame_time = t_now;
    }
    if (left_down && !impl_->prev_left) {
        impl_->playhead_sec = std::max(0.0, impl_->playhead_sec - 5.0);
        impl_->last_frame_time = t_now;
    }
    if (right_down && !impl_->prev_right) {
        impl_->playhead_sec = std::min(impl_->song.duration_sec, impl_->playhead_sec + 5.0);
        impl_->last_frame_time = t_now;
    }
    impl_->prev_space = space_down;
    impl_->prev_left = left_down;
    impl_->prev_right = right_down;

    const double dt = t_now - impl_->last_frame_time;
    impl_->last_frame_time = t_now;
    if (!impl_->paused && dt > 0.0) {
        impl_->playhead_sec += dt;
        if (impl_->playhead_sec > impl_->song.duration_sec) {
            impl_->playhead_sec = impl_->song.duration_sec;
        }
    }

    const double play_now = impl_->now();

    if (dt > 0.0) {
        impl_->fps_accum += dt;
        ++impl_->fps_frames;
        if (impl_->fps_accum >= 0.25) {
            impl_->fps_value = static_cast<double>(impl_->fps_frames) / impl_->fps_accum;
            impl_->fps_accum = 0.0;
            impl_->fps_frames = 0;
        }
    }

    constexpr float piano_top = -0.62f;
    constexpr float white_bottom = -1.0f;
    constexpr float black_top = -0.86f;
    constexpr double horizon_sec = 4.0;
    constexpr std::size_t max_visible_notes = 25000;

    impl_->cpu_vertices.clear();
    impl_->hit_r.fill(0.0f);
    impl_->hit_g.fill(0.0f);
    impl_->hit_b.fill(0.0f);

    while (impl_->first_note_hint < impl_->song.notes.size() && impl_->song.notes[impl_->first_note_hint].end_sec < play_now - 0.01) {
        ++impl_->first_note_hint;
    }

    auto project_time = [&](const double sec) -> float {
        const double n = (sec - play_now) / horizon_sec;
        const double y = static_cast<double>(piano_top) + n * (1.0 - static_cast<double>(piano_top));
        return static_cast<float>(std::clamp(y, static_cast<double>(piano_top), 1.0));
    };

    std::size_t polyphony = 0;
    std::size_t drawn_notes = 0;

    for (std::size_t i = impl_->first_note_hint; i < impl_->song.notes.size(); ++i) {
        const auto& n = impl_->song.notes[i];
        if (n.start_sec > play_now + horizon_sec) {
            break;
        }
        if (n.end_sec <= play_now - 0.01) {
            continue;
        }
        if (drawn_notes >= max_visible_notes) {
            break;
        }

        const std::size_t note_idx = static_cast<std::size_t>(n.note);
        if (!impl_->key_visible[note_idx]) {
            continue;
        }
        ++drawn_notes;

        if (n.start_sec <= play_now && n.end_sec > play_now) {
            ++polyphony;
            impl_->hit_r[note_idx] = n.r;
            impl_->hit_g[note_idx] = n.g;
            impl_->hit_b[note_idx] = n.b;
        }

        float y0 = (n.start_sec <= play_now) ? piano_top : project_time(n.start_sec);
        const float y1 = project_time(n.end_sec);
        if (y1 <= piano_top + 0.001f || y1 <= y0 + 0.0005f) {
            continue;
        }

        const float x0 = impl_->key_left[note_idx] + 0.001f;
        const float x1 = impl_->key_right[note_idx] - 0.001f;
        if (x1 <= x0) {
            continue;
        }
        y0 = std::clamp(y0, piano_top, 1.0f);
        push_rect(impl_->cpu_vertices, x0, y0, x1, y1, n.r, n.g, n.b);
    }

    for (int note = static_cast<int>(impl_->song.min_note); note <= static_cast<int>(impl_->song.max_note); ++note) {
        if (!impl_->key_visible[static_cast<std::size_t>(note)] || is_black_key(note)) {
            continue;
        }
        const std::size_t idx = static_cast<std::size_t>(note);
        float kr = 0.96f;
        float kg = 0.96f;
        float kb = 0.96f;
        if (impl_->hit_r[idx] > 0.0f || impl_->hit_g[idx] > 0.0f || impl_->hit_b[idx] > 0.0f) {
            kr = impl_->hit_r[idx];
            kg = impl_->hit_g[idx];
            kb = impl_->hit_b[idx];
        }
        push_rect(impl_->cpu_vertices, impl_->key_left[idx], white_bottom, impl_->key_right[idx], piano_top, kr, kg, kb);
        push_rect(impl_->cpu_vertices, impl_->key_left[idx], piano_top - 0.0015f, impl_->key_right[idx], piano_top, 0.10f, 0.10f, 0.10f);
        push_rect(impl_->cpu_vertices, impl_->key_right[idx] - 0.0012f, white_bottom, impl_->key_right[idx], piano_top, 0.18f, 0.18f, 0.18f);
    }

    for (int note = static_cast<int>(impl_->song.min_note); note <= static_cast<int>(impl_->song.max_note); ++note) {
        if (!impl_->key_visible[static_cast<std::size_t>(note)] || !is_black_key(note)) {
            continue;
        }
        const std::size_t idx = static_cast<std::size_t>(note);
        float kr = 0.08f;
        float kg = 0.08f;
        float kb = 0.08f;
        if (impl_->hit_r[idx] > 0.0f || impl_->hit_g[idx] > 0.0f || impl_->hit_b[idx] > 0.0f) {
            kr = impl_->hit_r[idx];
            kg = impl_->hit_g[idx];
            kb = impl_->hit_b[idx];
        }
        push_rect(impl_->cpu_vertices, impl_->key_left[idx], black_top, impl_->key_right[idx], piano_top, kr, kg, kb);
        push_rect(impl_->cpu_vertices, impl_->key_left[idx], piano_top - 0.0015f, impl_->key_right[idx], piano_top, 0.02f, 0.02f, 0.02f);
    }

    constexpr float button_x0 = 0.72f;
    constexpr float button_x1 = 0.97f;
    constexpr float button_y0 = 0.88f;
    constexpr float button_y1 = 0.98f;
    const bool mouse_down = glfwGetMouseButton(impl_->window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (mouse_down && !impl_->prev_mouse_left) {
        double cx = 0.0;
        double cy = 0.0;
        glfwGetCursorPos(impl_->window, &cx, &cy);
        const double w = static_cast<double>(std::max(1u, impl_->swap_extent.width));
        const double h = static_cast<double>(std::max(1u, impl_->swap_extent.height));
        const float ndc_x = static_cast<float>(cx * 2.0 / w - 1.0);
        const float ndc_y = static_cast<float>(1.0 - (cy * 2.0 / h));
        if (ndc_x >= button_x0 && ndc_x <= button_x1 && ndc_y >= button_y0 && ndc_y <= button_y1) {
            impl_->paused = !impl_->paused;
            impl_->last_frame_time = t_now;
        }
    }
    impl_->prev_mouse_left = mouse_down;

    if (impl_->paused) {
        push_rect(impl_->cpu_vertices, button_x0, button_y0, button_x1, button_y1, 0.24f, 0.64f, 0.24f);
    } else {
        push_rect(impl_->cpu_vertices, button_x0, button_y0, button_x1, button_y1, 0.72f, 0.26f, 0.22f);
    }
    push_rect(impl_->cpu_vertices, button_x0, button_y1 - 0.003f, button_x1, button_y1, 0.06f, 0.06f, 0.06f);
    push_rect(impl_->cpu_vertices, button_x0, button_y0, button_x0 + 0.003f, button_y1, 0.06f, 0.06f, 0.06f);

    const float pixel_w = 2.0f / static_cast<float>(std::max(1u, impl_->swap_extent.width));
    const float pixel_h = 2.0f / static_cast<float>(std::max(1u, impl_->swap_extent.height));
    const float sx = pixel_w * 3.0f;
    const float sy = pixel_h * 4.0f;

    {
        std::ostringstream s;
        s << "TOTAL NOTES: " << impl_->song.note_count;
        push_text(impl_->cpu_vertices, -0.985f, 0.975f, sx, sy, 1.0f, 1.0f, 1.0f, 1.0f, s.str());
    }
    {
        std::ostringstream s;
        s << "POLYPHONY: " << polyphony;
        push_text(impl_->cpu_vertices, -0.985f, 0.91f, sx, sy, 1.0f, 0.35f, 0.95f, 0.35f, s.str());
    }
    {
        std::ostringstream s;
        s.setf(std::ios::fixed);
        s.precision(1);
        s << "FPS: " << impl_->fps_value;
        push_text(impl_->cpu_vertices, -0.985f, 0.845f, sx, sy, 1.0f, 0.98f, 0.86f, 0.22f, s.str());
    }
    if (impl_->paused) {
        push_text(impl_->cpu_vertices, 0.755f, 0.955f, sx, sy, 1.0f, 1.0f, 1.0f, 1.0f, "RESUME");
    } else {
        push_text(impl_->cpu_vertices, 0.77f, 0.955f, sx, sy, 1.0f, 1.0f, 1.0f, 1.0f, "PAUSE");
    }

    if (!impl_->ensure_vertex_capacity(impl_->cpu_vertices.size())) {
        return false;
    }
    if (!impl_->cpu_vertices.empty()) {
        void* mapped = nullptr;
        if (vkMapMemory(
                impl_->device,
                impl_->vertex_memory,
                0,
                static_cast<VkDeviceSize>(impl_->cpu_vertices.size() * sizeof(Vertex)),
                0,
                &mapped)
            != VK_SUCCESS) {
            return false;
        }
        std::memcpy(mapped, impl_->cpu_vertices.data(), impl_->cpu_vertices.size() * sizeof(Vertex));
        vkUnmapMemory(impl_->device, impl_->vertex_memory);
    }

    vkWaitForFences(impl_->device, 1, &impl_->fences[impl_->frame], VK_TRUE, UINT64_MAX);

    std::uint32_t image_idx = 0;
    const VkResult acq = vkAcquireNextImageKHR(
        impl_->device,
        impl_->swapchain,
        UINT64_MAX,
        impl_->sem_img[impl_->frame],
        VK_NULL_HANDLE,
        &image_idx);
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        return false;
    }

    vkResetFences(impl_->device, 1, &impl_->fences[impl_->frame]);
    vkResetCommandBuffer(impl_->command_buffers[image_idx], 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(impl_->command_buffers[image_idx], &bi);

    VkClearValue clear{};
    clear.color = {{0.02f, 0.03f, 0.07f, 1.0f}};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = impl_->render_pass;
    rp.framebuffer = impl_->framebuffers[image_idx];
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = impl_->swap_extent;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;

    vkCmdBeginRenderPass(impl_->command_buffers[image_idx], &rp, VK_SUBPASS_CONTENTS_INLINE);
    if (!impl_->cpu_vertices.empty()) {
        vkCmdBindPipeline(impl_->command_buffers[image_idx], VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->pipeline);
        const VkBuffer vbs[] = {impl_->vertex_buffer};
        const VkDeviceSize offs[] = {0};
        vkCmdBindVertexBuffers(impl_->command_buffers[image_idx], 0, 1, vbs, offs);
        vkCmdDraw(impl_->command_buffers[image_idx], static_cast<std::uint32_t>(impl_->cpu_vertices.size()), 1, 0, 0);
    }
    vkCmdEndRenderPass(impl_->command_buffers[image_idx]);
    vkEndCommandBuffer(impl_->command_buffers[image_idx]);

    VkSemaphore wait_sem[] = {impl_->sem_img[impl_->frame]};
    VkPipelineStageFlags wait_stage[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signal_sem[] = {impl_->sem_render[impl_->frame]};

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = wait_sem;
    si.pWaitDstStageMask = wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &impl_->command_buffers[image_idx];
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = signal_sem;
    if (vkQueueSubmit(impl_->graphics_queue, 1, &si, impl_->fences[impl_->frame]) != VK_SUCCESS) {
        return false;
    }

    VkSwapchainKHR swaps[] = {impl_->swapchain};
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = signal_sem;
    pi.swapchainCount = 1;
    pi.pSwapchains = swaps;
    pi.pImageIndices = &image_idx;
    if (vkQueuePresentKHR(impl_->present_queue, &pi) != VK_SUCCESS) {
        return false;
    }

    impl_->frame = (impl_->frame + 1) % kFramesInFlight;
    return true;
#else
    return false;
#endif
}

bool VulkanRenderer::should_close() const
{
#if defined(OXIKARA_HAS_VULKAN) && defined(OXIKARA_HAS_GLFW)
    if (!initialized_ || impl_ == nullptr || impl_->window == nullptr) {
        return true;
    }
    return glfwWindowShouldClose(impl_->window) == GLFW_TRUE;
#else
    return true;
#endif
}

double VulkanRenderer::playback_time() const
{
#if defined(OXIKARA_HAS_VULKAN) && defined(OXIKARA_HAS_GLFW)
    if (impl_ == nullptr) {
        return 0.0;
    }
    return impl_->now();
#else
    return 0.0;
#endif
}

bool VulkanRenderer::playback_finished() const
{
#if defined(OXIKARA_HAS_VULKAN) && defined(OXIKARA_HAS_GLFW)
    if (impl_ == nullptr || !impl_->playback_started) {
        return true;
    }
    return impl_->now() > (impl_->song.duration_sec + 1.0);
#else
    return true;
#endif
}

} // namespace oxikara::render
