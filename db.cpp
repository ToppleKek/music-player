#include "db.hpp"
#include <unordered_map>
#include <filesystem>

// static std::unordered_map<std::string, Ichigo::Song> song_map;
static std::vector<Ichigo::Song> songs;

void IchigoDB::init_for_path(const std::string &music_directory) {
    songs.clear();
    const std::vector<std::string> files = Ichigo::platform_recurse_directory(music_directory);

    for (const auto &path : files) {
        // std::printf("[db] path=%s\n", path.c_str());
        Ichigo::SongFormat format;

        if (path.ends_with(".mp3"))
            format = Ichigo::SongFormat::MP3;
        else if (path.ends_with(".flac"))
            format = Ichigo::SongFormat::FLAC;
        else
            continue;

        songs.emplace_back(path, format);
    }
}

std::vector<Ichigo::Song> *IchigoDB::all_songs() {
    return &songs;
}
