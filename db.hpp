#pragma once
#include <string>
#include <vector>
#include "ichigo.hpp"

namespace IchigoDB {
void init_for_path(const std::string &music_directory);
void refresh(const std::string &music_directory);
// std::vector<Ichigo::Song> *all_songs();
u64 size();
// FIXME: Should this really return a pointer? For some reason in the main application we have current_song defined as a pointer.
// Thats the only reason that this returns a pointer.
Ichigo::Song *song(u64 i);
Ichigo::Song thread_local_song(u64 i);
}  // namespace IchigoDB
