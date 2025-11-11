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
    SDL_GetWindowSizeInPixels(window, &w, &h);
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

struct FrameContext
{
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkFence fence;
    VkImage backbuffer;
    VkImageView backbuffer_view;
    VkFramebuffer framebuffer;
};

struct FrameSemaphores
{
    VkSemaphore image_acquired;
    VkSemaphore render_complete;
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

    int m_width = 0;
    int m_height = 0;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSurfaceFormatKHR m_surface_format{};
    VkPresentModeKHR m_present_mode = VkPresentModeKHR::VK_PRESENT_MODE_MAX_ENUM_KHR;
    VkRenderPass m_render_pass = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    bool m_use_dynamic_rendering = false;
    bool m_clear_enabled = true;
    VkClearValue m_clear_value{};
    u32 m_frame_index = 0;
    u32 m_image_count = 0;
    u32 m_semaphore_count = 0;
    u32 m_semaphore_index = 0;
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
        vk_check(vkDeviceWaitIdle(m_device));

        ImGui_ImplVulkanH_Window tmp{};
        tmp.Surface = m_surface;
        tmp.SurfaceFormat = m_surface_format;
        tmp.PresentMode = m_present_mode;
        tmp.Width = m_window_width;
        tmp.Height = m_window_height;
        tmp.Swapchain = m_swapchain;
        tmp.RenderPass = m_render_pass;
        tmp.UseDynamicRendering = m_use_dynamic_rendering;
        tmp.ClearEnable = m_clear_enabled;
        tmp.ClearValue = m_clear_value;
        tmp.ImageCount = m_image_count;
        tmp.SemaphoreCount = m_semaphore_count;
        tmp.FrameIndex = m_frame_index;

        for (const auto &f : m_frames)
        {
            ImGui_ImplVulkanH_Frame fr{};
            fr.CommandPool = f.command_pool;
            fr.CommandBuffer = f.command_buffer;
            fr.Fence = f.fence;
            fr.Backbuffer = f.backbuffer;
            fr.BackbufferView = f.backbuffer_view;
            fr.Framebuffer = f.framebuffer;
            tmp.Frames.push_back(fr);
        }
        for (const auto &s : m_frame_semaphores)
        {
            ImGui_ImplVulkanH_FrameSemaphores sem{};
            sem.ImageAcquiredSemaphore = s.image_acquired;
            sem.RenderCompleteSemaphore = s.render_complete;
            tmp.FrameSemaphores.push_back(sem);
        }

        ImGui_ImplVulkanH_CreateOrResizeWindow(
            m_instance,
            m_physical_device,
            m_device,
            &tmp,
            m_queue_family,
            m_allocator,
            m_window_width,
            m_window_height,
            m_min_image_count,
            0);

        // Copy back results from ImGui window into our fields
        m_width = tmp.Width;
        m_height = tmp.Height;
        m_swapchain = tmp.Swapchain;
        m_render_pass = tmp.RenderPass;
        m_use_dynamic_rendering = tmp.UseDynamicRendering;
        m_clear_enabled = tmp.ClearEnable;
        m_clear_value = tmp.ClearValue;
        m_image_count = tmp.ImageCount;
        DS_ASSERT(m_image_count >= m_min_image_count);

        m_semaphore_count = tmp.SemaphoreCount;
        m_frame_index = tmp.FrameIndex;
        m_semaphore_index = 0;

        m_frames.clear();
        m_frames.reserve(static_cast<size_t>(tmp.ImageCount));
        for (int i = 0; i < tmp.Frames.Size; ++i)
        {
            const ImGui_ImplVulkanH_Frame &src = tmp.Frames[i];
            FrameContext dst{
                .command_pool = src.CommandPool,
                .command_buffer = src.CommandBuffer,
                .fence = src.Fence,
                .backbuffer = src.Backbuffer,
                .backbuffer_view = src.BackbufferView,
                .framebuffer = src.Framebuffer};
            m_frames.push_back(dst);
        }

        // Convert ImGui semaphores into our FrameSemaphores vector
        m_frame_semaphores.clear();
        m_frame_semaphores.reserve(static_cast<size_t>(tmp.SemaphoreCount));
        for (int i = 0; i < tmp.FrameSemaphores.Size; ++i)
        {
            const ImGui_ImplVulkanH_FrameSemaphores &src = tmp.FrameSemaphores[i];
            FrameSemaphores s{
                .image_acquired = src.ImageAcquiredSemaphore,
                .render_complete = src.RenderCompleteSemaphore};
            m_frame_semaphores.push_back(s);
        }
    }

    void render_frame()
    {
        VkSemaphore image_acquired_semaphore = m_frame_semaphores[m_semaphore_index].image_acquired;
        VkSemaphore render_complete_semaphore = m_frame_semaphores[m_semaphore_index].render_complete;
        VkResult err = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &m_frame_index);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) m_rebuild_swapchain = true;
        if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
        if (err != VK_SUBOPTIMAL_KHR) vk_check(err);
        if (m_frame_index >= m_frames.size())
        {
            m_rebuild_swapchain = true;
            return;
        }
        FrameContext *fd = &m_frames[m_frame_index];
        {
            vk_check(vkWaitForFences(m_device, 1, &fd->fence, VK_TRUE, UINT64_MAX));
            vk_check(vkResetFences(m_device, 1, &fd->fence));
        }
        {
            vk_check(vkResetCommandPool(m_device, fd->command_pool, 0));
            VkCommandBufferBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vk_check(vkBeginCommandBuffer(fd->command_buffer, &info));
        }
        {
            VkRenderPassBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            info.renderPass = m_render_pass;
            info.framebuffer = fd->framebuffer;
            info.renderArea.extent.width = static_cast<u32>(m_width);
            info.renderArea.extent.height = static_cast<u32>(m_height);
            info.clearValueCount = 1;
            info.pClearValues = &m_clear_value;
            vkCmdBeginRenderPass(fd->command_buffer, &info, VK_SUBPASS_CONTENTS_INLINE);
        }

        // Record dear imgui primitives into command buffer
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), fd->command_buffer);

        // Submit command buffer
        vkCmdEndRenderPass(fd->command_buffer);
        {
            VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            info.waitSemaphoreCount = 1;
            info.pWaitSemaphores = &image_acquired_semaphore;
            info.pWaitDstStageMask = &wait_stage;
            info.commandBufferCount = 1;
            info.pCommandBuffers = &fd->command_buffer;
            info.signalSemaphoreCount = 1;
            info.pSignalSemaphores = &render_complete_semaphore;

            vk_check(vkEndCommandBuffer(fd->command_buffer));
            vk_check(vkQueueSubmit(m_queue, 1, &info, fd->fence));
        }
    }

    void present_frame()
    {
        if (m_rebuild_swapchain) return;
        VkSemaphore render_complete_semaphore = m_frame_semaphores[m_semaphore_index].render_complete;
        VkPresentInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &render_complete_semaphore;
        info.swapchainCount = 1;
        info.pSwapchains = &m_swapchain;
        info.pImageIndices = &m_frame_index;
        VkResult err = vkQueuePresentKHR(m_queue, &info);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) m_rebuild_swapchain = true;
        if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
        if (err != VK_SUBOPTIMAL_KHR) vk_check(err);
        m_semaphore_index = (m_semaphore_index + 1) % m_semaphore_count;
    }

    void cleanup()
    {
        vk_check(vkDeviceWaitIdle(m_device));
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        vkDeviceWaitIdle(m_device);
        for (uint32_t i = 0; i < m_image_count; i++)
        {
            vkDestroyFence(m_device, m_frames[i].fence, m_allocator);
            vkFreeCommandBuffers(m_device, m_frames[i].command_pool, 1, &m_frames[i].command_buffer);
            vkDestroyCommandPool(m_device, m_frames[i].command_pool, m_allocator);
            m_frames[i].fence = VK_NULL_HANDLE;
            m_frames[i].command_buffer = VK_NULL_HANDLE;
            m_frames[i].command_pool = VK_NULL_HANDLE;

            vkDestroyImageView(m_device, m_frames[i].backbuffer_view, m_allocator);
            vkDestroyFramebuffer(m_device, m_frames[i].framebuffer, m_allocator);
        }
        for (uint32_t i = 0; i < m_semaphore_count; i++)
        {
            vkDestroySemaphore(m_device, m_frame_semaphores[i].image_acquired, m_allocator);
            vkDestroySemaphore(m_device, m_frame_semaphores[i].render_complete, m_allocator);
            m_frame_semaphores[i].image_acquired = VK_NULL_HANDLE;
            m_frame_semaphores[i].render_complete = VK_NULL_HANDLE;
        }
        m_frames.clear();
        m_frame_semaphores.clear();
        vkDestroyPipeline(m_device, m_pipeline, m_allocator);
        vkDestroyRenderPass(m_device, m_render_pass, m_allocator);
        vkDestroySwapchainKHR(m_device, m_swapchain, m_allocator);
        vkDestroySurfaceKHR(m_instance, m_surface, m_allocator);

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
        int fd_width{};
        int fd_height{};
        SDL_GetWindowSizeInPixels(m_window, &fd_width, &fd_height);
        const bool size_ok = (fd_width > 0 && fd_height > 0);
        const bool size_changed = (m_width != fd_width) || (m_height != fd_height);
        if (size_ok && (m_rebuild_swapchain || size_changed))
        {
            ImGui_ImplVulkan_SetMinImageCount(m_min_image_count);
            recreate_window();
            m_width = fd_width;
            m_height = fd_height;
            recreate_window();
            m_frame_index = 0;
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
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

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
        init_info.ImageCount = m_image_count;
        init_info.Allocator = m_allocator;
        init_info.PipelineInfoMain.RenderPass = m_render_pass;
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
        DS_ASSERT(m_instance != VK_NULL_HANDLE);

        setup_vulkan_find_physical_device_();
        DS_ASSERT(m_physical_device != VK_NULL_HANDLE);

        setup_vulkan_find_queue_family_();
        DS_ASSERT(m_queue_family != Constants::queue_family_uninitialised);

        setup_vulkan_logical_device_();
        DS_ASSERT(m_queue != VK_NULL_HANDLE);
        DS_ASSERT(m_device != VK_NULL_HANDLE);

        setup_vulkan_descriptor_pool_();
        DS_ASSERT(m_description_pool != VK_NULL_HANDLE);
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

    void setup_vulkan_find_physical_device_()
    {
        u32 gpu_count;
        vk_check(vkEnumeratePhysicalDevices(m_instance, &gpu_count, nullptr));
        DS_ASSERT(gpu_count > 0);

        ImVector<VkPhysicalDevice> gpus;
        gpus.resize(gpu_count);
        vk_check(vkEnumeratePhysicalDevices(m_instance, &gpu_count, gpus.Data));

        bool found_discrete = false;
        for (VkPhysicalDevice &device : gpus)
        {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(device, &properties);
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                m_physical_device = device;
                found_discrete = true;
                break;
            }
        }
        if (!found_discrete and (gpu_count > 0)) m_physical_device = gpus[0];
    }

    void setup_vulkan_find_queue_family_()
    {
        u32 count;
        vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &count, nullptr);
        ImVector<VkQueueFamilyProperties> queues_properties;
        queues_properties.resize(static_cast<int>(count));
        vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &count, queues_properties.Data);
        for (u32 i = 0; i < count; i++)
        {
            if (queues_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                m_queue_family = i;
                return;
            }
        }
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
        SDL_GetWindowSizeInPixels(m_window, &m_window_width, &m_window_height);

        // Check for WSI support
        VkBool32 res;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physical_device, m_queue_family, m_surface, &res);
        if (res != VK_TRUE) PANIC("Error no WSI support on physical device 0");

        setup_vulkan_window_select_surface_format_();
        setup_vulkan_window_select_presentation_mode_();

        // Create SwapChain, RenderPass, Framebuffer, etc.
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
        u32 num_availiable{};
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &num_availiable, nullptr);
        std::vector<VkSurfaceFormatKHR> availiable_formats(num_availiable);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &num_availiable, availiable_formats.data());

        // First check if only one format, VK_FORMAT_UNDEFINED, is available, which would imply that any format is available
        if (num_availiable == 1)
        {
            if (availiable_formats[0].format == VK_FORMAT_UNDEFINED)
            {
                VkSurfaceFormatKHR ret;
                ret.format = request_surface_image_format[0];
                ret.colorSpace = request_surface_color_space;
                m_surface_format = ret;
            }
            else
            {
                // No point in searching another format
                m_surface_format = availiable_formats[0];
            }
        }
        else
        {
            for (const auto &req_format : request_surface_image_format)
            {
                for (const auto &avail : availiable_formats)
                {
                    if (avail.format == req_format && avail.colorSpace == request_surface_color_space)
                    {
                        m_surface_format = avail;
                        return;
                    }
                }
            }
            m_surface_format = availiable_formats[0];
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
            ctx.m_clear_value.color.float32[0] = ctx.m_clear_color.x * ctx.m_clear_color.w;
            ctx.m_clear_value.color.float32[1] = ctx.m_clear_color.y * ctx.m_clear_color.w;
            ctx.m_clear_value.color.float32[2] = ctx.m_clear_color.z * ctx.m_clear_color.w;
            ctx.m_clear_value.color.float32[3] = ctx.m_clear_color.w;
            ctx.render_frame();
            ctx.present_frame();
        }
    }

    ctx.cleanup();
}
} // namespace DSEngine