#include "pba/gfx/vk_mvp.hpp"

//
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
//
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

//
#include <vulkan/vulkan.h>

// If GLFW has been included earlier via PCH without Vulkan enabled,
// we still want Vulkan helper declarations available.
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

// Ensure glfwCreateWindowSurface is declared even if the GLFW header was
// pulled in earlier without Vulkan helpers enabled.
extern "C" GLFWAPI VkResult glfwCreateWindowSurface(
    VkInstance instance,
    GLFWwindow *window,
    const VkAllocationCallbacks *allocator,
    VkSurfaceKHR *surface);

// VMA + ImGui backends may trigger warnings under your -Werror set.
// Suppress warnings for these third-party headers in this TU.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wcast-align"
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wfloat-conversion"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wundef"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace ds_pba {

namespace {

constexpr const char *k_portability_subset_ext =
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
#else
    "VK_KHR_portability_subset";
#endif

constexpr const char *k_portability_enumeration_ext =
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
#else
    "VK_KHR_portability_enumeration";
#endif

[[nodiscard]] std::string vk_result_string(VkResult r) {
    return std::to_string(static_cast<int>(r));
}

void vk_check(VkResult r, const char *what) {
    if (r != VK_SUCCESS) {
        throw std::runtime_error(std::string{what} + " (VkResult=" + vk_result_string(r) + ")");
    }
}

#ifndef NDEBUG
static constexpr bool k_enable_validation = true;
#else
static constexpr bool k_enable_validation = false;
#endif

static constexpr const char *k_validation_layer = "VK_LAYER_KHRONOS_validation";

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
    void *) {
    if (!callback_data || !callback_data->pMessage) {
        return VK_FALSE;
    }

    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::fprintf(stderr, "[Vulkan] %s\n", callback_data->pMessage);
    }
    return VK_FALSE;
}

[[maybe_unused]] [[nodiscard]] VkDebugUtilsMessengerEXT create_debug_messenger(VkInstance instance) {
    if (!k_enable_validation) {
        return VK_NULL_HANDLE;
    }

    const auto vkCreateDebugUtilsMessengerEXT_fn =
        reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));

    if (!vkCreateDebugUtilsMessengerEXT_fn) {
        return VK_NULL_HANDLE;
    }

    VkDebugUtilsMessengerCreateInfoEXT ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debug_callback;
    ci.pUserData = nullptr;

    VkDebugUtilsMessengerEXT messenger{VK_NULL_HANDLE};
    vk_check(vkCreateDebugUtilsMessengerEXT_fn(instance, &ci, nullptr, &messenger),
             "vkCreateDebugUtilsMessengerEXT");
    return messenger;
}

void destroy_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger) {
    if (!messenger) {
        return;
    }

    const auto vkDestroyDebugUtilsMessengerEXT_fn =
        reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));

    if (vkDestroyDebugUtilsMessengerEXT_fn) {
        vkDestroyDebugUtilsMessengerEXT_fn(instance, messenger, nullptr);
    }
}

[[maybe_unused]] [[nodiscard]] bool has_layer(const std::vector<VkLayerProperties> &layers, const char *name) {
    for (const VkLayerProperties &lp : layers) {
        if (std::strcmp(lp.layerName, name) == 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_extension(const std::vector<VkExtensionProperties> &exts, const char *name) {
    for (const VkExtensionProperties &ep : exts) {
        if (std::strcmp(ep.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::vector<std::uint32_t> read_spirv(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open SPIR-V file: " + path.string());
    }

    const std::streamsize size_bytes = file.tellg();
    if (size_bytes <= 0) {
        throw std::runtime_error("Empty SPIR-V file: " + path.string());
    }
    if ((size_bytes % 4) != 0) {
        throw std::runtime_error("SPIR-V file size not multiple of 4: " + path.string());
    }

    const std::size_t words = static_cast<std::size_t>(size_bytes) / 4u;
    std::vector<std::uint32_t> data(words);

    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char *>(data.data()), size_bytes);
    if (!file) {
        throw std::runtime_error("Failed to read SPIR-V file: " + path.string());
    }

    return data;
}

[[nodiscard]] VkShaderModule create_shader_module(VkDevice device, const std::filesystem::path &spv) {
    const std::vector<std::uint32_t> code = read_spirv(spv);

    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.pNext = nullptr;
    ci.flags = 0;
    ci.codeSize = code.size() * sizeof(std::uint32_t);
    ci.pCode = code.data();

    VkShaderModule mod{VK_NULL_HANDLE};
    vk_check(vkCreateShaderModule(device, &ci, nullptr, &mod), "vkCreateShaderModule");
    return mod;
}

[[nodiscard]] VkFormat pick_depth_stencil_format(VkPhysicalDevice phys) {
    const std::array<VkFormat, 3> candidates = {
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT};

    for (VkFormat fmt : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(phys, fmt, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0u) {
            return fmt;
        }
    }

    throw std::runtime_error("No suitable depth/stencil format found");
}

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
};

static constexpr std::array<Vertex, 36> k_cube_vertices = {
    // +X
    Vertex{{+0.5f, -0.5f, -0.5f}, {1.0f, 0.2f, 0.2f}},
    Vertex{{+0.5f, +0.5f, -0.5f}, {1.0f, 0.2f, 0.2f}},
    Vertex{{+0.5f, +0.5f, +0.5f}, {1.0f, 0.2f, 0.2f}},
    Vertex{{+0.5f, -0.5f, -0.5f}, {1.0f, 0.2f, 0.2f}},
    Vertex{{+0.5f, +0.5f, +0.5f}, {1.0f, 0.2f, 0.2f}},
    Vertex{{+0.5f, -0.5f, +0.5f}, {1.0f, 0.2f, 0.2f}},

    // -X
    Vertex{{-0.5f, -0.5f, +0.5f}, {0.2f, 1.0f, 0.2f}},
    Vertex{{-0.5f, +0.5f, +0.5f}, {0.2f, 1.0f, 0.2f}},
    Vertex{{-0.5f, +0.5f, -0.5f}, {0.2f, 1.0f, 0.2f}},
    Vertex{{-0.5f, -0.5f, +0.5f}, {0.2f, 1.0f, 0.2f}},
    Vertex{{-0.5f, +0.5f, -0.5f}, {0.2f, 1.0f, 0.2f}},
    Vertex{{-0.5f, -0.5f, -0.5f}, {0.2f, 1.0f, 0.2f}},

    // +Y
    Vertex{{-0.5f, +0.5f, -0.5f}, {0.2f, 0.2f, 1.0f}},
    Vertex{{-0.5f, +0.5f, +0.5f}, {0.2f, 0.2f, 1.0f}},
    Vertex{{+0.5f, +0.5f, +0.5f}, {0.2f, 0.2f, 1.0f}},
    Vertex{{-0.5f, +0.5f, -0.5f}, {0.2f, 0.2f, 1.0f}},
    Vertex{{+0.5f, +0.5f, +0.5f}, {0.2f, 0.2f, 1.0f}},
    Vertex{{+0.5f, +0.5f, -0.5f}, {0.2f, 0.2f, 1.0f}},

    // -Y
    Vertex{{-0.5f, -0.5f, +0.5f}, {1.0f, 1.0f, 0.2f}},
    Vertex{{-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 0.2f}},
    Vertex{{+0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 0.2f}},
    Vertex{{-0.5f, -0.5f, +0.5f}, {1.0f, 1.0f, 0.2f}},
    Vertex{{+0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 0.2f}},
    Vertex{{+0.5f, -0.5f, +0.5f}, {1.0f, 1.0f, 0.2f}},

    // +Z
    Vertex{{-0.5f, -0.5f, +0.5f}, {1.0f, 0.2f, 1.0f}},
    Vertex{{+0.5f, -0.5f, +0.5f}, {1.0f, 0.2f, 1.0f}},
    Vertex{{+0.5f, +0.5f, +0.5f}, {1.0f, 0.2f, 1.0f}},
    Vertex{{-0.5f, -0.5f, +0.5f}, {1.0f, 0.2f, 1.0f}},
    Vertex{{+0.5f, +0.5f, +0.5f}, {1.0f, 0.2f, 1.0f}},
    Vertex{{-0.5f, +0.5f, +0.5f}, {1.0f, 0.2f, 1.0f}},

    // -Z
    Vertex{{+0.5f, -0.5f, -0.5f}, {0.2f, 1.0f, 1.0f}},
    Vertex{{-0.5f, -0.5f, -0.5f}, {0.2f, 1.0f, 1.0f}},
    Vertex{{-0.5f, +0.5f, -0.5f}, {0.2f, 1.0f, 1.0f}},
    Vertex{{+0.5f, -0.5f, -0.5f}, {0.2f, 1.0f, 1.0f}},
    Vertex{{-0.5f, +0.5f, -0.5f}, {0.2f, 1.0f, 1.0f}},
    Vertex{{+0.5f, +0.5f, -0.5f}, {0.2f, 1.0f, 1.0f}},
};

template <class T>
[[nodiscard]] ImTextureID to_imgui_texture_id(T handle) noexcept {
    // Works with either ImTextureID = void* or ImTextureID = ImU64 (default in newer ImGui).
    if constexpr (std::is_pointer_v<ImTextureID>) {
        return reinterpret_cast<ImTextureID>(handle);
    } else {
        return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(handle));
    }
}

} // namespace

struct VulkanMvp::Impl final {
    static constexpr std::uint32_t k_frames_in_flight = 2;

    GLFWwindow *window{nullptr};
    bool framebuffer_resized{false};

    VkInstance instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debug_messenger{VK_NULL_HANDLE};
    VkSurfaceKHR surface{VK_NULL_HANDLE};

    VkPhysicalDevice phys{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue graphics_queue{VK_NULL_HANDLE};
    std::uint32_t graphics_queue_family{0};

    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    VkFormat swapchain_format{VK_FORMAT_UNDEFINED};
    VkExtent2D swapchain_extent{};
    std::vector<VkImage> swapchain_images{};
    std::vector<VkImageView> swapchain_views{};
    VkRenderPass swapchain_render_pass{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> swapchain_framebuffers{};

    VkCommandPool cmd_pool{VK_NULL_HANDLE};

    struct Frame {
        VkCommandBuffer cmd{VK_NULL_HANDLE};
        VkSemaphore image_acquired{VK_NULL_HANDLE};
        VkSemaphore render_complete{VK_NULL_HANDLE};
        VkFence in_flight{VK_NULL_HANDLE};
    };

    std::array<Frame, k_frames_in_flight> frames{};
    std::uint32_t frame_index{0};

    VmaAllocator allocator{VK_NULL_HANDLE};

    // ImGui
    VkDescriptorPool imgui_desc_pool{VK_NULL_HANDLE};

    // Offscreen (per frame-in-flight)
    VkRenderPass offscreen_render_pass{VK_NULL_HANDLE};
    VkSampler offscreen_sampler{VK_NULL_HANDLE};
    VkFormat offscreen_color_format{VK_FORMAT_R8G8B8A8_UNORM};
    VkFormat offscreen_depth_format{VK_FORMAT_UNDEFINED};

    struct OffscreenFrame {
        VkImage color_image{VK_NULL_HANDLE};
        VmaAllocation color_alloc{VK_NULL_HANDLE};
        VkImageView color_view{VK_NULL_HANDLE};

        VkImage depth_image{VK_NULL_HANDLE};
        VmaAllocation depth_alloc{VK_NULL_HANDLE};
        VkImageView depth_view{VK_NULL_HANDLE};

        VkFramebuffer framebuffer{VK_NULL_HANDLE};

        VkDescriptorSet imgui_texture_set{VK_NULL_HANDLE};

        std::uint32_t width{1};
        std::uint32_t height{1};
    };

    std::array<OffscreenFrame, k_frames_in_flight> offscreen{};

    // Cube pipeline + vertex buffer (rendered into offscreen)
    VkPipelineLayout cube_pipeline_layout{VK_NULL_HANDLE};
    VkPipeline cube_pipeline{VK_NULL_HANDLE};

    VkBuffer cube_vbo{VK_NULL_HANDLE};
    VmaAllocation cube_vbo_alloc{VK_NULL_HANDLE};

    std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};

    static void imgui_check_vk_result(VkResult err) {
        if (err != VK_SUCCESS) {
            std::fprintf(stderr, "[ImGui Vulkan] VkResult=%d\n", static_cast<int>(err));
        }
    }

    static void framebuffer_resize_callback(GLFWwindow *wnd, int, int) {
        void *ptr = glfwGetWindowUserPointer(wnd);
        if (!ptr) {
            return;
        }
        auto &self = *static_cast<Impl *>(ptr);
        self.framebuffer_resized = true;
    }

    void init_window() {
        if (glfwInit() == GLFW_FALSE) {
            throw std::runtime_error("glfwInit failed");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        window = glfwCreateWindow(1600, 900, "Vulkan MVP (Spinning Cube)", nullptr, nullptr);
        if (!window) {
            throw std::runtime_error("glfwCreateWindow failed");
        }

        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebuffer_resize_callback);
    }

    [[nodiscard]] std::vector<const char *> get_instance_extensions() const {
        std::uint32_t glfw_count = 0;
        const char **glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_count);
        if (!glfw_exts || glfw_count == 0) {
            throw std::runtime_error("glfwGetRequiredInstanceExtensions returned none");
        }

        std::vector<const char *> exts;
        exts.reserve(static_cast<std::size_t>(glfw_count) + 4u);
        for (std::uint32_t i = 0; i < glfw_count; ++i) {
            exts.push_back(glfw_exts[i]);
        }

        if constexpr (k_enable_validation) {
            exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

#if defined(__APPLE__)
        // MoltenVK portability enumeration
        exts.push_back(k_portability_enumeration_ext);
#endif

        return exts;
    }

    void create_instance() {
        std::uint32_t layer_count = 0;
        vk_check(vkEnumerateInstanceLayerProperties(&layer_count, nullptr),
                 "vkEnumerateInstanceLayerProperties(count)");
        std::vector<VkLayerProperties> layers(layer_count);
        vk_check(vkEnumerateInstanceLayerProperties(&layer_count, layers.data()),
                 "vkEnumerateInstanceLayerProperties(list)");

        std::vector<const char *> enabled_layers;
        if constexpr (k_enable_validation) {
            if (!has_layer(layers, k_validation_layer)) {
                throw std::runtime_error("Validation layer not available: VK_LAYER_KHRONOS_validation");
            }
            enabled_layers.push_back(k_validation_layer);
        }

        const std::vector<const char *> exts = get_instance_extensions();

        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pNext = nullptr;
        app.pApplicationName = "pba_vulkan_mvp";
        app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app.pEngineName = "pba";
        app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pNext = nullptr;
        ci.flags = 0;
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
        ci.ppEnabledExtensionNames = exts.data();
        ci.enabledLayerCount = static_cast<std::uint32_t>(enabled_layers.size());
        ci.ppEnabledLayerNames = enabled_layers.empty() ? nullptr : enabled_layers.data();

#if defined(__APPLE__)
        // Required when VK_KHR_portability_enumeration is enabled
        ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

        vk_check(vkCreateInstance(&ci, nullptr, &instance), "vkCreateInstance");

        if constexpr (k_enable_validation) {
            debug_messenger = create_debug_messenger(instance);
        }
    }

    void create_surface() {
        VkSurfaceKHR s{VK_NULL_HANDLE};
        vk_check(glfwCreateWindowSurface(instance, window, nullptr, &s),
                 "glfwCreateWindowSurface");
        surface = s;
    }

    struct DeviceChoice {
        VkPhysicalDevice dev{VK_NULL_HANDLE};
        std::uint32_t gfx_qfam{0};
    };

    [[nodiscard]] DeviceChoice pick_physical_device() {
        std::uint32_t dev_count = 0;
        vk_check(vkEnumeratePhysicalDevices(instance, &dev_count, nullptr),
                 "vkEnumeratePhysicalDevices(count)");
        if (dev_count == 0) {
            throw std::runtime_error("No Vulkan physical devices found");
        }

        std::vector<VkPhysicalDevice> devs(dev_count);
        vk_check(vkEnumeratePhysicalDevices(instance, &dev_count, devs.data()),
                 "vkEnumeratePhysicalDevices(list)");

        std::optional<DeviceChoice> best{};

        for (VkPhysicalDevice d : devs) {
            std::uint32_t ext_count = 0;
            vk_check(vkEnumerateDeviceExtensionProperties(d, nullptr, &ext_count, nullptr),
                     "vkEnumerateDeviceExtensionProperties(count)");
            std::vector<VkExtensionProperties> exts(ext_count);
            vk_check(vkEnumerateDeviceExtensionProperties(d, nullptr, &ext_count, exts.data()),
                     "vkEnumerateDeviceExtensionProperties(list)");

            const bool has_swapchain = has_extension(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            if (!has_swapchain) {
                continue;
            }

            // Find a queue family that supports graphics + present
            std::uint32_t qf_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(d, &qf_count, nullptr);
            std::vector<VkQueueFamilyProperties> qfs(qf_count);
            vkGetPhysicalDeviceQueueFamilyProperties(d, &qf_count, qfs.data());

            for (std::uint32_t i = 0; i < qf_count; ++i) {
                const bool gfx = (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u;

                VkBool32 present = VK_FALSE;
                vk_check(vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &present),
                         "vkGetPhysicalDeviceSurfaceSupportKHR");

                if (gfx && (present == VK_TRUE)) {
                    VkPhysicalDeviceProperties props{};
                    vkGetPhysicalDeviceProperties(d, &props);

                    if (!best.has_value()) {
                        best = DeviceChoice{d, i};
                    } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                        best = DeviceChoice{d, i};
                    }
                    break;
                }
            }
        }

        if (!best.has_value()) {
            throw std::runtime_error("No suitable Vulkan physical device found (need graphics+present and swapchain)");
        }

        return *best;
    }

    void create_device() {
        const DeviceChoice choice = pick_physical_device();
        phys = choice.dev;
        graphics_queue_family = choice.gfx_qfam;

        std::uint32_t ext_count = 0;
        vk_check(vkEnumerateDeviceExtensionProperties(phys, nullptr, &ext_count, nullptr),
                 "vkEnumerateDeviceExtensionProperties(count)");
        std::vector<VkExtensionProperties> exts(ext_count);
        vk_check(vkEnumerateDeviceExtensionProperties(phys, nullptr, &ext_count, exts.data()),
                 "vkEnumerateDeviceExtensionProperties(list)");

        std::vector<const char *> dev_exts;
        dev_exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

#if defined(__APPLE__)
        // Often required on MoltenVK
        if (has_extension(exts, k_portability_subset_ext)) {
            dev_exts.push_back(k_portability_subset_ext);
        }
#endif

        const float prio = 1.0f;
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.pNext = nullptr;
        qci.flags = 0;
        qci.queueFamilyIndex = graphics_queue_family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &prio;

        std::vector<const char *> layers;
        if constexpr (k_enable_validation) {
            layers.push_back(k_validation_layer);
        }

        VkPhysicalDeviceFeatures feats{};
        feats.samplerAnisotropy = VK_TRUE;

        VkDeviceCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.pNext = nullptr;
        dci.flags = 0;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = static_cast<std::uint32_t>(dev_exts.size());
        dci.ppEnabledExtensionNames = dev_exts.data();
        dci.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
        dci.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
        dci.pEnabledFeatures = &feats;

        vk_check(vkCreateDevice(phys, &dci, nullptr, &device), "vkCreateDevice");

        vkGetDeviceQueue(device, graphics_queue_family, 0, &graphics_queue);
        if (!graphics_queue) {
            throw std::runtime_error("vkGetDeviceQueue returned null");
        }
    }

    void create_allocator() {
        VmaAllocatorCreateInfo ci{};
        ci.flags = 0;
        ci.physicalDevice = phys;
        ci.device = device;
        ci.preferredLargeHeapBlockSize = 0;
        ci.pAllocationCallbacks = nullptr;
        ci.pDeviceMemoryCallbacks = nullptr;
        ci.pHeapSizeLimit = nullptr;
        ci.pVulkanFunctions = nullptr;
        ci.instance = instance;
        ci.vulkanApiVersion = VK_API_VERSION_1_2;
        ci.pTypeExternalMemoryHandleTypes = nullptr;

        vk_check(vmaCreateAllocator(&ci, &allocator), "vmaCreateAllocator");
    }

    void create_command_pool() {
        VkCommandPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.pNext = nullptr;
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = graphics_queue_family;
        vk_check(vkCreateCommandPool(device, &ci, nullptr, &cmd_pool), "vkCreateCommandPool");
    }

    struct SwapchainSupport {
        VkSurfaceCapabilitiesKHR caps{};
        std::vector<VkSurfaceFormatKHR> formats{};
        std::vector<VkPresentModeKHR> present_modes{};
    };

    [[nodiscard]] SwapchainSupport query_swapchain_support() const {
        SwapchainSupport s{};

        vk_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &s.caps),
                 "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        std::uint32_t fmt_count = 0;
        vk_check(vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmt_count, nullptr),
                 "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
        s.formats.resize(fmt_count);
        vk_check(vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmt_count, s.formats.data()),
                 "vkGetPhysicalDeviceSurfaceFormatsKHR(list)");

        std::uint32_t pm_count = 0;
        vk_check(vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pm_count, nullptr),
                 "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
        s.present_modes.resize(pm_count);
        vk_check(vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pm_count, s.present_modes.data()),
                 "vkGetPhysicalDeviceSurfacePresentModesKHR(list)");

        return s;
    }

    [[nodiscard]] VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR> &formats) const {
        for (const VkSurfaceFormatKHR &f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return f;
            }
        }
        return formats.at(0);
    }

    [[nodiscard]] VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR> &modes) const {
        for (VkPresentModeKHR m : modes) {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
                return m;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    [[nodiscard]] VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR &caps) const {
        if (caps.currentExtent.width != UINT32_MAX) {
            return caps.currentExtent;
        }

        int fb_w = 0;
        int fb_h = 0;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);

        const int safe_w = std::max(1, fb_w);
        const int safe_h = std::max(1, fb_h);

        VkExtent2D e{};
        e.width = static_cast<std::uint32_t>(safe_w);
        e.height = static_cast<std::uint32_t>(safe_h);

        e.width = std::clamp(e.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        e.height = std::clamp(e.height, caps.minImageExtent.height, caps.maxImageExtent.height);

        return e;
    }

    void destroy_swapchain_resources() {
        for (VkFramebuffer fb : swapchain_framebuffers) {
            vkDestroyFramebuffer(device, fb, nullptr);
        }
        swapchain_framebuffers.clear();

        if (swapchain_render_pass) {
            vkDestroyRenderPass(device, swapchain_render_pass, nullptr);
            swapchain_render_pass = VK_NULL_HANDLE;
        }

        for (VkImageView iv : swapchain_views) {
            vkDestroyImageView(device, iv, nullptr);
        }
        swapchain_views.clear();

        if (swapchain) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
        swapchain_images.clear();
        swapchain_format = VK_FORMAT_UNDEFINED;
        swapchain_extent = VkExtent2D{};
    }

    void create_swapchain() {
        const SwapchainSupport s = query_swapchain_support();

        const VkSurfaceFormatKHR sf = choose_surface_format(s.formats);
        const VkPresentModeKHR pm = choose_present_mode(s.present_modes);
        const VkExtent2D extent = choose_extent(s.caps);

        std::uint32_t image_count = s.caps.minImageCount + 1u;
        if (s.caps.maxImageCount > 0u && image_count > s.caps.maxImageCount) {
            image_count = s.caps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.pNext = nullptr;
        ci.flags = 0;
        ci.surface = surface;
        ci.minImageCount = image_count;
        ci.imageFormat = sf.format;
        ci.imageColorSpace = sf.colorSpace;
        ci.imageExtent = extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.queueFamilyIndexCount = 0;
        ci.pQueueFamilyIndices = nullptr;
        ci.preTransform = s.caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = pm;
        ci.clipped = VK_TRUE;
        ci.oldSwapchain = VK_NULL_HANDLE;

        vk_check(vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain), "vkCreateSwapchainKHR");

        std::uint32_t out_count = 0;
        vk_check(vkGetSwapchainImagesKHR(device, swapchain, &out_count, nullptr),
                 "vkGetSwapchainImagesKHR(count)");
        swapchain_images.resize(out_count);
        vk_check(vkGetSwapchainImagesKHR(device, swapchain, &out_count, swapchain_images.data()),
                 "vkGetSwapchainImagesKHR(list)");

        swapchain_format = sf.format;
        swapchain_extent = extent;

        // Image views
        swapchain_views.resize(swapchain_images.size());
        for (std::size_t i = 0; i < swapchain_images.size(); ++i) {
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.pNext = nullptr;
            vi.flags = 0;
            vi.image = swapchain_images[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = swapchain_format;
            vi.components = VkComponentMapping{};
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.baseMipLevel = 0;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.baseArrayLayer = 0;
            vi.subresourceRange.layerCount = 1;

            vk_check(vkCreateImageView(device, &vi, nullptr, &swapchain_views[i]),
                     "vkCreateImageView(swapchain)");
        }

        // Render pass (swapchain)
        VkAttachmentDescription color{};
        color.flags = 0;
        color.format = swapchain_format;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_ref{};
        color_ref.attachment = 0;
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub{};
        sub.flags = 0;
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.inputAttachmentCount = 0;
        sub.pInputAttachments = nullptr;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &color_ref;
        sub.pResolveAttachments = nullptr;
        sub.pDepthStencilAttachment = nullptr;
        sub.preserveAttachmentCount = 0;
        sub.pPreserveAttachments = nullptr;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep.dependencyFlags = 0;

        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.pNext = nullptr;
        rp.flags = 0;
        rp.attachmentCount = 1;
        rp.pAttachments = &color;
        rp.subpassCount = 1;
        rp.pSubpasses = &sub;
        rp.dependencyCount = 1;
        rp.pDependencies = &dep;

        vk_check(vkCreateRenderPass(device, &rp, nullptr, &swapchain_render_pass),
                 "vkCreateRenderPass(swapchain)");

        // Framebuffers
        swapchain_framebuffers.resize(swapchain_views.size());
        for (std::size_t i = 0; i < swapchain_views.size(); ++i) {
            VkImageView attachments[1] = {swapchain_views[i]};

            VkFramebufferCreateInfo fb{};
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.pNext = nullptr;
            fb.flags = 0;
            fb.renderPass = swapchain_render_pass;
            fb.attachmentCount = 1;
            fb.pAttachments = attachments;
            fb.width = swapchain_extent.width;
            fb.height = swapchain_extent.height;
            fb.layers = 1;

            vk_check(vkCreateFramebuffer(device, &fb, nullptr, &swapchain_framebuffers[i]),
                     "vkCreateFramebuffer(swapchain)");
        }
    }

    void recreate_swapchain() {
        int fb_w = 0;
        int fb_h = 0;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        while (fb_w == 0 || fb_h == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(window, &fb_w, &fb_h);
        }

        vkDeviceWaitIdle(device);

        destroy_swapchain_resources();
        create_swapchain();

        ImGui_ImplVulkan_SetMinImageCount(static_cast<std::uint32_t>(swapchain_images.size()));

        framebuffer_resized = false;
    }

    void create_sync_and_cmd_buffers() {
        // Allocate command buffers (one per frame-in-flight)
        std::vector<VkCommandBuffer> cmds(static_cast<std::size_t>(k_frames_in_flight), VK_NULL_HANDLE);

        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.pNext = nullptr;
        ai.commandPool = cmd_pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = k_frames_in_flight;

        vk_check(vkAllocateCommandBuffers(device, &ai, cmds.data()),
                 "vkAllocateCommandBuffers");

        for (std::uint32_t i = 0; i < k_frames_in_flight; ++i) {
            frames[i].cmd = cmds[i];
        }

        // Semaphores + fences
        VkSemaphoreCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        sci.pNext = nullptr;
        sci.flags = 0;

        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.pNext = nullptr;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (std::uint32_t i = 0; i < k_frames_in_flight; ++i) {
            vk_check(vkCreateSemaphore(device, &sci, nullptr, &frames[i].image_acquired),
                     "vkCreateSemaphore(image_acquired)");
            vk_check(vkCreateSemaphore(device, &sci, nullptr, &frames[i].render_complete),
                     "vkCreateSemaphore(render_complete)");
            vk_check(vkCreateFence(device, &fci, nullptr, &frames[i].in_flight),
                     "vkCreateFence(in_flight)");
        }
    }

    void create_imgui_descriptor_pool() {
        const std::array<VkDescriptorPoolSize, 11> pool_sizes = {
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
        };

        VkDescriptorPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.pNext = nullptr;
        ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        ci.maxSets = 1000u * static_cast<std::uint32_t>(pool_sizes.size());
        ci.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
        ci.pPoolSizes = pool_sizes.data();

        vk_check(vkCreateDescriptorPool(device, &ci, nullptr, &imgui_desc_pool),
                 "vkCreateDescriptorPool(imgui)");
    }

    void init_imgui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        ImGui::StyleColorsDark();

        if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
            throw std::runtime_error("ImGui_ImplGlfw_InitForVulkan failed");
        }

        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.ApiVersion = VK_API_VERSION_1_2;
        init_info.Instance = instance;
        init_info.PhysicalDevice = phys;
        init_info.Device = device;
        init_info.QueueFamily = graphics_queue_family;
        init_info.Queue = graphics_queue;
        init_info.DescriptorPool = imgui_desc_pool;
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = imgui_check_vk_result;
        init_info.MinImageCount = static_cast<std::uint32_t>(swapchain_images.size());
        init_info.ImageCount = static_cast<std::uint32_t>(swapchain_images.size());
        init_info.DescriptorPoolSize = 0;
        init_info.MinAllocationSize = 0;

        // Newer ImGui Vulkan backend: pipeline settings live in PipelineInfoMain.
        init_info.PipelineInfoMain.RenderPass = swapchain_render_pass;
        init_info.PipelineInfoMain.Subpass = 0;
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo = VkPipelineRenderingCreateInfoKHR{};
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;

        // Not using multi-viewport, but keep it consistent.
        init_info.PipelineInfoForViewports = init_info.PipelineInfoMain;

        if (!ImGui_ImplVulkan_Init(&init_info)) {
            throw std::runtime_error("ImGui_ImplVulkan_Init failed");
        }

        // NOTE: Explicit font upload helpers were removed from the backend.
        // Fonts and texture updates are handled internally during rendering.
    }

    void destroy_imgui() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        if (imgui_desc_pool) {
            vkDestroyDescriptorPool(device, imgui_desc_pool, nullptr);
            imgui_desc_pool = VK_NULL_HANDLE;
        }
    }

    void create_offscreen_render_pass_and_sampler() {
        offscreen_depth_format = pick_depth_stencil_format(phys);

        VkAttachmentDescription color{};
        color.flags = 0;
        color.format = offscreen_color_format;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentDescription depth{};
        depth.flags = 0;
        depth.format = offscreen_depth_format;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference color_ref{};
        color_ref.attachment = 0;
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depth_ref{};
        depth_ref.attachment = 1;
        depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub{};
        sub.flags = 0;
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.inputAttachmentCount = 0;
        sub.pInputAttachments = nullptr;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &color_ref;
        sub.pResolveAttachments = nullptr;
        sub.pDepthStencilAttachment = &depth_ref;
        sub.preserveAttachmentCount = 0;
        sub.pPreserveAttachments = nullptr;

        VkSubpassDependency deps[2]{};

        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[0].dependencyFlags = 0;

        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[1].dependencyFlags = 0;

        VkAttachmentDescription atts[2] = {color, depth};

        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.pNext = nullptr;
        rp.flags = 0;
        rp.attachmentCount = 2;
        rp.pAttachments = atts;
        rp.subpassCount = 1;
        rp.pSubpasses = &sub;
        rp.dependencyCount = 2;
        rp.pDependencies = deps;

        vk_check(vkCreateRenderPass(device, &rp, nullptr, &offscreen_render_pass),
                 "vkCreateRenderPass(offscreen)");

        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.pNext = nullptr;
        si.flags = 0;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.mipLodBias = 0.0f;
        si.anisotropyEnable = VK_FALSE;
        si.maxAnisotropy = 1.0f;
        si.compareEnable = VK_FALSE;
        si.compareOp = VK_COMPARE_OP_ALWAYS;
        si.minLod = 0.0f;
        si.maxLod = 1.0f;
        si.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        si.unnormalizedCoordinates = VK_FALSE;

        vk_check(vkCreateSampler(device, &si, nullptr, &offscreen_sampler),
                 "vkCreateSampler(offscreen)");
    }

    void destroy_offscreen() {
        for (OffscreenFrame &f : offscreen) {
            if (f.imgui_texture_set) {
                ImGui_ImplVulkan_RemoveTexture(f.imgui_texture_set);
                f.imgui_texture_set = VK_NULL_HANDLE;
            }

            if (f.framebuffer) {
                vkDestroyFramebuffer(device, f.framebuffer, nullptr);
                f.framebuffer = VK_NULL_HANDLE;
            }

            if (f.depth_view) {
                vkDestroyImageView(device, f.depth_view, nullptr);
                f.depth_view = VK_NULL_HANDLE;
            }
            if (f.depth_image && f.depth_alloc) {
                vmaDestroyImage(allocator, f.depth_image, f.depth_alloc);
                f.depth_image = VK_NULL_HANDLE;
                f.depth_alloc = VK_NULL_HANDLE;
            }

            if (f.color_view) {
                vkDestroyImageView(device, f.color_view, nullptr);
                f.color_view = VK_NULL_HANDLE;
            }
            if (f.color_image && f.color_alloc) {
                vmaDestroyImage(allocator, f.color_image, f.color_alloc);
                f.color_image = VK_NULL_HANDLE;
                f.color_alloc = VK_NULL_HANDLE;
            }

            f.width = 1;
            f.height = 1;
        }

        if (offscreen_sampler) {
            vkDestroySampler(device, offscreen_sampler, nullptr);
            offscreen_sampler = VK_NULL_HANDLE;
        }
        if (offscreen_render_pass) {
            vkDestroyRenderPass(device, offscreen_render_pass, nullptr);
            offscreen_render_pass = VK_NULL_HANDLE;
        }
        offscreen_depth_format = VK_FORMAT_UNDEFINED;
    }

    void create_offscreen_frame_resources(OffscreenFrame &f, std::uint32_t w, std::uint32_t h) {
        f.width = std::max(1u, w);
        f.height = std::max(1u, h);

        // Color image
        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.pNext = nullptr;
        ici.flags = 0;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = offscreen_color_format;
        ici.extent = VkExtent3D{f.width, f.height, 1u};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.queueFamilyIndexCount = 0;
        ici.pQueueFamilyIndices = nullptr;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo ainfo{};
        ainfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        ainfo.usage = VMA_MEMORY_USAGE_AUTO;
        ainfo.requiredFlags = 0;
        ainfo.preferredFlags = 0;
        ainfo.memoryTypeBits = 0;
        ainfo.pool = VK_NULL_HANDLE;
        ainfo.pUserData = nullptr;
        ainfo.priority = 0.0f;

        vk_check(vmaCreateImage(allocator, &ici, &ainfo, &f.color_image, &f.color_alloc, nullptr),
                 "vmaCreateImage(color)");

        VkImageViewCreateInfo cvi{};
        cvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        cvi.pNext = nullptr;
        cvi.flags = 0;
        cvi.image = f.color_image;
        cvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        cvi.format = offscreen_color_format;
        cvi.components = VkComponentMapping{};
        cvi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cvi.subresourceRange.baseMipLevel = 0;
        cvi.subresourceRange.levelCount = 1;
        cvi.subresourceRange.baseArrayLayer = 0;
        cvi.subresourceRange.layerCount = 1;

        vk_check(vkCreateImageView(device, &cvi, nullptr, &f.color_view),
                 "vkCreateImageView(color)");

        // Depth image
        VkImageCreateInfo dici{};
        dici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        dici.pNext = nullptr;
        dici.flags = 0;
        dici.imageType = VK_IMAGE_TYPE_2D;
        dici.format = offscreen_depth_format;
        dici.extent = VkExtent3D{f.width, f.height, 1u};
        dici.mipLevels = 1;
        dici.arrayLayers = 1;
        dici.samples = VK_SAMPLE_COUNT_1_BIT;
        dici.tiling = VK_IMAGE_TILING_OPTIMAL;
        dici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        dici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        dici.queueFamilyIndexCount = 0;
        dici.pQueueFamilyIndices = nullptr;
        dici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        vk_check(vmaCreateImage(allocator, &dici, &ainfo, &f.depth_image, &f.depth_alloc, nullptr),
                 "vmaCreateImage(depth)");

        VkImageAspectFlags depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (offscreen_depth_format == VK_FORMAT_D24_UNORM_S8_UINT ||
            offscreen_depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
            depth_aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        VkImageViewCreateInfo dvi{};
        dvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        dvi.pNext = nullptr;
        dvi.flags = 0;
        dvi.image = f.depth_image;
        dvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        dvi.format = offscreen_depth_format;
        dvi.components = VkComponentMapping{};
        dvi.subresourceRange.aspectMask = depth_aspect;
        dvi.subresourceRange.baseMipLevel = 0;
        dvi.subresourceRange.levelCount = 1;
        dvi.subresourceRange.baseArrayLayer = 0;
        dvi.subresourceRange.layerCount = 1;

        vk_check(vkCreateImageView(device, &dvi, nullptr, &f.depth_view),
                 "vkCreateImageView(depth)");

        // Framebuffer
        VkImageView attachments[2] = {f.color_view, f.depth_view};

        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.pNext = nullptr;
        fb.flags = 0;
        fb.renderPass = offscreen_render_pass;
        fb.attachmentCount = 2;
        fb.pAttachments = attachments;
        fb.width = f.width;
        fb.height = f.height;
        fb.layers = 1;

        vk_check(vkCreateFramebuffer(device, &fb, nullptr, &f.framebuffer),
                 "vkCreateFramebuffer(offscreen)");

        // ImGui texture handle
        f.imgui_texture_set = ImGui_ImplVulkan_AddTexture(
            offscreen_sampler, f.color_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void recreate_offscreen(std::uint32_t w, std::uint32_t h) {
        vkDeviceWaitIdle(device);

        destroy_offscreen();
        create_offscreen_render_pass_and_sampler();

        for (OffscreenFrame &f : offscreen) {
            create_offscreen_frame_resources(f, w, h);
        }

        // Cube pipeline references the offscreen render pass.
        destroy_cube_pipeline();
        create_cube_pipeline();
    }

    void create_cube_vertex_buffer() {
        const VkDeviceSize size = static_cast<VkDeviceSize>(k_cube_vertices.size() * sizeof(Vertex));

        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.pNext = nullptr;
        bci.flags = 0;
        bci.size = size;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bci.queueFamilyIndexCount = 0;
        bci.pQueueFamilyIndices = nullptr;

        VmaAllocationCreateInfo aci{};
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.requiredFlags = 0;
        aci.preferredFlags = 0;
        aci.memoryTypeBits = 0;
        aci.pool = VK_NULL_HANDLE;
        aci.pUserData = nullptr;
        aci.priority = 0.0f;

        VmaAllocationInfo alloc_info{};
        vk_check(vmaCreateBuffer(allocator, &bci, &aci, &cube_vbo, &cube_vbo_alloc, &alloc_info),
                 "vmaCreateBuffer(cube_vbo)");

        const std::size_t nbytes = static_cast<std::size_t>(size);

        if (!alloc_info.pMappedData) {
            void *mapped = nullptr;
            vk_check(vmaMapMemory(allocator, cube_vbo_alloc, &mapped), "vmaMapMemory(cube_vbo)");
            std::memcpy(mapped, k_cube_vertices.data(), nbytes);
            vmaUnmapMemory(allocator, cube_vbo_alloc);
        } else {
            std::memcpy(alloc_info.pMappedData, k_cube_vertices.data(), nbytes);
        }
    }

    void destroy_cube_vertex_buffer() {
        if (cube_vbo && cube_vbo_alloc) {
            vmaDestroyBuffer(allocator, cube_vbo, cube_vbo_alloc);
            cube_vbo = VK_NULL_HANDLE;
            cube_vbo_alloc = VK_NULL_HANDLE;
        }
    }

    void create_cube_pipeline() {
        const std::filesystem::path base = std::filesystem::path{"assets"} / "shaders";
        const VkShaderModule vs = create_shader_module(device, base / "cube.vert.spv");
        const VkShaderModule fs = create_shader_module(device, base / "cube.frag.spv");

        VkPipelineShaderStageCreateInfo stages[2]{};

        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].pNext = nullptr;
        stages[0].flags = 0;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[0].pSpecializationInfo = nullptr;

        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].pNext = nullptr;
        stages[1].flags = 0;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;
        stages[1].pName = "main";
        stages[1].pSpecializationInfo = nullptr;

        const VkVertexInputBindingDescription binding{
            .binding = 0,
            .stride = static_cast<std::uint32_t>(sizeof(Vertex)),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        const std::array<VkVertexInputAttributeDescription, 2> attrs = {
            VkVertexInputAttributeDescription{
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = 0,
            },
            VkVertexInputAttributeDescription{
                .location = 1,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = static_cast<std::uint32_t>(offsetof(Vertex, color)),
            },
        };

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.pNext = nullptr;
        vi.flags = 0;
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrs.size());
        vi.pVertexAttributeDescriptions = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.pNext = nullptr;
        ia.flags = 0;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ia.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.pNext = nullptr;
        vp.flags = 0;
        vp.viewportCount = 1;
        vp.pViewports = nullptr; // dynamic
        vp.scissorCount = 1;
        vp.pScissors = nullptr; // dynamic

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.pNext = nullptr;
        rs.flags = 0;
        rs.depthClampEnable = VK_FALSE;
        rs.rasterizerDiscardEnable = VK_FALSE;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.depthBiasEnable = VK_FALSE;
        rs.depthBiasConstantFactor = 0.0f;
        rs.depthBiasClamp = 0.0f;
        rs.depthBiasSlopeFactor = 0.0f;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.pNext = nullptr;
        ms.flags = 0;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        ms.sampleShadingEnable = VK_FALSE;
        ms.minSampleShading = 0.0f;
        ms.pSampleMask = nullptr;
        ms.alphaToCoverageEnable = VK_FALSE;
        ms.alphaToOneEnable = VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.pNext = nullptr;
        ds.flags = 0;
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
        ds.depthBoundsTestEnable = VK_FALSE;
        ds.stencilTestEnable = VK_FALSE;
        ds.front = VkStencilOpState{};
        ds.back = VkStencilOpState{};
        ds.minDepthBounds = 0.0f;
        ds.maxDepthBounds = 1.0f;

        VkPipelineColorBlendAttachmentState cb_att{};
        cb_att.blendEnable = VK_FALSE;
        cb_att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cb_att.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        cb_att.colorBlendOp = VK_BLEND_OP_ADD;
        cb_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cb_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cb_att.alphaBlendOp = VK_BLEND_OP_ADD;
        cb_att.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.pNext = nullptr;
        cb.flags = 0;
        cb.logicOpEnable = VK_FALSE;
        cb.logicOp = VK_LOGIC_OP_COPY;
        cb.attachmentCount = 1;
        cb.pAttachments = &cb_att;
        cb.blendConstants[0] = 0.0f;
        cb.blendConstants[1] = 0.0f;
        cb.blendConstants[2] = 0.0f;
        cb.blendConstants[3] = 0.0f;

        const std::array<VkDynamicState, 2> dyn_states = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.pNext = nullptr;
        dyn.flags = 0;
        dyn.dynamicStateCount = static_cast<std::uint32_t>(dyn_states.size());
        dyn.pDynamicStates = dyn_states.data();

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcr.offset = 0;
        pcr.size = static_cast<std::uint32_t>(sizeof(glm::mat4));

        VkPipelineLayoutCreateInfo pl{};
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.pNext = nullptr;
        pl.flags = 0;
        pl.setLayoutCount = 0;
        pl.pSetLayouts = nullptr;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &pcr;

        vk_check(vkCreatePipelineLayout(device, &pl, nullptr, &cube_pipeline_layout),
                 "vkCreatePipelineLayout(cube)");

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.pNext = nullptr;
        gp.flags = 0;
        gp.stageCount = 2;
        gp.pStages = stages;
        gp.pVertexInputState = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pTessellationState = nullptr;
        gp.pViewportState = &vp;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cb;
        gp.pDynamicState = &dyn;
        gp.layout = cube_pipeline_layout;
        gp.renderPass = offscreen_render_pass;
        gp.subpass = 0;
        gp.basePipelineHandle = VK_NULL_HANDLE;
        gp.basePipelineIndex = -1;

        vk_check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &cube_pipeline),
                 "vkCreateGraphicsPipelines(cube)");

        vkDestroyShaderModule(device, fs, nullptr);
        vkDestroyShaderModule(device, vs, nullptr);
    }

    void destroy_cube_pipeline() {
        if (cube_pipeline) {
            vkDestroyPipeline(device, cube_pipeline, nullptr);
            cube_pipeline = VK_NULL_HANDLE;
        }
        if (cube_pipeline_layout) {
            vkDestroyPipelineLayout(device, cube_pipeline_layout, nullptr);
            cube_pipeline_layout = VK_NULL_HANDLE;
        }
    }

    void record_offscreen(VkCommandBuffer cb, const OffscreenFrame &f) {
        VkClearValue clears[2]{};
        clears[0].color = VkClearColorValue{{0.18f, 0.18f, 0.18f, 1.0f}};
        clears[1].depthStencil = VkClearDepthStencilValue{1.0f, 0};

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.pNext = nullptr;
        rp.renderPass = offscreen_render_pass;
        rp.framebuffer = f.framebuffer;
        rp.renderArea.offset = VkOffset2D{0, 0};
        rp.renderArea.extent = VkExtent2D{f.width, f.height};
        rp.clearValueCount = 2;
        rp.pClearValues = clears;

        vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{};
        vp.x = 0.0f;
        vp.y = 0.0f;
        vp.width = static_cast<float>(f.width);
        vp.height = static_cast<float>(f.height);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cb, 0, 1, &vp);

        VkRect2D sc{};
        sc.offset = VkOffset2D{0, 0};
        sc.extent = VkExtent2D{f.width, f.height};
        vkCmdSetScissor(cb, 0, 1, &sc);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, cube_pipeline);

        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &cube_vbo, &off);

        const auto now = std::chrono::steady_clock::now();
        const float t = std::chrono::duration<float>(now - start_time).count();

        const glm::mat4 M =
            glm::rotate(glm::mat4(1.0f), t, glm::vec3(0.0f, 0.0f, 1.0f)) *
            glm::rotate(glm::mat4(1.0f), 0.6f * t, glm::vec3(0.0f, 1.0f, 0.0f));

        const glm::vec3 eye = glm::vec3(2.4f, -3.2f, 1.8f);
        const glm::vec3 at = glm::vec3(0.0f, 0.0f, 0.0f);
        const glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);

        const glm::mat4 V = glm::lookAt(eye, at, up);

        const float aspect = (f.height > 0u)
                                 ? (static_cast<float>(f.width) / static_cast<float>(f.height))
                                 : 1.0f;
        glm::mat4 P = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 0.1f, 100.0f);

        // Vulkan NDC Y is inverted relative to typical camera expectations.
        P[1][1] *= -1.0f;

        const glm::mat4 MVP = P * V * M;

        vkCmdPushConstants(cb, cube_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           static_cast<std::uint32_t>(sizeof(glm::mat4)), &MVP);

        vkCmdDraw(cb, static_cast<std::uint32_t>(k_cube_vertices.size()), 1, 0, 0);

        vkCmdEndRenderPass(cb);
    }

    void record_swapchain(VkCommandBuffer cb, VkFramebuffer fb) {
        VkClearValue clear{};
        clear.color = VkClearColorValue{{0.10f, 0.10f, 0.10f, 1.0f}};

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.pNext = nullptr;
        rp.renderPass = swapchain_render_pass;
        rp.framebuffer = fb;
        rp.renderArea.offset = VkOffset2D{0, 0};
        rp.renderArea.extent = swapchain_extent;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;

        vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);

        vkCmdEndRenderPass(cb);
    }

    void draw_frame() {
        Frame &fr = frames[frame_index];

        vk_check(vkWaitForFences(device, 1, &fr.in_flight, VK_TRUE, UINT64_MAX),
                 "vkWaitForFences");
        vk_check(vkResetFences(device, 1, &fr.in_flight), "vkResetFences");

        std::uint32_t image_index = 0;
        VkResult acquire = vkAcquireNextImageKHR(
            device, swapchain, UINT64_MAX, fr.image_acquired, VK_NULL_HANDLE, &image_index);

        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            recreate_swapchain();
            return;
        }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
            vk_check(acquire, "vkAcquireNextImageKHR");
        }

        vk_check(vkResetCommandBuffer(fr.cmd, 0), "vkResetCommandBuffer");

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.pNext = nullptr;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        bi.pInheritanceInfo = nullptr;
        vk_check(vkBeginCommandBuffer(fr.cmd, &bi), "vkBeginCommandBuffer");

        record_offscreen(fr.cmd, offscreen[frame_index]);
        record_swapchain(fr.cmd, swapchain_framebuffers.at(image_index));

        vk_check(vkEndCommandBuffer(fr.cmd), "vkEndCommandBuffer");

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.pNext = nullptr;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &fr.image_acquired;
        si.pWaitDstStageMask = &wait_stage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &fr.cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &fr.render_complete;

        vk_check(vkQueueSubmit(graphics_queue, 1, &si, fr.in_flight),
                 "vkQueueSubmit");

        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.pNext = nullptr;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &fr.render_complete;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain;
        pi.pImageIndices = &image_index;
        pi.pResults = nullptr;

        VkResult present = vkQueuePresentKHR(graphics_queue, &pi);
        if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR || framebuffer_resized) {
            recreate_swapchain();
        } else {
            vk_check(present, "vkQueuePresentKHR");
        }

        frame_index = (frame_index + 1u) % k_frames_in_flight;
    }

    void build_ui() {
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        ImGui::Begin("Viewport", nullptr,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGuiIO &io = ImGui::GetIO();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 scale = io.DisplayFramebufferScale;

        const float px_w_f = std::max(1.0f, avail.x * scale.x);
        const float px_h_f = std::max(1.0f, avail.y * scale.y);

        const std::uint32_t px_w = static_cast<std::uint32_t>(std::lround(px_w_f));
        const std::uint32_t px_h = static_cast<std::uint32_t>(std::lround(px_h_f));

        OffscreenFrame &cur = offscreen[frame_index];

        if (px_w != cur.width || px_h != cur.height) {
            recreate_offscreen(px_w, px_h);
        }

        if (cur.imgui_texture_set != VK_NULL_HANDLE) {
            ImGui::Image(
                to_imgui_texture_id(cur.imgui_texture_set),
                avail);
        }
        ImGui::End();

        ImGui::Begin("Info");
        ImGui::Text("Frame-in-flight: %u / %u", frame_index, k_frames_in_flight);
        ImGui::Text("Swapchain: %ux%u (images=%zu)",
                    swapchain_extent.width, swapchain_extent.height, swapchain_images.size());
        ImGui::Text("Offscreen: %ux%u",
                    offscreen[frame_index].width, offscreen[frame_index].height);
        ImGui::End();
    }

    void init_all() {
        init_window();
        create_instance();
        create_surface();
        create_device();
        create_allocator();
        create_command_pool();
        create_swapchain();

        create_sync_and_cmd_buffers();

        create_imgui_descriptor_pool();
        init_imgui();

        create_offscreen_render_pass_and_sampler();
        for (OffscreenFrame &f : offscreen) {
            create_offscreen_frame_resources(f, 1280u, 720u);
        }

        create_cube_pipeline();
        create_cube_vertex_buffer();

        start_time = std::chrono::steady_clock::now();
    }

    void shutdown_all() noexcept {
        // Best-effort cleanup; do not throw.
        if (device) {
            vkDeviceWaitIdle(device);
        }

        if (device && allocator) {
            destroy_cube_vertex_buffer();
            destroy_cube_pipeline();
        }

        if (device && allocator) {
            destroy_offscreen();
        }

        if (device) {
            destroy_imgui();
        }

        if (device) {
            for (Frame &f : frames) {
                if (f.in_flight) {
                    vkDestroyFence(device, f.in_flight, nullptr);
                }
                if (f.render_complete) {
                    vkDestroySemaphore(device, f.render_complete, nullptr);
                }
                if (f.image_acquired) {
                    vkDestroySemaphore(device, f.image_acquired, nullptr);
                }
                f.in_flight = VK_NULL_HANDLE;
                f.render_complete = VK_NULL_HANDLE;
                f.image_acquired = VK_NULL_HANDLE;
                f.cmd = VK_NULL_HANDLE;
            }

            if (cmd_pool) {
                vkDestroyCommandPool(device, cmd_pool, nullptr);
                cmd_pool = VK_NULL_HANDLE;
            }

            destroy_swapchain_resources();
        }

        if (allocator) {
            vmaDestroyAllocator(allocator);
            allocator = VK_NULL_HANDLE;
        }

        if (surface && instance) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }

        if (device) {
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }

        if (instance) {
            destroy_debug_messenger(instance, debug_messenger);
            debug_messenger = VK_NULL_HANDLE;

            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }

        if (window) {
            glfwDestroyWindow(window);
            window = nullptr;
        }
        glfwTerminate();
    }

    void run_loop() {
        while (window && glfwWindowShouldClose(window) == GLFW_FALSE) {
            glfwPollEvents();

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            build_ui();

            ImGui::Render();

            draw_frame();
        }

        if (device) {
            vkDeviceWaitIdle(device);
        }
    }
};

VulkanMvp::VulkanMvp() : impl_{std::make_unique<Impl>()} {
}

VulkanMvp::~VulkanMvp() {
    if (impl_) {
        impl_->shutdown_all();
    }
}

void VulkanMvp::run() {
    impl_->init_all();
    impl_->run_loop();
}

} // namespace ds_pba
