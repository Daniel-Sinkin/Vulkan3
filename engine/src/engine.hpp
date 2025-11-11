// engine/src/engine.hpp
#pragma once

#include <array>
#include <print>
#include <ranges>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <stdio.h>
#include <stdlib.h>

#include "constants.hpp"
#include "types.hpp"
#include "util.hpp"

#ifdef APP_USE_VULKAN_DEBUG_REPORT
static VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
#endif

namespace DSEngine
{

namespace SDL
{
inline void print_window_info(SDL_Window *window)
{
    if (!window)
    {
        std::print(stderr, "Window pointer is null\n");
        return;
    }

    const char *title = SDL_GetWindowTitle(window);
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    SDL_DisplayID display_id = SDL_GetDisplayForWindow(window);
    float scale = SDL_GetDisplayContentScale(display_id);
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);

    auto yesno = [](bool v)
    {
        return v ? "yes" : "no";
    };

    std::print(stderr,
        "Window Info:\n"
        "  Title: {}\n"
        "  Size: {}x{}\n"
        "  Display ID: {}\n"
        "  Content Scale: {}\n"
        "  Flags: 0x{:08X}\n"
        "    - {:<15} {}\n"
        "    - {:<15} {}\n"
        "    - {:<15} {}\n"
        "    - {:<15} {}\n"
        "    - {:<15} {}\n"
        "    - {:<15} {}\n"
        "    - {:<15} {}\n"
        "    - {:<15} {}\n"
        "    - {:<15} {}\n"
        "    - {:<15} {}\n"
        "  Pointer: {}\n",
        title ? title : "(null)",
        w, h,
        static_cast<unsigned int>(display_id),
        scale,
        static_cast<unsigned int>(flags),
        "Fullscreen:", yesno(flags & SDL_WINDOW_FULLSCREEN),
        "Hidden:", yesno(flags & SDL_WINDOW_HIDDEN),
        "Borderless:", yesno(flags & SDL_WINDOW_BORDERLESS),
        "Resizable:", yesno(flags & SDL_WINDOW_RESIZABLE),
        "Minimized:", yesno(flags & SDL_WINDOW_MINIMIZED),
        "Maximized:", yesno(flags & SDL_WINDOW_MAXIMIZED),
        "High DPI:", yesno(flags & SDL_WINDOW_HIGH_PIXEL_DENSITY),
        "Vulkan:", yesno(flags & SDL_WINDOW_VULKAN),
        "Metal:", yesno(flags & SDL_WINDOW_METAL),
        "OpenGL:", yesno(flags & SDL_WINDOW_OPENGL),
        static_cast<const void *>(window));
}
} // namespace SDL

static void vk_check(VkResult err)
{
    if (err == VK_SUCCESS) return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0) abort();
}

#ifdef APP_USE_VULKAN_DEBUG_REPORT
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(
    [[maybe_unused]] VkDebugReportFlagsEXT flags,
    VkDebugReportObjectTypeEXT objectType,
    [[maybe_unused]] u64 object,
    [[maybe_unused]] usize location,
    [[maybe_unused]] i32 messageCode,
    [[maybe_unused]] const char *pLayerPrefix,
    const char *pMessage,
    [[maybe_unused]] void *pUserData)
{
    fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
    return VK_FALSE;
}
#endif // APP_USE_VULKAN_DEBUG_REPORT

static bool IsExtensionAvailable(const std::vector<VkExtensionProperties> &properties, const char *extension)
{
    for (const VkExtensionProperties &p : properties)
    {
        if (strcmp(p.extensionName, extension) == 0) return true;
    }
    return false;
}

// TODO: Replace those, either with my own structures or by inlining those
struct FrameContext
{
    VkCommandPool CommandPool;
    VkCommandBuffer CommandBuffer;
    VkFence Fence;
    VkImage Backbuffer;
    VkImageView BackbufferView;
    VkFramebuffer Framebuffer;
};

// TODO: Replace those, either with my own structures or by inlining those
struct FrameSemaphores
{
    VkSemaphore ImageAcquiredSemaphore;
    VkSemaphore RenderCompleteSemaphore;
};

struct EngineContext
{
    VkAllocationCallbacks *m_allocator = nullptr;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    u32 m_queue_family = Constants::queue_family_uninitialised;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkPipelineCache m_pipeline_cache = VK_NULL_HANDLE;
    VkDescriptorPool m_description_pool = VK_NULL_HANDLE;

    ImGui_ImplVulkanH_Window *m_main_window_data; // TODO: Inline this and remove dep on imgui structs

    int m_width;
    int m_height;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSurfaceFormatKHR m_surface_format{};
    VkPresentModeKHR m_present_mode = VkPresentModeKHR::VK_PRESENT_MODE_MAX_ENUM_KHR;
    VkRenderPass m_render_pass = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    bool m_use_dynamic_rendering;
    bool m_clear_enabled;
    VkClearValue m_clear_value{};
    u32 m_frame_index;     // Current frame being rendered to (0 <= FrameIndex < FrameInFlightCount)
    u32 m_image_count;     // Number of simultaneous in-flight frames (returned by vkGetSwapchainImagesKHR, usually derived from min_image_count)
    u32 m_semaphore_count; // Number of simultaneous in-flight frames + 1, to be able to use it in vkAcquireNextImageKHR
    u32 m_semaphore_index; // Current set of swapchain wait semaphores we're using (needs to be distinct from per frame data)
    std::vector<FrameContext> m_frames;
    std::vector<FrameSemaphores> m_frame_semaphores;

    u32 m_min_image_count = 2;
    bool m_rebuild_swapchain = false;

    std::vector<const char *> m_extensions{};

    SDL_Window *m_window = nullptr;

    inline void print_window_info() const noexcept { SDL::print_window_info(m_window); }
    inline void print_extensions() const noexcept
    {
        if (m_extensions.empty())
        {
            std::println("No Vulkan extensions detected!");
        }
        else
        {
            std::println("There are {} Vulkan extensions:", m_extensions.size());
            for (usize i = 0; i < m_extensions.size(); ++i)
            {
                std::println(" [{:>2}] {}", i, m_extensions[i]);
            }
        }
    }
    inline SDL_DisplayID get_sdl_display_id() const noexcept { return SDL_GetDisplayForWindow(m_window); }
    inline f32 get_display_content_scale() const noexcept { return SDL_GetDisplayContentScale(get_sdl_display_id()); }
    inline SDL_WindowFlags get_sdl_window_flags() const noexcept { return SDL_GetWindowFlags(m_window); }

    void setup()
    {
        setup_sdl_();
        setup_vulkan_();
    }

    void setup_vulkan_window_select_surface_format()
    { // TODO: Maybe use other name
        // TODO: Make this private and integrate into the setup_vulkan chain
        constexpr std::array<VkFormat, 4> request_surface_image_format = {
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_B8G8R8_UNORM,
            VK_FORMAT_R8G8B8_UNORM};
        const VkColorSpaceKHR request_surface_color_space = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        u32 avail_count{};
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &avail_count, nullptr);
        std::vector<VkSurfaceFormatKHR> avail_format(avail_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &avail_count, avail_format.data());

        // First check if only one format, VK_FORMAT_UNDEFINED, is available, which would imply that any format is available
        if (avail_count == 1)
        {
            if (avail_format[0].format == VK_FORMAT_UNDEFINED)
            {
                VkSurfaceFormatKHR ret;
                ret.format = request_surface_image_format[0];
                ret.colorSpace = request_surface_color_space;
                m_surface_format = ret;
            }
            else
            {
                // No point in searching another format
                m_surface_format = avail_format[0];
            }
        }
        else
        {
            for (usize request_i = 0; request_i < request_surface_image_format.size(); request_i++)
            {
                for (uint32_t avail_i = 0; avail_i < avail_count; avail_i++)
                {
                    if (avail_format[avail_i].format == request_surface_image_format[request_i] && avail_format[avail_i].colorSpace == request_surface_color_space)
                    {
                        m_surface_format = avail_format[avail_i];
                    }
                }
            }
            m_surface_format = avail_format[0];
        }
    }

    void setup_vulkan_window_select_presentation_mode()
    {
#ifdef APP_USE_UNLIMITED_FRAME_RATE
        constexpr std::array present_modes = {
            VK_PRESENT_MODE_MAILBOX_KHR,
            VK_PRESENT_MODE_IMMEDIATE_KHR,
            VK_PRESENT_MODE_FIFO_KHR};
#else
        constexpr std::array present_modes = {
            VK_PRESENT_MODE_FIFO_KHR};
#endif

        static_assert(
            std::ranges::find(present_modes, VK_PRESENT_MODE_FIFO_KHR) != present_modes.end(),
            "VK_PRESENT_MODE_FIFO_KHR must be present in present_modes");

        u32 num_supported = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical_device, m_surface, &num_supported, nullptr);
        std::vector<VkPresentModeKHR> supported_modes;
        supported_modes.resize(static_cast<usize>(num_supported));
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical_device, m_surface, &num_supported, supported_modes.data());
        for (const auto mode : present_modes)
        {
            if (std::ranges::find(supported_modes, mode) != supported_modes.end())
            {
                m_present_mode = mode;
                return;
            }
        }
        m_present_mode = VK_PRESENT_MODE_FIFO_KHR;
    }

private:
    void
    setup_sdl_()
    {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) PANIC_MSG("Error: SDL_Init(): %s", SDL_GetError());

        // Create window with Vulkan graphics context
        f32 main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        SDL_Window *window = SDL_CreateWindow("DSEngine", (int)(1280 * main_scale), (int)(800 * main_scale), window_flags);
        m_window = window;
        if (!m_window) PANIC_MSG("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
    }

    void setup_vulkan_()
    {
        // "VK_KHR_surface"
        // "VK_EXT_metal_surface"
        // "VK_KHR_portability_enumeration"
        u32 sdl_extensions_count = 0;
        const char *const *sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);
        m_extensions.insert(m_extensions.end(), sdl_extensions, sdl_extensions + sdl_extensions_count);

        setup_vulkan_instance_();

        m_physical_device = ImGui_ImplVulkanH_SelectPhysicalDevice(m_instance);
        DS_ASSERT(m_physical_device != VK_NULL_HANDLE);

        m_queue_family = ImGui_ImplVulkanH_SelectQueueFamilyIndex(m_physical_device);
        DS_ASSERT(m_queue_family != Constants::queue_family_uninitialised);

        setup_vulkan_logical_device_();
        setup_vulkan_descriptor_pool_();
    }
    void setup_vulkan_instance_()
    {
        VkInstanceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

        u32 properties_count;
        std::vector<VkExtensionProperties> properties;
        vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        vk_check(vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.data()));

        if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        {
            m_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        }
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
        {
            m_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
#endif

#ifdef APP_USE_VULKAN_DEBUG_REPORT
        const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = layers;
        m_extensions.push_back("VK_EXT_debug_report");
#endif

        create_info.enabledExtensionCount = static_cast<u32>(m_extensions.size());
        create_info.ppEnabledExtensionNames = m_extensions.data();
        vk_check(vkCreateInstance(&create_info, m_allocator, &m_instance));

#ifdef APP_USE_VULKAN_DEBUG_REPORT
        auto f_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugReportCallbackEXT");
        DS_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
        VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
        debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        debug_report_ci.pfnCallback = debug_report;
        debug_report_ci.pUserData = nullptr;
        vk_check(f_vkCreateDebugReportCallbackEXT(m_instance, &debug_report_ci, m_allocator, &g_DebugReport));
#endif
    }

    void setup_vulkan_logical_device_()
    {
        std::vector<const char *> device_extensions;
        device_extensions.push_back("VK_KHR_swapchain");

        // Enumerate physical device extension
        u32 properties_count;
        std::vector<VkExtensionProperties> properties;
        vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &properties_count, properties.data());
#if DSE_PORTABILITY_REQUIRED
        if (IsExtensionAvailable(properties, "VK_KHR_portability_subset"))
        {
            device_extensions.push_back("VK_KHR_portability_subset");
        }
#endif

        const f32 queue_priority[] = {1.0f};
        VkDeviceQueueCreateInfo queue_info[1] = {};
        queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[0].queueFamilyIndex = m_queue_family;
        queue_info[0].queueCount = 1;
        queue_info[0].pQueuePriorities = queue_priority;
        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
        create_info.pQueueCreateInfos = queue_info;
        create_info.enabledExtensionCount = static_cast<u32>(device_extensions.size());
        create_info.ppEnabledExtensionNames = device_extensions.data();
        vk_check(vkCreateDevice(m_physical_device, &create_info, m_allocator, &m_device));
        vkGetDeviceQueue(m_device, m_queue_family, 0, &m_queue);
    }

    void setup_vulkan_descriptor_pool_()
    {
        VkDescriptorPoolSize pool_sizes[] =
            {
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE},
            };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 0;
        for (VkDescriptorPoolSize &pool_size : pool_sizes)
        {
            pool_info.maxSets += pool_size.descriptorCount;
        }
        pool_info.poolSizeCount = static_cast<u32>(IM_ARRAYSIZE(pool_sizes));
        pool_info.pPoolSizes = pool_sizes;
        vk_check(vkCreateDescriptorPool(m_device, &pool_info, m_allocator, &m_description_pool));
    }
};

// clang-format off
static VkAllocationCallbacks*     g_Allocator       = nullptr;
static VkInstance                 g_Instance        = VK_NULL_HANDLE;
static VkPhysicalDevice           g_PhysicalDevice  = VK_NULL_HANDLE;
static VkDevice                   g_Device          = VK_NULL_HANDLE;
static u32                        g_QueueFamily     = Constants::queue_family_uninitialised;
static VkQueue                    g_Queue           = VK_NULL_HANDLE;
static VkPipelineCache            g_PipelineCache   = VK_NULL_HANDLE;
static VkDescriptorPool           g_DescriptorPool  = VK_NULL_HANDLE;

static ImGui_ImplVulkanH_Window   g_MainWindowData;
static u32                        g_MinImageCount   = 2;
static bool                       g_SwapChainRebuild = false;
// clang-format on

static void SetupVulkan(std::vector<const char *> instance_extensions)
{
    VkResult err;

    // Create Vulkan Instance
    {
        VkInstanceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

        // Enumerate available extensions
        u32 properties_count;
        std::vector<VkExtensionProperties> properties;
        vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.data());
        vk_check(err);

        // Enable required extensions
        if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
            instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
        {
            instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
#endif

        // Enabling validation layers
#ifdef APP_USE_VULKAN_DEBUG_REPORT
        const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = layers;
        instance_extensions.push_back("VK_EXT_debug_report");
#endif

        // Create Vulkan Instance
        create_info.enabledExtensionCount = static_cast<u32>(instance_extensions.size());
        create_info.ppEnabledExtensionNames = instance_extensions.data();
        err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
        vk_check(err);

        // Setup the debug report callback
#ifdef APP_USE_VULKAN_DEBUG_REPORT
        auto f_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkCreateDebugReportCallbackEXT");
        DS_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
        VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
        debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        debug_report_ci.pfnCallback = debug_report;
        debug_report_ci.pUserData = nullptr;
        err = f_vkCreateDebugReportCallbackEXT(g_Instance, &debug_report_ci, g_Allocator, &g_DebugReport);
        vk_check(err);
#endif
    }

    // Select Physical Device (GPU)
    g_PhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(g_Instance);
    DS_ASSERT(g_PhysicalDevice != VK_NULL_HANDLE);

    // Select graphics queue family
    g_QueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(g_PhysicalDevice);
    DS_ASSERT(g_QueueFamily != Constants::queue_family_uninitialised);

    // Create Logical Device (with 1 queue)
    {
        std::vector<const char *> device_extensions;
        device_extensions.push_back("VK_KHR_swapchain");

        // Enumerate physical device extension
        u32 properties_count;
        std::vector<VkExtensionProperties> properties;
        vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, properties.data());
#if DSE_PORTABILITY_REQUIRED
        if (IsExtensionAvailable(properties, "VK_KHR_portability_subset"))
        {
            device_extensions.push_back("VK_KHR_portability_subset");
        }
#endif

        const f32 queue_priority[] = {1.0f};
        VkDeviceQueueCreateInfo queue_info[1] = {};
        queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[0].queueFamilyIndex = g_QueueFamily;
        queue_info[0].queueCount = 1;
        queue_info[0].pQueuePriorities = queue_priority;
        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
        create_info.pQueueCreateInfos = queue_info;
        create_info.enabledExtensionCount = (u32)device_extensions.size();
        create_info.ppEnabledExtensionNames = device_extensions.data();
        err = vkCreateDevice(g_PhysicalDevice, &create_info, g_Allocator, &g_Device);
        vk_check(err);
        vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
    }

    // Create Descriptor Pool
    // If you wish to load e.g. additional textures you may need to alter pools sizes and maxSets.
    {
        VkDescriptorPoolSize pool_sizes[] =
            {
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE},
            };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 0;
        for (VkDescriptorPoolSize &pool_size : pool_sizes)
        {
            pool_info.maxSets += pool_size.descriptorCount;
        }
        pool_info.poolSizeCount = (u32)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
        vk_check(err);
    }
}

static void CleanupVulkan()
{
    vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
    // Remove the debug report callback
    auto f_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkDestroyDebugReportCallbackEXT");
    f_vkDestroyDebugReportCallbackEXT(g_Instance, g_DebugReport, g_Allocator);
#endif // APP_USE_VULKAN_DEBUG_REPORT

    vkDestroyDevice(g_Device, g_Allocator);
    vkDestroyInstance(g_Instance, g_Allocator);
}

static void CleanupVulkanWindow()
{
    ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, &g_MainWindowData, g_Allocator);
}

static void FrameRender(ImGui_ImplVulkanH_Window *wd, ImDrawData *draw_data)
{
    VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkResult err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) g_SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (err != VK_SUBOPTIMAL_KHR) vk_check(err);
    ImGui_ImplVulkanH_Frame *fd = &wd->Frames[wd->FrameIndex];
    {
        // wait indefinitely instead of periodically checking
        vk_check(vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX));
        vk_check(vkResetFences(g_Device, 1, &fd->Fence));
    }
    {
        vk_check(vkResetCommandPool(g_Device, fd->CommandPool, 0));
        VkCommandBufferBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk_check(vkBeginCommandBuffer(fd->CommandBuffer, &info));
    }
    {
        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = wd->RenderPass;
        info.framebuffer = fd->Framebuffer;
        info.renderArea.extent.width = wd->Width;
        info.renderArea.extent.height = wd->Height;
        info.clearValueCount = 1;
        info.pClearValues = &wd->ClearValue;
        vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    // Record dear imgui primitives into command buffer
    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    // Submit command buffer
    vkCmdEndRenderPass(fd->CommandBuffer);
    {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &image_acquired_semaphore;
        info.pWaitDstStageMask = &wait_stage;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &fd->CommandBuffer;
        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores = &render_complete_semaphore;

        vk_check(vkEndCommandBuffer(fd->CommandBuffer));
        vk_check(vkQueueSubmit(g_Queue, 1, &info, fd->Fence));
    }
}

static void FramePresent(ImGui_ImplVulkanH_Window *wd)
{
    if (g_SwapChainRebuild)
        return;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_complete_semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &wd->Swapchain;
    info.pImageIndices = &wd->FrameIndex;
    VkResult err = vkQueuePresentKHR(g_Queue, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) g_SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (err != VK_SUBOPTIMAL_KHR) vk_check(err);
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount; // Now we can use the next set of semaphores
}

// Main code
int main()
{
    EngineContext ctx{};
    ctx.setup();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) PANIC_MSG("Error: SDL_Init(): %s", SDL_GetError());

    SDL_Window *window = ctx.m_window;
    f32 main_scale = ctx.get_display_content_scale();
    SDL_WindowFlags window_flags = ctx.get_sdl_window_flags();

    // Setup Vulkan
    g_Allocator = ctx.m_allocator;
    g_Instance = ctx.m_instance;
    g_PhysicalDevice = ctx.m_physical_device;
    g_Device = ctx.m_device;
    g_QueueFamily = ctx.m_queue_family;
    g_Queue = ctx.m_queue;
    g_PipelineCache = ctx.m_pipeline_cache;
    g_DescriptorPool = ctx.m_description_pool;

    // Create Window Surface
    {
        if (SDL_Vulkan_CreateSurface(window, ctx.m_instance, ctx.m_allocator, &ctx.m_surface) == 0)
        {
            printf("Failed to create Vulkan surface.\n");
            return 1;
        }

        // Create Framebuffers
        int window_width, window_height;
        SDL_GetWindowSize(window, &window_width, &window_height);
        ctx.m_main_window_data = &g_MainWindowData;
        // SetupVulkanWindow(wd, surface, window_width, window_height);
        {
            ctx.m_main_window_data->Surface = ctx.m_surface;

            // Check for WSI support
            VkBool32 res;
            vkGetPhysicalDeviceSurfaceSupportKHR(ctx.m_physical_device, ctx.m_queue_family, ctx.m_surface, &res);
            if (res != VK_TRUE)
            {
                fprintf(stderr, "Error no WSI support on physical device 0\n");
                exit(-1);
            }
            ctx.setup_vulkan_window_select_surface_format();
            ctx.setup_vulkan_window_select_presentation_mode();

            // Create SwapChain, RenderPass, Framebuffer, etc.
            ctx.m_main_window_data->Surface = ctx.m_surface;
            ctx.m_main_window_data->SurfaceFormat = ctx.m_surface_format;
            ctx.m_main_window_data->PresentMode = ctx.m_present_mode;
            DS_ASSERT(g_MinImageCount >= 2);
            ImGui_ImplVulkanH_CreateOrResizeWindow(
                ctx.m_instance,
                ctx.m_physical_device,
                ctx.m_device,
                ctx.m_main_window_data,
                ctx.m_queue_family,
                ctx.m_allocator,
                window_width,
                window_height,
                ctx.m_min_image_count,
                0);
        }
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_ShowWindow(window);
    }
    ImGui_ImplVulkanH_Window *wd = ctx.m_main_window_data;
    g_Allocator = ctx.m_allocator;
    g_Instance = ctx.m_instance;
    g_PhysicalDevice = ctx.m_physical_device;
    g_Device = ctx.m_device;
    g_QueueFamily = ctx.m_queue_family;
    g_Queue = ctx.m_queue;
    g_PipelineCache = ctx.m_pipeline_cache;
    g_DescriptorPool = ctx.m_description_pool;

    /*
    ###################
    REFACTOR CHECKPOINT
    ###################
    */

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale); // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale; // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo init_info = {};
    // init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass in your value of VkApplicationInfo::apiVersion, otherwise will default to header version.
    init_info.Instance = g_Instance;
    init_info.PhysicalDevice = g_PhysicalDevice;
    init_info.Device = g_Device;
    init_info.QueueFamily = g_QueueFamily;
    init_info.Queue = g_Queue;
    init_info.PipelineCache = g_PipelineCache;
    init_info.DescriptorPool = g_DescriptorPool;
    init_info.MinImageCount = g_MinImageCount;
    init_info.ImageCount = wd->ImageCount;
    init_info.Allocator = g_Allocator;
    init_info.PipelineInfoMain.RenderPass = wd->RenderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = vk_check;
    ImGui_ImplVulkan_Init(&init_info);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details. If you like the default font but want it to scale better, consider using the 'ProggyVector' from the same author!
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // style.FontSizeBase = 20.0f;
    // io.Fonts->AddFontDefault();
    // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    // ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    // DS_ASSERT(font != nullptr);

    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
            {
                done = true;
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
            {
                done = true;
            }
        }

        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        // Resize swap chain?
        int fb_width;
        int fb_height;
        SDL_GetWindowSize(window, &fb_width, &fb_height);
        if (fb_width > 0 && fb_height > 0 && (g_SwapChainRebuild || g_MainWindowData.Width != fb_width || g_MainWindowData.Height != fb_height))
        {
            ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
            ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, fb_width, fb_height, g_MinImageCount, 0);
            g_MainWindowData.FrameIndex = 0;
            g_SwapChainRebuild = false;
        }

        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (show_demo_window) ImGui::ShowDemoWindow(&show_demo_window);

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
        {
            static f32 f = 0.0f;
            static int counter = 0;

            ImGui::Begin("Hello, world!"); // Create a window called "Hello, world!" and append into it.

            ImGui::Text("This is some useful text.");          // Display some text (you can use a format strings too)
            ImGui::Checkbox("Demo Window", &show_demo_window); // Edit bools storing our window open/close state
            ImGui::Checkbox("Another Window", &show_another_window);

            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);           // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color", (f32 *)&clear_color); // Edit 3 floats representing a color

            // Buttons return true when clicked (most widgets return true when edited/activated)
            if (ImGui::Button("Button")) counter++;
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
        }

        // 3. Show another simple window.
        if (show_another_window)
        {
            ImGui::Begin("Another Window", &show_another_window); // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                show_another_window = false;
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        ImDrawData *draw_data = ImGui::GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
        if (!is_minimized)
        {
            wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
            wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
            wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
            wd->ClearValue.color.float32[3] = clear_color.w;
            FrameRender(wd, draw_data);
            FramePresent(wd);
        }
    }

    // Cleanup
    vk_check(vkDeviceWaitIdle(g_Device));
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    CleanupVulkanWindow();
    CleanupVulkan();

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
} // namespace DSEngine