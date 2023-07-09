#include "db.hpp"
#include "tags.hpp"
#include <filesystem>
#include <mutex>
static std::vector<Ichigo::Song> songs;
static std::mutex mutex;

void IchigoDB::init_for_path(const std::string &music_directory) {
    refresh(music_directory);
}

void file_visit_callback(const std::string &path) {
    std::lock_guard<std::mutex> guard(mutex);
    Ichigo::SongFormat format;
    Ichigo::Song s;
    s.path = path;

    FILE *f = Ichigo::platform_open_file(path, "rb");
    if (f == nullptr) {
        std::printf("(db) warn: Failed to open file: %s\n", path.c_str());
        return;
    }

    if (path.ends_with(".mp3")) {
        s.format = Ichigo::SongFormat::MP3;
        s.tag = Tags::id3_read(f);
    } else if (path.ends_with(".flac")) {
        s.format = Ichigo::SongFormat::FLAC;
        s.tag = Tags::flac_read(f);
    }
    else
        goto close;


    songs.push_back(s);
close:
    std::fclose(f);
}

// FIXME: Not thread safe, should we cancel any ongoing refresh requests before we enter?
void IchigoDB::refresh(const std::string &music_directory) {
    songs.clear();

    // const std::vector<std::string> files = Ichigo::platform_recurse_directory(music_directory);

    // for (const auto path : files) {
    //     Ichigo::SongFormat format;
    //     Ichigo::Song s;
    //     s.path = path;

    //     FILE *f = Ichigo::platform_open_file(path, "rb");
    //     if (f == nullptr) {
    //         std::printf("(db) warn: Failed to open file: %s\n", path.c_str());
    //         continue;
    //     }

    //     if (path.ends_with(".mp3")) {
    //         s.format = Ichigo::SongFormat::MP3;
    //         s.tag = Tags::id3_read(f);
    //     } else if (path.ends_with(".flac")) {
    //         s.format = Ichigo::SongFormat::FLAC;
    //         s.tag = Tags::flac_read(f);
    //     }
    //     else
    //         goto close;


    //     songs.push_back(s);
    // close:
    //     std::fclose(f);
    // }

    Ichigo::platform_recurse_directory_async(music_directory, file_visit_callback);
}

std::vector<Ichigo::Song> *IchigoDB::all_songs() {
    // std::lock_guard<std::mutex> guard(mutex);
    return &songs;
}
