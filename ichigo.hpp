#pragma once
#include "vulkan.hpp"
#include "tags.hpp"
#include "util.hpp"
#include <string>
#include <vector>

namespace Ichigo {
enum class SongFormat {
    MP3,
    FLAC
};

struct Song {
    u64 id;
    u64 duration;
    u64 duration_in_bytes;
    u32 sample_rate;
    u32 channel_count;
    std::string path;
    Tags::Tag tag;
    SongFormat format;
};

struct Playlist {
    i64 *songs;
    char **unresolved_paths;
    std::string name;
    u64 size;
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
void play_song(u64 id);
void set_player_state(Ichigo::PlayerState state);
Ichigo::PlayerState get_player_state();
void deinit();

// Thread *platform_create_thread(ThreadEntryProc *entry_proc, void *data);
std::FILE *platform_open_file(const std::string &path, const std::string &mode);
bool platform_file_exists(const char *path);
Util::IchigoVector<std::string> platform_recurse_directory(const std::string &path, const char **extension_filter, const u16 extension_filter_count);
void platform_playback_set_state(const Ichigo::PlayerState state);
void platform_playback_reset_for_seek(bool should_play);
}
