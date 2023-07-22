#define _CRT_SECURE_NO_WARNINGS
#include "common.hpp"
#define VK_USE_PLATFORM_WIN32_KHR
#include <cstdio>
#include "ichigo.hpp"
#include "thirdparty/imgui/imgui_impl_win32.h"

#define WIN32_LEAN_AND_MEAN
#include <dsound.h>
#include <mmeapi.h>
#include <mmreg.h>
#include <windows.h>


static HWND window_handle;
static IDirectSoundBuffer8 *secondary_dsound_buffer = nullptr;
static LPDIRECTSOUND8 direct_sound;
static bool init_completed = false;
static u32 previous_height = 1920;
static u32 previous_width = 1080;
static bool in_sizing_loop = false;
static u64 last_written_pos = 0;
static u8 samples[400000] = {};
static Ichigo::PlayerState play_state = Ichigo::PlayerState::STOPPED;
static bool play_state_dirty_flag = false;

// TODO: These are global because we need to use them to calculate the play cursor delta if we are in WM_PAINT
// Maybe this will be resolved when we put our render in a separate thread?
static u64 last_play_cursor_pos = 0;
static u64 dsound_buffer_size = 0;

// struct Ichigo::Thread {
//     HANDLE thread_handle;
//     DWORD thread_id;
// };

// struct ThreadParams {
//     Ichigo::ThreadEntryProc *entry_proc;
//     void *data;
// };

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// static DWORD thread_proc(void *data) {
//     auto params = reinterpret_cast<ThreadParams *>(data);
//     return params->entry_proc(params->data);
// }

// Ichigo::Thread *Ichigo::platform_create_thread(Ichigo::ThreadEntryProc *entry_proc, void *data) {
//     ThreadParams params = {
//         entry_proc,
//         data
//     };

//     Ichigo::Thread ret;
//     ret.thread_handle = CreateThread(nullptr, 0, thread_proc, &params, 0, &ret.thread_id);

//     return ret;
// }

std::FILE *Ichigo::platform_open_file(const std::string &path, const std::string &mode) {
    i32 buf_size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    i32 mode_buf_size = MultiByteToWideChar(CP_UTF8, 0, mode.c_str(), -1, nullptr, 0);
    assert(buf_size > 0 && mode_buf_size > 0);
    wchar_t *wide_buf = new wchar_t[buf_size];
    wchar_t *mode_wide_buf = new wchar_t[mode_buf_size];
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide_buf, buf_size);
    MultiByteToWideChar(CP_UTF8, 0, mode.c_str(), -1, mode_wide_buf, mode_buf_size);
    // std::wprintf(L"platform_open_file: wide_buf=%s mode_wide_buf=%s\n", wide_buf, mode_wide_buf);
    std::FILE *ret = _wfopen(wide_buf, mode_wide_buf);

    // TODO: Should this actually exist? If we want to check if a file exists then should we have a different platform function for it?
    // assert(ret != nullptr);
    delete[] wide_buf;
    delete[] mode_wide_buf;
    return ret;
}

static bool is_filtered_file(const wchar_t *filename, const std::vector<const char *> &extension_filter) {
    // Find the last period in the file name
    u64 period_index = 0;
    for (u64 current_index; filename[current_index] != '\0'; ++current_index) {
        if (filename[current_index] == '.')
            period_index = current_index;
    }

    wchar_t ext_wide[16] = {};
    for (auto ext : extension_filter) {
        i32 buf_size = MultiByteToWideChar(CP_UTF8, 0, ext, -1, nullptr, 0);
        assert(buf_size <= 16);
        MultiByteToWideChar(CP_UTF8, 0, ext, -1, ext_wide, buf_size);

        if (std::wcscmp(&filename[period_index + 1], ext_wide) == 0)
            return true;
    }

    return false;
}

void visit_directory(const wchar_t *path, std::vector<std::string> *files, const std::vector<const char *> &extension_filter) {
    HANDLE find_handle;
    WIN32_FIND_DATAW find_data;
    wchar_t path_with_filter[2048] = {};
    std::wcscat(path_with_filter, path);
    std::wcscat(path_with_filter, L"\\*");

    if ((find_handle = FindFirstFileW(path_with_filter, &find_data)) != INVALID_HANDLE_VALUE) {
        do {
            if (std::wcscmp(find_data.cFileName, L".") == 0 || std::wcscmp(find_data.cFileName, L"..") == 0)
                continue;

            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                wchar_t sub_dir[2048] = {};
                _snwprintf(sub_dir, 2048, L"%s/%s", path, find_data.cFileName);
                visit_directory(sub_dir, files, extension_filter);
            } else {
                if (!is_filtered_file(find_data.cFileName, extension_filter))
                    continue;

                wchar_t full_path[2048] = {};
                _snwprintf(full_path, 2048, L"%s/%s", path, find_data.cFileName);
                i32 wide_filename_len = std::wcslen(full_path);
                i32 u8_buf_size = WideCharToMultiByte(CP_UTF8, 0, full_path, -1, nullptr, 0, nullptr, nullptr);
                char *u8_bytes = new char[u8_buf_size]();
                WideCharToMultiByte(CP_UTF8, 0, full_path, -1, u8_bytes, u8_buf_size, nullptr, nullptr);

                files->emplace_back(u8_bytes);
                delete[] u8_bytes;
            }
        } while (FindNextFileW(find_handle, &find_data) != 0);

        FindClose(find_handle);
    } else {
        auto error = GetLastError();
        std::printf("error=%d\n", error);
    }
}

std::vector<std::string> Ichigo::platform_recurse_directory(const std::string &path, const std::vector<const char *> &extension_filter) {
    std::vector<std::string> ret;

    i32 buf_size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    assert(buf_size > 0);
    wchar_t *wide_buf = new wchar_t[buf_size]();
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide_buf, buf_size);

    visit_directory(wide_buf, &ret, extension_filter);

    delete[] wide_buf;
    return ret;
}

void Ichigo::platform_playback_set_state(const Ichigo::PlayerState state) {
    play_state = state;
    play_state_dirty_flag = true;
}

static u64 write_samples(u8 *, u64, u64);

void Ichigo::platform_playback_reset_for_seek(bool should_play) {
    if (!secondary_dsound_buffer || !Ichigo::current_song) {
        std::printf("(win32 plat) warn: attempted to reset for seek without a sound buffer allocated or a song currently playing\n");
        return;
    }

    secondary_dsound_buffer->Stop();

    assert(SUCCEEDED(secondary_dsound_buffer->GetCurrentPosition(reinterpret_cast<unsigned long *>(&last_written_pos), nullptr)));
    u64 bytes_to_write = Ichigo::current_song->sample_rate * sizeof(i32);
    Ichigo::fill_sample_buffer(samples, bytes_to_write);
    last_written_pos = write_samples(samples, bytes_to_write, last_written_pos);

    if (should_play)
        secondary_dsound_buffer->Play(0, 0, DSBPLAY_LOOPING);
}

static void commit_play_state() {
    switch (play_state) {
    case Ichigo::PlayerState::PLAYING: {
        if (secondary_dsound_buffer)
            secondary_dsound_buffer->Play(0, 0, DSBPLAY_LOOPING);
    } break;

    case Ichigo::PlayerState::PAUSED: {
        if (secondary_dsound_buffer)
            secondary_dsound_buffer->Stop();
    } break;

    case Ichigo::PlayerState::STOPPED: {
        if (secondary_dsound_buffer) {
            last_written_pos = 0;
            secondary_dsound_buffer->Stop();
            assert(SUCCEEDED(secondary_dsound_buffer->SetCurrentPosition(0)));
        }
    } break;
    }

    play_state_dirty_flag = false;
}

static void init_dsound(HWND window) {
    assert(SUCCEEDED(DirectSoundCreate8(nullptr, &direct_sound, nullptr)) && SUCCEEDED(direct_sound->SetCooperativeLevel(window, DSSCL_NORMAL)));
}

static void realloc_dsound_buffer(u32 samples_per_second, u32 buffer_size) {
    if (secondary_dsound_buffer)
        secondary_dsound_buffer->Release();

    WAVEFORMATEX wave_format = {};
    wave_format.wFormatTag = WAVE_FORMAT_PCM;
    wave_format.nChannels = 2;
    wave_format.nSamplesPerSec = samples_per_second;
    wave_format.wBitsPerSample = 16;
    wave_format.nBlockAlign = wave_format.nChannels * wave_format.wBitsPerSample / 8;
    wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
    wave_format.cbSize = 0;

    DSBUFFERDESC secondary_buffer_description = {};
    secondary_buffer_description.dwSize = sizeof(secondary_buffer_description);
    secondary_buffer_description.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_TRUEPLAYPOSITION;
    secondary_buffer_description.dwBufferBytes = buffer_size;
    secondary_buffer_description.lpwfxFormat = &wave_format;

    IDirectSoundBuffer *query_secondary_dsound_buffer;

    assert(SUCCEEDED(direct_sound->CreateSoundBuffer(&secondary_buffer_description, &query_secondary_dsound_buffer, nullptr)));
    assert(SUCCEEDED(query_secondary_dsound_buffer->QueryInterface(IID_IDirectSoundBuffer8, reinterpret_cast<void **>(&secondary_dsound_buffer))));

    query_secondary_dsound_buffer->Release();
}

static u64 write_samples(u8 *samples, u64 bytes_to_write, u64 last_written_pos) {
    u8 *region1, *region2;
    unsigned long region1_size = 0, region2_size = 0;
    assert(SUCCEEDED(secondary_dsound_buffer->Lock(last_written_pos, bytes_to_write, reinterpret_cast<void **>(&region1), &region1_size,
                                                   reinterpret_cast<void **>(&region2), &region2_size, 0)));
    std::memcpy(region1, samples, region1_size);
    std::memcpy(region2, samples + region1_size, region2_size);
    last_written_pos += bytes_to_write;
    last_written_pos %= dsound_buffer_size;
    secondary_dsound_buffer->Unlock(region1, region1_size, region2, region2_size);

    return last_written_pos;
}

static void platform_do_frame() {
    unsigned long play_cursor = 0;
    if (secondary_dsound_buffer)
        assert(SUCCEEDED(secondary_dsound_buffer->GetCurrentPosition(&play_cursor, nullptr)));

    u64 play_cursor_delta = play_cursor < last_play_cursor_pos ? dsound_buffer_size - last_play_cursor_pos + play_cursor : play_cursor - last_play_cursor_pos;
    last_play_cursor_pos = play_cursor;

    ImGui_ImplWin32_NewFrame();
    Ichigo::do_frame(previous_width, previous_height, ImGui_ImplWin32_GetDpiScaleForHwnd(window_handle), play_cursor_delta);

    if (Ichigo::must_realloc_sound_buffer) {
        if (secondary_dsound_buffer) {
            secondary_dsound_buffer->Stop();
            assert(SUCCEEDED(secondary_dsound_buffer->SetCurrentPosition(0)));
        }

        last_play_cursor_pos = 0;
        last_written_pos = 0;
        dsound_buffer_size = Ichigo::current_song->channel_count * sizeof(i16) * Ichigo::current_song->sample_rate * 8;
        realloc_dsound_buffer(Ichigo::current_song->sample_rate, dsound_buffer_size);
        Ichigo::must_realloc_sound_buffer = false;

        // Write one second of samples to the buffer initially so we do not hear silence
        if (Ichigo::current_song) {
            u64 bytes_to_write = Ichigo::current_song->sample_rate * sizeof(i32);
            Ichigo::fill_sample_buffer(samples, bytes_to_write);
            last_written_pos = write_samples(samples, bytes_to_write, last_written_pos);
        }
    }

    // TODO: I tested this in WM_TIMER instead so that we don't have to do this every frame, but there seems to be no difference in CPU (rough guess via task manager)
    if (Ichigo::current_song) {
        u32 distance_from_play_cursor = 0;
        if (play_cursor < last_written_pos)
            distance_from_play_cursor = dsound_buffer_size - last_written_pos + play_cursor;
        else
            distance_from_play_cursor = play_cursor - last_written_pos;

        u64 bytes_to_write = Ichigo::current_song->sample_rate * Ichigo::current_song->channel_count * sizeof(i16);
        // Ensure we are at least one second away from the play cursor
        if (distance_from_play_cursor < bytes_to_write)
            goto skip;

        // Static buffer size
        assert(bytes_to_write <= 400000);

        Ichigo::fill_sample_buffer(samples, bytes_to_write);
        last_written_pos = write_samples(samples, bytes_to_write, last_written_pos);
    }

skip:
    if (play_state_dirty_flag)
        commit_play_state();
}

static LRESULT window_proc(HWND window, u32 msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_ENTERSIZEMOVE: {
        printf("WM_ENTERSIZEMOVE\n");
        in_sizing_loop = true;
    } break;

    case WM_EXITSIZEMOVE: {
        printf("WM_EXITSIZEMOVE\n");
        in_sizing_loop = false;
    } break;

    case WM_SIZE: {
        return 0;
    } break;

    case WM_DESTROY: {
        std::printf("WM_DESTROY\n");
        PostQuitMessage(0);
        return 0;
    } break;

    case WM_CLOSE: {
        std::printf("WM_CLOSE\n");
        PostQuitMessage(0);
        return 0;
    } break;

    case WM_ACTIVATEAPP: {
        return 0;
    } break;

    case WM_TIMER: {
        if (in_sizing_loop)
            platform_do_frame();

        return 0;
    } break;

    case WM_PAINT: {
        // std::printf("WM_PAINT lparam=%lld wparam=%lld\n", lparam, wparam);
        PAINTSTRUCT paint;
        auto device = BeginPaint(window, &paint);

        if (init_completed) {
            i32 height = paint.rcPaint.bottom - paint.rcPaint.top;
            i32 width = paint.rcPaint.right - paint.rcPaint.left;

            if (height <= 0 || width <= 0)
                break;

            if (height != previous_height || width != previous_width) {
                Ichigo::must_rebuild_swapchain = true;
                previous_height = height;
                previous_width = width;
            }

            platform_do_frame();
        }
        EndPaint(window, &paint);
        return 0;
    } break;
    case WM_MOVE: {
        // std::printf("WM_MOVE\n");
    } break;
    }

    if (ImGui_ImplWin32_WndProcHandler(window, msg, wparam, lparam))
        return 1;

    return DefWindowProc(window, msg, wparam, lparam);
}

i32 main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleOutputCP(CP_UTF8);

    // Create win32 window
    auto instance = GetModuleHandle(nullptr);
    WNDCLASS window_class = {};
    window_class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = "musicplayer";
    RegisterClass(&window_class);
    window_handle = CreateWindowEx(0, window_class.lpszClassName, "Music Player", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1920, 1080, nullptr,
                                   nullptr, instance, nullptr);

    assert(window_handle);
    init_dsound(window_handle);
    // Ask main module to init vulkan
    static const char *extensions[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};

    Ichigo::vk_context.init(extensions, 2);

    // Create a win32 vk surface and give it to the vulkan module
    VkWin32SurfaceCreateInfoKHR surface_create_info = {};
    surface_create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_create_info.hinstance = instance;
    surface_create_info.hwnd = window_handle;

    VkSurfaceKHR vk_surface;
    assert(vkCreateWin32SurfaceKHR(Ichigo::vk_context.vk_instance, &surface_create_info, nullptr, &vk_surface) == VK_SUCCESS);

    Ichigo::vk_context.surface = vk_surface;
    Ichigo::init();

    // Platform init
    ImGui_ImplWin32_Init(window_handle);

    init_completed = true;
    u32 timer_id = SetTimer(window_handle, 0, 500, nullptr);
    // Main loop
    for (;;) {
        MSG message;
        while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT)
                return 0;

            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        platform_do_frame();
    }

    return 0;
}
