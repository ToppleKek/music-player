#define _CRT_SECURE_NO_WARNINGS
#include "db.hpp"
#include "tags.hpp"
#include <filesystem>
#include <thread>

#define NUM_THREADS 6

static u64 thread_last_processed_index[NUM_THREADS]{};
static Ichigo::Song *songs = nullptr;
static Util::IchigoVector<Ichigo::Playlist> g_playlists;
static std::atomic<u64> num_songs = 0;
static u64 number_of_files = 0;
static std::thread refresh_worker;
static bool should_kill_self = false;
static bool currently_processing = false;

void IchigoDB::init_for_path(const std::string &music_directory) {
    refresh(music_directory);
}

Util::IchigoVector<std::string> parse_playlists(const std::string &playlist_path) {
    static const char *extension_filter[] = { "m3u", "m3u8" };
    Util::IchigoVector<std::string> files = Ichigo::platform_recurse_directory(playlist_path, extension_filter, 2);
    Util::IchigoVector<std::string> ret;

    for (u32 i = 0; i < files.size(); ++i) {
        const std::string current_file = files.at(i);
        std::printf("parsing playlist: %s\n", current_file.c_str());
        Ichigo::Playlist p;
        std::FILE *f = Ichigo::platform_open_file(current_file, "rb");
        std::fseek(f, 0, SEEK_END);
        u64 file_size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        char *m3u_data = new char[file_size + 1];
        m3u_data[file_size] = 0;
        std::fread(m3u_data, 1, file_size, f);
        std::fclose(f);

        Util::IchigoVector<char *> lines;
        const std::string parent_path = current_file.substr(0, current_file.find_last_of('/'));
        char *line = std::strtok(m3u_data, "\n");
        while (line) {
            if (line[0] != '#') {
                char *line_copy = nullptr;
                if (!Ichigo::platform_file_exists(line)) {
                    u64 buf_size = parent_path.size() + std::strlen(line) + 2;
                    std::printf("buf_size=%llu\n", buf_size);
                    line_copy = new char[buf_size];
                    std::strcpy(line_copy, parent_path.c_str());
                    line_copy[parent_path.size()] = '/';
                    std::strcpy(&line_copy[parent_path.size() + 1], line);
                } else {
                    line_copy = new char[std::strlen(line) + 1];
                    std::strcpy(line_copy, line);
                }

                lines.append(line_copy);
                ret.append(line_copy);
            }

            line = std::strtok(nullptr, "\n");
        }

        u32 begin = current_file.find_last_of('/') + 1;
        p.name = current_file.substr(begin, current_file.find_last_of('.') - begin);
        p.size = lines.size();
        p.songs = new i64[p.size];

        for (u64 j = 0; j < p.size; ++j)
            p.songs[j] = -1;

        p.unresolved_paths = new char *[p.size];
        p.unresolved_paths = lines.release_data();

        std::printf("finished parsing playlist: %s size=%llu\n", p.name.c_str(), p.size);
        g_playlists.append(p);
        delete[] m3u_data;
    }

    return ret;
}

void thread_work(const Util::IchigoVector<std::string> *files, std::atomic<u64> *index, u8 my_id) {
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

        for (u64 i = 0; i < g_playlists.size(); ++i) {
            auto playlist = g_playlists.at(i);
            for (u64 j = 0; j < playlist.size; ++j) {
                if (playlist.unresolved_paths[j] != nullptr && std::strcmp(playlist.unresolved_paths[j], s.path.c_str()) == 0) {
                    playlist.songs[j] = s.id;
                    std::printf("Resolved path %s to song id %llu\n", playlist.unresolved_paths[j], s.id);
                }
            }
        }

        if (s.tag.album.length() == 0 && s.tag.artist.length() == 0 && s.tag.title.length() == 0)
            std::printf("Just parsed a song with blank everything. Path=%s\n", s.path.c_str());

        songs[this_index] = s;
        num_songs++;
        thread_last_processed_index[my_id] = this_index;
    }
}

static void process_files(std::string music_directory) {
    static const char *extension_filter[] = { "mp3", "flac" };
    Util::IchigoVector<std::string> files = Ichigo::platform_recurse_directory(music_directory, extension_filter, 2);
    Util::IchigoVector<std::string> playlist_requested_files = parse_playlists(music_directory + "/playlists"); // FIXME: Hardcoded path

    for (u64 i = 0; i < playlist_requested_files.size(); ++i) {
        const std::string file = playlist_requested_files.at(i);
        if (files.index_of(file) == -1) {
            std::printf("Adding %s to full file list.\n", file.c_str());
            files.append(file);
        }
    }

    number_of_files = files.size();
    songs = new Ichigo::Song[number_of_files];
    std::atomic<u64> index = 0;
    std::thread threads[NUM_THREADS];

    for (u8 i = 0; i < NUM_THREADS; ++i)
        threads[i] = std::thread{thread_work, &files, &index, i};

    for (u8 i = 0; i < NUM_THREADS; ++i)
        threads[i].join();

    for (u64 i = 0; i < g_playlists.size(); ++i)
        delete[] g_playlists.at(i).unresolved_paths;

    currently_processing = false;
    std::printf("process_files: All threads have joined: killing myself now\n");
}

void IchigoDB::refresh(const std::string &music_directory) {
    cancel_refresh();

    if (songs)
        delete[] songs;

    currently_processing = true;
    refresh_worker = std::thread{process_files, music_directory};
}

void IchigoDB::cancel_refresh() {
    if (refresh_worker.joinable()) {
        should_kill_self = true;
        refresh_worker.join();
        should_kill_self = false;
    }
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

const Util::IchigoVector<Ichigo::Playlist> &IchigoDB::playlists() {
    return g_playlists;
}
