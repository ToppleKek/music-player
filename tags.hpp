#pragma once
#define _CRT_SECURE_NO_WARNINGS

#include <string>
#include "common.hpp"

namespace Tags {
struct Tag {
    u32 track;
    u64 length;
    std::string title;
    std::string artist;
    std::string album;
};

Tag id3_read(std::FILE *f);
Tag id3_read_path(const std::string &path);
Tag id3_read_data(u8 *data, u64 len);
Tag flac_read(std::FILE *f);
Tag flac_read_path(const std::string &path);
Tag flac_read_data(u8 *data, u64 len);
}  // namespace Tags
