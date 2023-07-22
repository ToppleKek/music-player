#pragma once
#include "vulkan.hpp"
#include "tags.hpp"
#include <string>
#include <vector>

namespace Ichigo {
enum class SongFormat {
    MP3,
    FLAC
};

struct Song {
    u64 duration = 0;
    u64 duration_in_bytes = 0;
    u32 sample_rate = 0;
    u32 channel_count = 0;
    std::string path; // TODO: remove?
    Tags::Tag tag;
    SongFormat format;
};

enum class PlayerState {
    PLAYING,
    PAUSED,
    STOPPED,
};

// struct Thread;

extern IchigoVulkan::Context vk_context;
extern bool must_rebuild_swapchain;
extern bool must_realloc_sound_buffer;
extern bool current_song_has_data;
extern Song *current_song;

void init();
void do_frame(u32 window_width, u32 window_height, float dpi_scale, u64 play_cursor_delta);
u64 fill_sample_buffer(u8 *buffer, u64 buffer_size);

// Thread *platform_create_thread(ThreadEntryProc *entry_proc, void *data);
std::FILE *platform_open_file(const std::string &path, const std::string &mode);
std::vector<std::string> platform_recurse_directory(const std::string &path, const std::vector<const char *> &extension_filter);
void platform_playback_set_state(const Ichigo::PlayerState state);
void platform_playback_reset_for_seek(bool should_play);
}
