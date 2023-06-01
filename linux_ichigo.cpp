#include "common.hpp"
#include <cstdio>
#include "ichigo.hpp"
#include "thirdparty/imgui/imgui_impl_sdl2.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_vulkan.h>
#include "vulkan.hpp"

static SDL_Window *window;
static u32 previous_height = 1920;
static u32 previous_width = 1080;
static bool init_completed = false;

// TODO: These are global because we need to use them to calculate the play cursor delta if we are in WM_PAINT
// Maybe this will be resolved when we put our render in a separate thread?
static u64 last_play_cursor_pos = 0;

std::FILE *Ichigo::platform_open_file(const std::string &path, const std::string &mode) {
    return std::fopen(path.c_str(), mode.c_str());
}

// void init_dsound(HWND window) {
//     assert(SUCCEEDED(DirectSoundCreate8(nullptr, &direct_sound, nullptr)) &&
//            SUCCEEDED(direct_sound->SetCooperativeLevel(window, DSSCL_NORMAL)));
// }

// void realloc_dsound_buffer(u32 samples_per_second, u32 buffer_size) {
//     if (secondary_dsound_buffer)
//         secondary_dsound_buffer->Release();

//     WAVEFORMATEX wave_format = {};
//     wave_format.wFormatTag = WAVE_FORMAT_PCM;
//     wave_format.nChannels = 2;
//     wave_format.nSamplesPerSec = samples_per_second;
//     wave_format.wBitsPerSample = 16;
//     wave_format.nBlockAlign = wave_format.nChannels * wave_format.wBitsPerSample / 8;
//     wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
//     wave_format.cbSize = 0;

//     DSBUFFERDESC secondary_buffer_description = {};
//     secondary_buffer_description.dwSize = sizeof(secondary_buffer_description);
//     secondary_buffer_description.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_TRUEPLAYPOSITION;
//     secondary_buffer_description.dwBufferBytes = buffer_size;
//     secondary_buffer_description.lpwfxFormat = &wave_format;

//     IDirectSoundBuffer *query_secondary_dsound_buffer;

//     assert(SUCCEEDED(direct_sound->CreateSoundBuffer(&secondary_buffer_description,
//                                                      &query_secondary_dsound_buffer, nullptr)));
//     assert(SUCCEEDED(query_secondary_dsound_buffer->QueryInterface(
//         IID_IDirectSoundBuffer8, reinterpret_cast<void **>(&secondary_dsound_buffer))));

//     query_secondary_dsound_buffer->Release();
// }

u64 write_samples(u8 *samples, u64 bytes_to_write, u64 last_written_pos) {
    return 0;
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
    if (SDL_Vulkan_CreateSurface(window, Ichigo::vk_context.vk_instance, &vk_surface) != SDL_TRUE) {

    }

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
            Ichigo::must_realloc_sound_buffer = false;
        }

        if (Ichigo::current_song) {
        }

        switch (Ichigo::new_player_state) {
        case Ichigo::PlayerState::PLAYING: {
        } break;

        case Ichigo::PlayerState::PAUSED: {
        } break;

        case Ichigo::PlayerState::STOPPED: {
        } break;

        case Ichigo::PlayerState::NOP: {
        } break;
        }

        Ichigo::new_player_state = Ichigo::PlayerState::NOP;
    }
}
