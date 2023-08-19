#pragma once
#include <string>
#include "ichigo.hpp"

namespace IchigoDB {
void init_for_path(const std::string &music_directory);
void refresh(const std::string &music_directory);
void cancel_refresh();
// std::vector<Ichigo::Song> *all_songs();
u64 processed_size();
u64 total_size();
// FIXME: Should this really return a pointer? For some reason in the main application we have current_song defined as a pointer.
// Thats the only reason that this returns a pointer.
Ichigo::Song *song(u64 i);
const Util::IchigoVector<Ichigo::Playlist> &playlists();
}  // namespace IchigoDB
