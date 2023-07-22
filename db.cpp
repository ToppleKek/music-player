#include "db.hpp"
#include "tags.hpp"
#include <filesystem>
#include <thread>

// static std::vector<Ichigo::Song> songs;
static Ichigo::Song *songs = nullptr;
static std::atomic<u64> num_songs = 0;

void IchigoDB::init_for_path(const std::string &music_directory) {
    refresh(music_directory);
}

void thread_work(std::vector<std::string> *files, std::atomic<u64> *index) {
    for (;;) {
        u64 this_index = (*index)++;
        if (this_index >= files->size())
            break;

        Ichigo::SongFormat format;
        Ichigo::Song s;

        s.path = files->at(this_index);

        if (s.path.ends_with(".mp3")) {
            s.format = Ichigo::SongFormat::MP3;
            std::FILE *f = Ichigo::platform_open_file(s.path, "rb");
            if (f == nullptr) {
                std::printf("(db) warn: Failed to open file: %s\n", s.path.c_str());
                continue;
            }

            s.tag = Tags::id3_read(f);
            std::fclose(f);
        } else if (s.path.ends_with(".flac")) {
            s.format = Ichigo::SongFormat::FLAC;
            std::FILE *f = Ichigo::platform_open_file(s.path, "rb");
            if (f == nullptr) {
                std::printf("(db) warn: Failed to open file: %s\n", s.path.c_str());
                continue;
            }

            s.tag = Tags::flac_read(f);
            std::fclose(f);
        }
        else
            continue;


        songs[this_index] = s;
        num_songs++;
    }
}

void process_files(std::vector<std::string> files) {
    std::atomic<u64> index = 0;
    std::vector<std::jthread> threads{2};

    for (u32 i = 0; i < 2; ++i)
        threads.emplace_back(thread_work, &files, &index);
}

// FIXME: Not thread safe, should we cancel any ongoing refresh requests before we enter?
void IchigoDB::refresh(const std::string &music_directory) {
    if (songs)
        delete[] songs;

    const std::vector<std::string> files = Ichigo::platform_recurse_directory(music_directory, {"mp3", "flac"});
    songs = new Ichigo::Song[files.size()];
    std::thread worker{process_files, files};
    worker.detach();
}

u64 IchigoDB::size() {
    return num_songs;
}

Ichigo::Song *IchigoDB::song(u64 i) {
    return &songs[i];
}
