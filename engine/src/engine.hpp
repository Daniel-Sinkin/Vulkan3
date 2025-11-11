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

    ImGui_ImplVulkanH_Window m_main_window_data{}; // TODO: Inline this and remove dep on imgui structs

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

    f32 m_main_scale = -1.0f;

    SDL_Window *m_window = nullptr;
    ImVec4 m_clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    int m_window_width = -1;
    int m_window_height = -1;

    bool m_is_active = false;

    bool m_show_demo_window = true;
    bool m_show_another_window = false;

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
        setup_vulkan_window_();
        setup_imgui_();
    }

    void recreate_window()
    {
        ImGui_ImplVulkanH_CreateOrResizeWindow(
            m_instance,
            m_physical_device,
            m_device,
            &m_main_window_data,
            m_queue_family,
            m_allocator,
            m_window_width,
            m_window_height,
            m_min_image_count,
            0);
    }

    void render_frame()
    {
        VkSemaphore image_acquired_semaphore = m_main_window_data.FrameSemaphores[m_main_window_data.SemaphoreIndex].ImageAcquiredSemaphore;
        VkSemaphore render_complete_semaphore = m_main_window_data.FrameSemaphores[m_main_window_data.SemaphoreIndex].RenderCompleteSemaphore;
        VkResult err = vkAcquireNextImageKHR(m_device, m_main_window_data.Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &m_main_window_data.FrameIndex);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) m_rebuild_swapchain = true;
        if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
        if (err != VK_SUBOPTIMAL_KHR) vk_check(err);
        ImGui_ImplVulkanH_Frame *fd = &m_main_window_data.Frames[m_main_window_data.FrameIndex];
        {
            // wait indefinitely instead of periodically checking
            vk_check(vkWaitForFences(m_device, 1, &fd->Fence, VK_TRUE, UINT64_MAX));
            vk_check(vkResetFences(m_device, 1, &fd->Fence));
        }
        {
            vk_check(vkResetCommandPool(m_device, fd->CommandPool, 0));
            VkCommandBufferBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vk_check(vkBeginCommandBuffer(fd->CommandBuffer, &info));
        }
        {
            VkRenderPassBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            info.renderPass = m_main_window_data.RenderPass;
            info.framebuffer = fd->Framebuffer;
            info.renderArea.extent.width = m_main_window_data.Width;
            info.renderArea.extent.height = m_main_window_data.Height;
            info.clearValueCount = 1;
            info.pClearValues = &m_main_window_data.ClearValue;
            vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
        }

        // Record dear imgui primitives into command buffer
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), fd->CommandBuffer);

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
            vk_check(vkQueueSubmit(m_queue, 1, &info, fd->Fence));
        }
    }

    void present_frame()
    {
        if (m_rebuild_swapchain) return;
        VkSemaphore render_complete_semaphore = m_main_window_data.FrameSemaphores[m_main_window_data.SemaphoreIndex].RenderCompleteSemaphore;
        VkPresentInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &render_complete_semaphore;
        info.swapchainCount = 1;
        info.pSwapchains = &m_main_window_data.Swapchain;
        info.pImageIndices = &m_main_window_data.FrameIndex;
        VkResult err = vkQueuePresentKHR(m_queue, &info);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) m_rebuild_swapchain = true;
        if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
        if (err != VK_SUBOPTIMAL_KHR) vk_check(err);
        m_main_window_data.SemaphoreIndex = (m_main_window_data.SemaphoreIndex + 1) % m_main_window_data.SemaphoreCount;
    }

    void cleanup()
    {
        vk_check(vkDeviceWaitIdle(m_device));
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        ImGui_ImplVulkanH_DestroyWindow(m_instance, m_device, &m_main_window_data, m_allocator);
        vkDestroyDescriptorPool(m_device, m_description_pool, m_allocator);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
        // Remove the debug report callback
        auto f_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugReportCallbackEXT");
        f_vkDestroyDebugReportCallbackEXT(m_instance, g_DebugReport, m_allocator);
#endif // APP_USE_VULKAN_DEBUG_REPORT

        vkDestroyDevice(m_device, m_allocator);
        vkDestroyInstance(m_instance, m_allocator);

        SDL_DestroyWindow(m_window);
        SDL_Quit();
    }

    void poll_events()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            bool quit_event = event.type == SDL_EVENT_QUIT;
            bool window_closed = event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(m_window);
            bool escape_pressed = event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE;
            if (quit_event || window_closed || escape_pressed)
            {
                m_is_active = false;
                continue;
            }
        }
    }

    bool handle_minimized() const
    {
        if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            return true;
        }
        return false;
    }

    void recreate_swapchain_if_needed()
    {
        // Resize swap chain?
        SDL_GetWindowSize(m_window, &m_width, &m_height);
        if (m_width > 0 && m_height > 0 && (m_rebuild_swapchain || m_main_window_data.Width != m_width || m_main_window_data.Height != m_height))
        {
            ImGui_ImplVulkan_SetMinImageCount(m_min_image_count);
            recreate_window();
            m_main_window_data.FrameIndex = 0;
            m_rebuild_swapchain = false;
        }
    }
    void new_frame()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }

    void draw_ui()
    {
        if (m_show_demo_window) ImGui::ShowDemoWindow(&m_show_demo_window);

        {
            static f32 f = 0.0f;
            static int counter = 0;

            ImGui::Begin("Hello, world!"); // Create a window called "Hello, world!" and append into it.

            ImGui::Text("This is some useful text.");            // Display some text (you can use a format strings too)
            ImGui::Checkbox("Demo Window", &m_show_demo_window); // Edit bools storing our window open/close state
            ImGui::Checkbox("Another Window", &m_show_another_window);

            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
            ImGui::ColorEdit3("clear color", reinterpret_cast<f32 *>(&m_clear_color));

            // Buttons return true when clicked (most widgets return true when edited/activated)
            if (ImGui::Button("Button")) counter++;
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();
        }

        if (m_show_another_window)
        {
            ImGui::Begin("Another Window", &m_show_another_window); // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me")) m_show_another_window = false;
            ImGui::End();
        }
    }

private:
    void setup_imgui_()
    {
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
        style.ScaleAllSizes(m_main_scale); // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
        style.FontScaleDpi = m_main_scale; // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

        // Setup Platform/Renderer backends
        ImGui_ImplSDL3_InitForVulkan(m_window);
        ImGui_ImplVulkan_InitInfo init_info = {};
        // init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass in your value of VkApplicationInfo::apiVersion, otherwise will default to header version.
        init_info.Instance = m_instance;
        init_info.PhysicalDevice = m_physical_device;
        init_info.Device = m_device;
        init_info.QueueFamily = m_queue_family;
        init_info.Queue = m_queue;
        init_info.PipelineCache = m_pipeline_cache;
        init_info.DescriptorPool = m_description_pool;
        init_info.MinImageCount = m_min_image_count;
        init_info.ImageCount = m_main_window_data.ImageCount;
        init_info.Allocator = m_allocator;
        init_info.PipelineInfoMain.RenderPass = m_main_window_data.RenderPass;
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
    }

    void setup_sdl_()
    {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) PANIC("Error: SDL_Init(): %s", SDL_GetError());

        // Create window with Vulkan graphics context
        m_main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        // TODO: Make those variables
        SDL_Window *window = SDL_CreateWindow("DSEngine", (int)(1280 * m_main_scale), (int)(800 * m_main_scale), window_flags);
        m_window = window;
        if (!m_window) PANIC("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
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

    void setup_vulkan_window_()
    {
        if (SDL_Vulkan_CreateSurface(m_window, m_instance, m_allocator, &m_surface) == 0)
        {
            PANIC("Failed to create Vulkan surface");
        }

        // Create Framebuffers
        SDL_GetWindowSize(m_window, &m_window_width, &m_window_height);
        m_main_window_data.Surface = m_surface;

        // Check for WSI support
        VkBool32 res;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physical_device, m_queue_family, m_surface, &res);
        if (res != VK_TRUE) PANIC("Error no WSI support on physical device 0");

        setup_vulkan_window_select_surface_format_();
        setup_vulkan_window_select_presentation_mode_();

        // Create SwapChain, RenderPass, Framebuffer, etc.
        m_main_window_data.Surface = m_surface;
        m_main_window_data.SurfaceFormat = m_surface_format;
        m_main_window_data.PresentMode = m_present_mode;
        DS_ASSERT(m_min_image_count >= 2);
        recreate_window();
        SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_ShowWindow(m_window);
    }

    void setup_vulkan_window_select_surface_format_()
    {
        constexpr std::array request_surface_image_format = {
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
            for (const auto &req_format : request_surface_image_format)
            {
                for (const auto &avail : avail_format)
                {
                    if (avail.format == req_format && avail.colorSpace == request_surface_color_space)
                    {
                        m_surface_format = avail;
                        return;
                    }
                }
            }
            m_surface_format = avail_format[0];
        }
    }

    void setup_vulkan_window_select_presentation_mode_()
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
};

// Main code
void main()
{
    EngineContext ctx{};
    ctx.setup();

    // Our state
    ctx.m_show_demo_window = true;
    ctx.m_show_another_window = false;

    // Main loop
    ctx.m_is_active = true;
    while (ctx.m_is_active)
    {
        ctx.poll_events();
        ctx.handle_minimized();
        ctx.recreate_swapchain_if_needed();
        ctx.new_frame();

        ctx.draw_ui();

        // Rendering
        ImGui::Render();
        ImDrawData *draw_data = ImGui::GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
        if (!is_minimized)
        {
            ctx.m_main_window_data.ClearValue.color.float32[0] = ctx.m_clear_color.x * ctx.m_clear_color.w;
            ctx.m_main_window_data.ClearValue.color.float32[1] = ctx.m_clear_color.y * ctx.m_clear_color.w;
            ctx.m_main_window_data.ClearValue.color.float32[2] = ctx.m_clear_color.z * ctx.m_clear_color.w;
            ctx.m_main_window_data.ClearValue.color.float32[3] = ctx.m_clear_color.w;
            ctx.render_frame();
            ctx.present_frame();
        }
    }

    ctx.cleanup();
}
} // namespace DSEngine