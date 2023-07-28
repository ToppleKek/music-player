#include "db.hpp"
#include "tags.hpp"
#include <filesystem>
#include <thread>

#define NUM_THREADS 2

static u64 thread_last_processed_index[NUM_THREADS]{};
static Ichigo::Song *songs = nullptr;
static std::atomic<u64> num_songs = 0;
static u64 number_of_files = 0;
static std::thread refresh_worker;
static bool should_kill_self = false;
static bool currently_processing = false;

void IchigoDB::init_for_path(const std::string &music_directory) {
    refresh(music_directory);
}

void thread_work(const std::vector<std::string> *files, std::atomic<u64> *index, u8 my_id) {
    while (!should_kill_self) {
        u64 this_index = (*index)++;
        if (this_index >= files->size())
            break;

        Ichigo::Song s;

        s.id = this_index;
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
        thread_last_processed_index[my_id] = this_index;
    }
}

static void process_files(std::string music_directory) {
    const std::vector<std::string> files = Ichigo::platform_recurse_directory(music_directory, {"mp3", "flac"});
    number_of_files = files.size();
    songs = new Ichigo::Song[number_of_files];
    std::atomic<u64> index = 0;
    std::thread threads[NUM_THREADS];

    for (u8 i = 0; i < NUM_THREADS; ++i)
        threads[i] = std::thread{thread_work, &files, &index, i};

    for (u8 i = 0; i < NUM_THREADS; ++i)
        threads[i].join();

    currently_processing = false;
}

void IchigoDB::refresh(const std::string &music_directory) {
    if (refresh_worker.joinable()) {
        should_kill_self = true;
        refresh_worker.join();
        should_kill_self = false;
    }

    if (songs)
        delete[] songs;

    currently_processing = true;
    refresh_worker = std::thread{process_files, music_directory};
}

u64 IchigoDB::processed_size() {
    if (!currently_processing)
        return num_songs;

    u64 min = thread_last_processed_index[0];
    for (u8 i = 1; i < NUM_THREADS; ++i) {
        if (min > thread_last_processed_index[i])
            min = thread_last_processed_index[i];
    }

    return min;
}

u64 IchigoDB::total_size() {
    return number_of_files;
}

Ichigo::Song *IchigoDB::song(u64 i) {
    return &songs[i];
}
