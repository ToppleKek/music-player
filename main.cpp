#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "common.hpp"
#include "db.hpp"
#include "ichigo.hpp"
#include "tags.hpp"
#include "vulkan/vulkan_core.h"

// extern "C" {
// #include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h>
// #include <libavutil/frame.h>
// #include <libavutil/mem.h>
// #include <libavutil/opt.h>
// #include <libswresample/swresample.h>
// }

#include "vulkan.hpp"

#include "thirdparty/imgui/imgui.h"
#include "thirdparty/imgui/imgui_impl_vulkan.h"
// #include "thirdparty/imgui/imgui_impl_win32.h"

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

#define EMBED(FNAME, VNAME)                                                               \
    __asm__(                                                                              \
        ".section .rodata    \n"                                                          \
        ".global " #VNAME "    \n.align 16\n" #VNAME ":    \n.incbin \"" FNAME            \
        "\"       \n"                                                                     \
        ".global " #VNAME "_end\n.align 1 \n" #VNAME                                      \
        "_end:\n.byte 1                   \n"                                             \
        ".global " #VNAME "_len\n.align 16\n" #VNAME "_len:\n.int " #VNAME "_end-" #VNAME \
        "\n"                                                                              \
        ".align 16           \n.text    \n");                                             \
    extern const __declspec(align(16)) unsigned char VNAME[];                             \
    extern const __declspec(align(16)) unsigned char *const VNAME##_end;                  \
    extern const unsigned int VNAME##_len;

extern "C" {
EMBED("noto.ttf", noto_font)
}

static float scale = 1;
static ImGuiStyle initial_style;
static ImFontConfig font_config;

static u64 play_cursor = 0;
static Ichigo::PlayerState player_state = Ichigo::PlayerState::STOPPED;

static drmp3 mp3;
static drflac *flac;

static bool drmp3_initialized = false;
static bool drflac_initialized = false;
static std::FILE *open_music_file = nullptr;

IchigoVulkan::Context Ichigo::vk_context = {};
bool Ichigo::must_rebuild_swapchain = false;
bool Ichigo::must_realloc_sound_buffer = false;
bool Ichigo::current_song_has_data = false;
Ichigo::Song *Ichigo::current_song = nullptr;

static void check_vk_result(VkResult err) {
    if (err == 0)
        return;
    std::printf("[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

static void init_avcodec_for_song(Ichigo::Song *song) {
    switch (song->format) {
    case Ichigo::SongFormat::MP3: {
        // FIXME: Dubious.
        // std::rewind(song->file);
        std::printf("Attempting to init drmp3 with path=%s\n", song->path.c_str());
        open_music_file = Ichigo::platform_open_file(song->path, "rb");
        assert(drmp3_ichigo_init_file(&mp3, open_music_file, nullptr));
        // assert(drmp3_init_file(&mp3, song->path.c_str(), nullptr));
        u64 length_in_bytes = drmp3_get_pcm_frame_count(&mp3) * mp3.channels * sizeof(i16);
        song->sample_rate = mp3.sampleRate;
        song->channel_count = mp3.channels;
        song->duration = static_cast<double>(length_in_bytes) / (song->sample_rate * mp3.channels * sizeof(i16)) * 1000.0;
        song->duration_in_bytes = length_in_bytes;
        std::printf("duration=%llu in_bytes=%llu\n", song->duration, song->duration_in_bytes);
        Ichigo::current_song_has_data = true;
        drmp3_initialized = true;
    } break;

    case Ichigo::SongFormat::FLAC: {
        // FIXME: Dubious.
        // std::rewind(song->file);

        open_music_file = Ichigo::platform_open_file(song->path, "rb");
        assert(flac = drflac_ichigo_open_file(open_music_file, nullptr));
        // assert(flac = drflac_open_file(song->path.c_str(), nullptr));

        u64 length_in_bytes = flac->totalPCMFrameCount * flac->channels * sizeof(i16);
        song->sample_rate = flac->sampleRate;
        song->channel_count = flac->channels;
        song->duration = static_cast<double>(length_in_bytes) / (song->sample_rate * song->channel_count * sizeof(i16)) * 1000.0;
        song->duration_in_bytes = length_in_bytes;
        std::printf("duration=%llu in_bytes=%llu\n", song->duration, song->duration_in_bytes);
        Ichigo::current_song_has_data = true;
        drflac_initialized = true;
    } break;
    }
}

static void deinit_avcodec() {
    if (drmp3_initialized)
        drmp3_ichigo_uninit(&mp3);

    if (drflac_initialized)
        drflac_free(flac, nullptr);

    drmp3_initialized = false;
    drflac_initialized = false;
    Ichigo::current_song_has_data = false;

    if (open_music_file) {
        std::fclose(open_music_file);
        open_music_file = nullptr;
    }
}

u64 Ichigo::fill_sample_buffer(u8 *buffer, u64 buffer_size) {
    if (!Ichigo::current_song)
        return 0;

    const u64 num_frames = buffer_size / (Ichigo::current_song->channel_count * sizeof(i16));
    u64 frames_read = 0;

    if (Ichigo::current_song->format == Ichigo::SongFormat::MP3)
        frames_read = drmp3_read_pcm_frames_s16(&mp3, num_frames, reinterpret_cast<i16 *>(buffer));
    else if (Ichigo::current_song->format == Ichigo::SongFormat::FLAC)
        frames_read = drflac_read_pcm_frames_s16(flac, num_frames, reinterpret_cast<i16 *>(buffer));
    // Return silence if we did not read any data

    if (frames_read == 0)
        std::memset(buffer, 0, buffer_size);

    return frames_read;
}

static std::string s_to_mmss(u32 sec) {
    u32 m = sec / 60, s = sec % 60;
    return (m < 10 ? "0" + std::to_string(m) : std::to_string(m)) + ":" + (s < 10 ? "0" + std::to_string(s) : std::to_string(s));
}

static void frame_present() {
    if (Ichigo::must_rebuild_swapchain)
        return;
    VkSemaphore render_complete_semaphore =
        Ichigo::vk_context.imgui_window_data.FrameSemaphores[Ichigo::vk_context.imgui_window_data.SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_complete_semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &Ichigo::vk_context.imgui_window_data.Swapchain;
    info.pImageIndices = &Ichigo::vk_context.imgui_window_data.FrameIndex;
    VkResult err = vkQueuePresentKHR(Ichigo::vk_context.queue, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        Ichigo::must_rebuild_swapchain = true;
        return;
    }
    check_vk_result(err);
    Ichigo::vk_context.imgui_window_data.SemaphoreIndex =
        (Ichigo::vk_context.imgui_window_data.SemaphoreIndex + 1) % Ichigo::vk_context.imgui_window_data.ImageCount;  // Now we can use the next set of semaphores
}

static void frame_render(ImDrawData *draw_data) {
    VkSemaphore image_acquired_semaphore =
        Ichigo::vk_context.imgui_window_data.FrameSemaphores[Ichigo::vk_context.imgui_window_data.SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore =
        Ichigo::vk_context.imgui_window_data.FrameSemaphores[Ichigo::vk_context.imgui_window_data.SemaphoreIndex].RenderCompleteSemaphore;

    auto err = vkAcquireNextImageKHR(Ichigo::vk_context.logical_device, Ichigo::vk_context.imgui_window_data.Swapchain, UINT64_MAX, image_acquired_semaphore,
                                     VK_NULL_HANDLE, &Ichigo::vk_context.imgui_window_data.FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        Ichigo::must_rebuild_swapchain = true;
        return;
    }
    check_vk_result(err);

    ImGui_ImplVulkanH_Frame *fd = &Ichigo::vk_context.imgui_window_data.Frames[Ichigo::vk_context.imgui_window_data.FrameIndex];

    err = vkWaitForFences(Ichigo::vk_context.logical_device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
    check_vk_result(err);

    err = vkResetFences(Ichigo::vk_context.logical_device, 1, &fd->Fence);
    check_vk_result(err);

    err = vkResetCommandPool(Ichigo::vk_context.logical_device, fd->CommandPool, 0);
    check_vk_result(err);
    VkCommandBufferBeginInfo buffer_begin_info = {};
    buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    buffer_begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    err = vkBeginCommandBuffer(fd->CommandBuffer, &buffer_begin_info);
    check_vk_result(err);

    VkRenderPassBeginInfo render_begin_info = {};
    render_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_begin_info.renderPass = Ichigo::vk_context.imgui_window_data.RenderPass;
    render_begin_info.framebuffer = fd->Framebuffer;
    render_begin_info.renderArea.extent.width = Ichigo::vk_context.imgui_window_data.Width;
    render_begin_info.renderArea.extent.height = Ichigo::vk_context.imgui_window_data.Height;
    render_begin_info.clearValueCount = 1;
    render_begin_info.pClearValues = &Ichigo::vk_context.imgui_window_data.ClearValue;
    vkCmdBeginRenderPass(fd->CommandBuffer, &render_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);
    vkCmdEndRenderPass(fd->CommandBuffer);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_acquired_semaphore;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &fd->CommandBuffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_complete_semaphore;

    err = vkEndCommandBuffer(fd->CommandBuffer);
    check_vk_result(err);
    err = vkQueueSubmit(Ichigo::vk_context.queue, 1, &submit_info, fd->Fence);
    check_vk_result(err);
}

static void change_song_and_play(Ichigo::Song *new_song) {
    player_state = Ichigo::PlayerState::PLAYING;
    Ichigo::platform_playback_set_state(player_state);
    play_cursor = 0;
    deinit_avcodec();
    init_avcodec_for_song(new_song);

    Ichigo::current_song = new_song;
    Ichigo::must_realloc_sound_buffer = true;
}

void Ichigo::do_frame(u32 window_width, u32 window_height, float dpi_scale, u64 play_cursor_delta) {
    if (Ichigo::must_rebuild_swapchain) {
        std::printf("Rebuilding swapchain\n");
        ImGui_ImplVulkan_SetMinImageCount(2);
        ImGui_ImplVulkanH_CreateOrResizeWindow(Ichigo::vk_context.vk_instance, Ichigo::vk_context.selected_gpu, Ichigo::vk_context.logical_device,
                                               &Ichigo::vk_context.imgui_window_data, Ichigo::vk_context.queue_family_index, nullptr, window_width, window_height, 2);

        Ichigo::vk_context.imgui_window_data.FrameIndex = 0;
        Ichigo::must_rebuild_swapchain = false;
    }

    if (dpi_scale != scale) {
        std::printf("scaling to scale=%f\n", dpi_scale);
        auto io = ImGui::GetIO();
        {
            io.Fonts->Clear();
            io.Fonts->AddFontFromMemoryTTF((void *) noto_font, noto_font_len, static_cast<i32>(18 * dpi_scale), &font_config, io.Fonts->GetGlyphRangesJapanese());

            // io.Fonts->AddFontFromFileTTF("noto.ttf", static_cast<i32>(18 * dpi_scale), nullptr,
            // io.Fonts->GetGlyphRangesJapanese());
            io.Fonts->Build();

            vkQueueWaitIdle(Ichigo::vk_context.queue);
            ImGui_ImplVulkan_DestroyFontsTexture();

            // Upload fonts to GPU
            VkCommandPool command_pool = Ichigo::vk_context.imgui_window_data.Frames[Ichigo::vk_context.imgui_window_data.FrameIndex].CommandPool;
            VkCommandBufferAllocateInfo allocate_info = {};
            allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocate_info.commandPool = command_pool;
            allocate_info.commandBufferCount = 1;
            allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

            VkCommandBuffer command_buffer = VK_NULL_HANDLE;
            assert(vkAllocateCommandBuffers(Ichigo::vk_context.logical_device, &allocate_info, &command_buffer) == VK_SUCCESS);

            auto err = vkResetCommandPool(Ichigo::vk_context.logical_device, command_pool, 0);
            check_vk_result(err);
            VkCommandBufferBeginInfo begin_info = {};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            err = vkBeginCommandBuffer(command_buffer, &begin_info);
            check_vk_result(err);

            ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

            VkSubmitInfo end_info = {};
            end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            end_info.commandBufferCount = 1;
            end_info.pCommandBuffers = &command_buffer;
            err = vkEndCommandBuffer(command_buffer);
            check_vk_result(err);
            err = vkQueueSubmit(Ichigo::vk_context.queue, 1, &end_info, VK_NULL_HANDLE);
            check_vk_result(err);

            err = vkDeviceWaitIdle(Ichigo::vk_context.logical_device);
            check_vk_result(err);
            ImGui_ImplVulkan_DestroyFontUploadObjects();

            vkFreeCommandBuffers(Ichigo::vk_context.logical_device, command_pool, 1, &command_buffer);
        }
        // ImGui::GetIO().FontGlobalScale = dpi_scale / 2;
        ImGui::HACK_SetStyle(initial_style);
        ImGui::GetStyle().ScaleAllSizes(dpi_scale);
        scale = dpi_scale;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    unsigned long seconds = 0;
    if (Ichigo::current_song) {
        play_cursor += play_cursor_delta;
        seconds = play_cursor / (Ichigo::current_song->sample_rate * Ichigo::current_song->channel_count * sizeof(i16));
    }

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("main_window", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);
    ImGui::Text("FPS=%.1f", ImGui::GetIO().Framerate);

    // if (ImGui::Button("Anata"))
    //     change_song_and_play(&anata);

    // if (ImGui::Button("Reflection"))
    //     change_song_and_play(&reflection);

    // if (ImGui::Button("Kira"))
    //     change_song_and_play(&kira);

    ImGui::BeginChild("play controls", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - ImGui::GetTextLineHeightWithSpacing() * 4));

    if (ImGui::BeginTable("songs", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti)) {
        ImGui::TableSetupColumn("Title", 0);
        ImGui::TableSetupColumn("Artist", 0);
        ImGui::TableSetupColumn("Album", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        ImGuiTableSortSpecs *sort_specs;
        if ((sort_specs = ImGui::TableGetSortSpecs()) && sort_specs->SpecsDirty) {
        }

        i32 selected_song = -1;
        u64 size = IchigoDB::size();

        for (u32 i = 0; i < size; ++i) {
            Ichigo::Song *song = IchigoDB::song(i);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(song->tag.title.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
                selected_song = i;

            ImGui::TableNextColumn();
            ImGui::Text("%s", song->tag.artist.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", song->tag.album.c_str());
        }

        ImGui::EndTable();

        if (selected_song != -1) {
            std::printf("selected song changed to %d\n", selected_song);
            change_song_and_play(IchigoDB::song(selected_song));
        }
    }

    ImGui::EndChild();
    ImGui::BeginGroup();
    if (Ichigo::current_song)
        ImGui::Text("%s - %s on %s", current_song->tag.artist.c_str(), current_song->tag.title.c_str(), current_song->tag.album.c_str());
    else
        ImGui::Text("Not playing");

    ImGui::Text("%s", s_to_mmss(seconds).c_str());

    // TODO: Only perform seek when the seek head is dropped? This seeks whenever you even slightly touch the seek head. Maybe we can fix this when we create our own seek
    // widget.
    if (ImGui::SliderInt("Seek", reinterpret_cast<i32 *>(&seconds), 0, Ichigo::current_song ? Ichigo::current_song->duration / 1000 : 0)) {
        u64 seek_frame = seconds * Ichigo::current_song->sample_rate;
        switch (Ichigo::current_song->format) {
        case Ichigo::SongFormat::MP3: {
            drmp3_seek_to_pcm_frame(&mp3, seek_frame);
        } break;
        case Ichigo::SongFormat::FLAC: {
            drflac_seek_to_pcm_frame(flac, seek_frame);
        } break;
        }

        play_cursor = seek_frame * Ichigo::current_song->channel_count * sizeof(i16);
        Ichigo::platform_playback_reset_for_seek(player_state == Ichigo::PlayerState::PLAYING);
    }

    if (ImGui::Button("Play") && player_state != Ichigo::PlayerState::PLAYING && Ichigo::current_song) {
        player_state = Ichigo::PlayerState::PLAYING;
        Ichigo::platform_playback_set_state(player_state);
    }

    ImGui::SameLine();

    if (ImGui::Button("Pause") && player_state != Ichigo::PlayerState::PAUSED && Ichigo::current_song) {
        player_state = Ichigo::PlayerState::PAUSED;
        Ichigo::platform_playback_set_state(player_state);
    }

    ImGui::SameLine();

    if (ImGui::Button("Stop") && player_state != Ichigo::PlayerState::STOPPED && Ichigo::current_song) {
        player_state = Ichigo::PlayerState::STOPPED;
        Ichigo::platform_playback_set_state(player_state);
        deinit_avcodec();
        Ichigo::current_song = {};
        play_cursor = 0;
    }

    ImGui::EndGroup();
    ImGui::End();

    // Stop current song when the play cursor plays over the end of the song
    // TODO: This should play the next song in the queue whenever that is implemented
    if (player_state == Ichigo::PlayerState::PLAYING && Ichigo::current_song && seconds >= (current_song->duration / 1000.0)) {
        std::printf("STOPPING\n");
        player_state = Ichigo::PlayerState::STOPPED;
        Ichigo::platform_playback_set_state(player_state);
        deinit_avcodec();
        Ichigo::current_song = {};
        play_cursor = 0;
    }

    ImGui::Render();
    ImDrawData *draw_data = ImGui::GetDrawData();
    bool minimized = draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f;
    if (!minimized) {
        Ichigo::vk_context.imgui_window_data.ClearValue.color.float32[0] = 0.5;
        Ichigo::vk_context.imgui_window_data.ClearValue.color.float32[1] = 0.5;
        Ichigo::vk_context.imgui_window_data.ClearValue.color.float32[2] = 0.5;
        Ichigo::vk_context.imgui_window_data.ClearValue.color.float32[3] = 1;

        frame_render(draw_data);
        frame_present();
    }
}

void Ichigo::init() {
    font_config.FontDataOwnedByAtlas = false;
    font_config.OversampleH = 2;
    font_config.OversampleV = 2;
    font_config.RasterizerMultiply = 1.5f;

    VkBool32 res;
    vkGetPhysicalDeviceSurfaceSupportKHR(Ichigo::vk_context.selected_gpu, Ichigo::vk_context.queue_family_index, Ichigo::vk_context.surface, &res);
    assert(res == VK_TRUE);

    Ichigo::vk_context.imgui_window_data.Surface = Ichigo::vk_context.surface;
    const VkFormat request_surface_image_format[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
    const VkColorSpaceKHR request_surface_color_space = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    // TODO: remove Macros
    Ichigo::vk_context.imgui_window_data.SurfaceFormat =
        ImGui_ImplVulkanH_SelectSurfaceFormat(Ichigo::vk_context.selected_gpu, Ichigo::vk_context.imgui_window_data.Surface, request_surface_image_format,
                                              (size_t) IM_ARRAYSIZE(request_surface_image_format), request_surface_color_space);

    // VkPresentModeKHR present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    Ichigo::vk_context.imgui_window_data.PresentMode =
        ImGui_ImplVulkanH_SelectPresentMode(Ichigo::vk_context.selected_gpu, Ichigo::vk_context.imgui_window_data.Surface, &present_mode, 1);
    ImGui_ImplVulkanH_CreateOrResizeWindow(Ichigo::vk_context.vk_instance, Ichigo::vk_context.selected_gpu, Ichigo::vk_context.logical_device,
                                           &Ichigo::vk_context.imgui_window_data, Ichigo::vk_context.queue_family_index, nullptr, 1920, 1080, 2);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = Ichigo::vk_context.vk_instance;
    init_info.PhysicalDevice = Ichigo::vk_context.selected_gpu;
    init_info.Device = Ichigo::vk_context.logical_device;
    init_info.QueueFamily = Ichigo::vk_context.queue_family_index;
    init_info.Queue = Ichigo::vk_context.queue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = Ichigo::vk_context.descriptor_pool;
    init_info.Subpass = 0;
    init_info.MinImageCount = 2;
    init_info.ImageCount = Ichigo::vk_context.imgui_window_data.ImageCount;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init(&init_info, Ichigo::vk_context.imgui_window_data.RenderPass);

    {
        auto io = ImGui::GetIO();
        // io.Fonts->AddFontFromFileTTF("noto.ttf", 18, nullptr, io.Fonts->GetGlyphRangesJapanese());
        io.Fonts->AddFontFromMemoryTTF((void *) noto_font, noto_font_len, 18, &font_config, io.Fonts->GetGlyphRangesJapanese());
        // io.Fonts->AddFontFromFileTTF("noto.ttf", 18);

        // Upload fonts to GPU
        VkCommandPool command_pool = Ichigo::vk_context.imgui_window_data.Frames[Ichigo::vk_context.imgui_window_data.FrameIndex].CommandPool;
        VkCommandBuffer command_buffer = Ichigo::vk_context.imgui_window_data.Frames[Ichigo::vk_context.imgui_window_data.FrameIndex].CommandBuffer;

        auto err = vkResetCommandPool(Ichigo::vk_context.logical_device, command_pool, 0);
        check_vk_result(err);
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(command_buffer, &begin_info);
        check_vk_result(err);

        ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

        VkSubmitInfo end_info = {};
        end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        end_info.commandBufferCount = 1;
        end_info.pCommandBuffers = &command_buffer;
        err = vkEndCommandBuffer(command_buffer);
        check_vk_result(err);
        err = vkQueueSubmit(Ichigo::vk_context.queue, 1, &end_info, VK_NULL_HANDLE);
        check_vk_result(err);

        err = vkDeviceWaitIdle(Ichigo::vk_context.logical_device);
        check_vk_result(err);
        ImGui_ImplVulkan_DestroyFontUploadObjects();
        // io.Fonts->Build();
    }

    initial_style = ImGui::GetStyle();

    IchigoDB::init_for_path("Z:/syncthing/Music Library");
    // IchigoDB::init_for_path("./full library");
    // IchigoDB::init_for_path("./fixing");
}
