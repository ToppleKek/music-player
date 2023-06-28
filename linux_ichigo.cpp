#include "common.hpp"
#include <cstdio>
#include "ichigo.hpp"
#include "thirdparty/imgui/imgui_impl_sdl2.h"
#include <SDL2/SDL_timer.h>
#include <ftw.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_vulkan.h>
#include <sys/asoundlib.h>
#include "vulkan.hpp"

static SDL_Window *window;
static u32 previous_height = 1920;
static u32 previous_width = 1080;
static bool init_completed = false;

static snd_pcm_t *asound_handle = nullptr;
static snd_pcm_hw_params_t *asound_hw_params = nullptr;

// TODO: These are global because we need to use them to calculate the play cursor delta if we are in WM_PAINT
// Maybe this will be resolved when we put our render in a separate thread?
static u64 last_play_cursor_pos = 0;

std::FILE *Ichigo::platform_open_file(const std::string &path, const std::string &mode) {
    return std::fopen(path.c_str(), mode.c_str());
}

// TODO: We should probably change this API for every platform
static std::vector<std::string> files;

int visit_node(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
    files.emplace_back(fpath);
    return 0;
}

std::vector<std::string> Ichigo::platform_recurse_directory(const std::string &path) {
    nftw(path.c_str(), visit_node, 20, FTW_DEPTH);
    return files;
}

// TODO: STUB!
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

// TODO: STUB!
void Ichigo::platform_playback_reset_for_seek(bool should_play) {

}

u64 write_samples(snd_pcm_sframes_t frames) {
    u64 buffer_size = frames * sizeof(i16) * Ichigo::current_song->channel_count;
    static u8 sample_buffer[200000] = {};

    assert(buffer_size <= 200000);

    Ichigo::fill_sample_buffer(sample_buffer, buffer_size);
    snd_pcm_writei(asound_handle, sample_buffer, frames);
    return 0;
}

static void realloc_asound_buffer(u32 samples_per_second, u32 buffer_size_in_frames) {
    if (asound_handle) {
        snd_pcm_drop(asound_handle);
        snd_pcm_close(asound_handle);
        asound_handle = nullptr;
    }

    assert(snd_pcm_open(&asound_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) >= 0);
    assert(snd_pcm_hw_params_malloc(&asound_hw_params) >= 0);
    assert(snd_pcm_hw_params_any(asound_handle, asound_hw_params) >= 0);
    assert(snd_pcm_hw_params_set_access(asound_handle, asound_hw_params, SND_PCM_ACCESS_RW_INTERLEAVED) >= 0);
    assert(snd_pcm_hw_params_set_format(asound_handle, asound_hw_params, SND_PCM_FORMAT_S16_LE) >= 0);
    assert(snd_pcm_hw_params_set_channels(asound_handle, asound_hw_params, 2) >= 0); // TODO: More than 2 channels?
    assert(snd_pcm_hw_params_set_rate_near(asound_handle, asound_hw_params, &samples_per_second, 0) >= 0);

    assert(snd_pcm_hw_params(asound_handle, asound_hw_params) >= 0);
    snd_pcm_hw_params_free(asound_hw_params);
    asound_hw_params = nullptr;

    assert(snd_pcm_prepare(asound_handle) >= 0);

    snd_pcm_sframes_t pcm_frames_available = snd_pcm_avail_update(asound_handle);
    write_samples(pcm_frames_available);
}

i32 main(i32, char **) {
    assert(SDL_Init(SDL_INIT_VIDEO) >= 0);

    window = SDL_CreateWindow("Music Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1920, 1080, SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);

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
    u8 samples[400000] = {};

    // Main loop
    for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                return 0;
        }

        ImGui_ImplSDL2_NewFrame();

        Ichigo::do_frame(previous_width, previous_height, 1.0,
                         0);

        if (Ichigo::must_realloc_sound_buffer) {
            std::printf("realloc sound buffer\n");
            realloc_asound_buffer(Ichigo::current_song->sample_rate, Ichigo::current_song->sample_rate * Ichigo::current_song->channel_count * 8);
            Ichigo::must_realloc_sound_buffer = false;
        }

        if (Ichigo::current_song) {
            snd_pcm_sframes_t deliverable_frames = snd_pcm_avail_update(asound_handle);
            if (deliverable_frames > 0)
                write_samples(deliverable_frames);
        }
    }
}
