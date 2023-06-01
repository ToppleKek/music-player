#pragma once
#include <string>
#include <vector>
#include "ichigo.hpp"

namespace IchigoDB {
void init_for_path(const std::string &music_directory);
std::vector<Ichigo::Song> *all_songs();
}  // namespace IchigoDB
