#include "common.hpp"
#include <cstdio>
#include <cstring>
#include <sys/time.h>
#include "ichigo.hpp"
#include "thirdparty/imgui/imgui_impl_sdl2.h"
#include <SDL2/SDL_timer.h>
#include <ftw.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_vulkan.h>
#include <alsa/asoundlib.h>
#include "vulkan.hpp"

static SDL_Window *window;
static u32 previous_height = 1920;
static u32 previous_width = 1080;
static bool init_completed = false;

static snd_pcm_t *asound_handle = nullptr;
static snd_pcm_hw_params_t *asound_hw_params = nullptr;

static u64 last_play_cursor_pos = 0;
static u64 asound_period_size = 0;
static u64 asound_buffer_size = 0;

static u64 last_tick_time = 0;

std::FILE *Ichigo::platform_open_file(const std::string &path, const std::string &mode) {
    return std::fopen(path.c_str(), mode.c_str());
}

// TODO: Retarded
static std::vector<std::string> g_files;
static std::vector<const char *> g_extension_filter;

int visit_node(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
    if (tflag != FTW_F)
        return 0;

    // Find the last period in the file name
    u64 period_index = 0;
    for (u64 current_index = 0; fpath[current_index] != '\0'; ++current_index) {
        if (fpath[current_index] == '.')
            period_index = current_index;
    }

    for (auto ext : g_extension_filter) {
        if (std::strcmp(&fpath[period_index + 1], ext) == 0) {
            g_files.emplace_back(fpath);
            return 0;
        }
    }

    return 0;
}

std::vector<std::string> Ichigo::platform_recurse_directory(const std::string &path, const std::vector<const char *> extension_filter) {
    g_files.clear();
    g_extension_filter = extension_filter;
    nftw(path.c_str(), visit_node, 20, FTW_DEPTH);
    return g_files;
}

void Ichigo::platform_playback_set_state(const Ichigo::PlayerState state) {
    switch (state) {
        case Ichigo::PlayerState::PLAYING: {
            if (asound_handle)
                snd_pcm_pause(asound_handle, 0);
        } break;
        case Ichigo::PlayerState::PAUSED: {
            if (asound_handle)
                snd_pcm_pause(asound_handle, 1);
        } break;
        case Ichigo::PlayerState::STOPPED: {
            if (asound_handle) {
                snd_pcm_drop(asound_handle);
                snd_pcm_close(asound_handle);
                asound_handle = nullptr;
            }
        } break;
    }
}

static u64 write_samples(snd_pcm_sframes_t frames) {
    u64 buffer_size = frames * sizeof(i16) * Ichigo::current_song->channel_count;
    u8 *sample_buffer = new u8[buffer_size]{};

    Ichigo::fill_sample_buffer(sample_buffer, buffer_size);
    i32 err = 0;
again:
    if ((err = snd_pcm_writei(asound_handle, sample_buffer, frames)) < 0) {
        std::printf("ATTEMPTING RECOVER\n");
        snd_pcm_recover(asound_handle, err, 0);
        goto again;
    }

    delete[] sample_buffer;
    return 0;
}

static void try_write_samples() {
    for (snd_pcm_sframes_t deliverable_frames = snd_pcm_avail(asound_handle); deliverable_frames >= asound_period_size; deliverable_frames = snd_pcm_avail_update(asound_handle))
        write_samples(asound_period_size);
}

void Ichigo::platform_playback_reset_for_seek(bool should_play) {
    snd_pcm_drop(asound_handle);
    snd_pcm_prepare(asound_handle);
    try_write_samples();

    if (!should_play)
        snd_pcm_pause(asound_handle, 1);
}

static void realloc_asound_buffer(u32 samples_per_second) {
    if (asound_handle) {
        snd_pcm_drop(asound_handle);
        snd_pcm_close(asound_handle);
        asound_handle = nullptr;
    }

    u64 buffer_time_in_us = 8 * 1000 * 1000;
    u64 period_time_in_us = 1000 * 1000;
    [[maybe_unused]] i32 dir = 0;
    i32 err = snd_pcm_open(&asound_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    std::printf("REALLOC ERROR=%d\n", err);
    assert(err >= 0);
    assert(snd_pcm_hw_params_malloc(&asound_hw_params) >= 0);
    assert(snd_pcm_hw_params_any(asound_handle, asound_hw_params) >= 0);
    assert(snd_pcm_hw_params_set_access(asound_handle, asound_hw_params, SND_PCM_ACCESS_RW_INTERLEAVED) >= 0);
    assert(snd_pcm_hw_params_set_format(asound_handle, asound_hw_params, SND_PCM_FORMAT_S16_LE) >= 0);
    assert(snd_pcm_hw_params_set_channels(asound_handle, asound_hw_params, 2) >= 0); // TODO: More than 2 channels?
    assert(snd_pcm_hw_params_set_rate_near(asound_handle, asound_hw_params, &samples_per_second, 0) >= 0);

    assert(snd_pcm_hw_params_set_buffer_size_near(asound_handle, asound_hw_params, &buffer_time_in_us) == 0);
    assert(snd_pcm_hw_params_get_buffer_size(asound_hw_params, &asound_buffer_size) == 0);

    assert(snd_pcm_hw_params_set_period_size_near(asound_handle, asound_hw_params, &period_time_in_us, &dir) == 0);
    assert(snd_pcm_hw_params_get_period_size(asound_hw_params, &asound_period_size, &dir) == 0);

    assert(snd_pcm_hw_params(asound_handle, asound_hw_params) >= 0);
    snd_pcm_hw_params_free(asound_hw_params);
    asound_hw_params = nullptr;

    std::printf("determined period size = %llu determined buffer size = %llu\n", asound_period_size, asound_buffer_size);

    assert(snd_pcm_prepare(asound_handle) >= 0);

    try_write_samples();
}


i32 main(i32, char **) {
    assert(SDL_Init(SDL_INIT_VIDEO) >= 0);

    window = SDL_CreateWindow("Ichigo", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1500, 1000, SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);

    // static const char *extensions[] = {"VK_KHR_surface", "VK_KHR_xcb_surface"};
    const char *extensions[10] = {};
    u32 num_extensions = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &num_extensions, nullptr);
    SDL_Vulkan_GetInstanceExtensions(window, &num_extensions, extensions);


    Ichigo::vk_context.init(extensions, num_extensions);

    VkSurfaceKHR vk_surface;
    assert(SDL_Vulkan_CreateSurface(window, Ichigo::vk_context.vk_instance, &vk_surface));

    Ichigo::vk_context.surface = vk_surface;
    Ichigo::init();


    // Platform init
    ImGui_ImplSDL2_InitForVulkan(window);
    init_completed = true;

    u64 last_written_pos = 0;

    // Main loop
    for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                std::printf("deinit() now\n");
                Ichigo::deinit();
                return 0;
            }
        }

        ImGui_ImplSDL2_NewFrame();

        u64 new_tick_time = SDL_GetTicks64();
        u64 delta = 0;
        if (asound_handle && Ichigo::get_player_state() == Ichigo::PlayerState::PLAYING) {
            delta = new_tick_time - last_tick_time;
            delta *= (Ichigo::current_song->sample_rate * Ichigo::current_song->channel_count * sizeof(i16)) / 1000;
        }

        last_tick_time = new_tick_time;

        Ichigo::do_frame(previous_width, previous_height, 1.0, delta);

        if (Ichigo::must_realloc_sound_buffer) {
            std::printf("realloc sound buffer\n");
            realloc_asound_buffer(Ichigo::current_song->sample_rate);
            Ichigo::must_realloc_sound_buffer = false;
        }

        if (Ichigo::current_song && Ichigo::get_player_state() == Ichigo::PlayerState::PLAYING)
            try_write_samples();
    }
}
