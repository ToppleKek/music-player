#include "tags.hpp"
#include "ichigo.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <vector>
#include <cstring>
#include "common.hpp"

bool str_equal_no_case(const char *lhs, const char *rhs) {
    while (*lhs != '\0' && *rhs != '\0') {
        ++lhs;
        ++rhs;

        if (std::tolower(*lhs) != std::tolower(*rhs))
            return false;
    }

    return true;
}

u32 to_le32(u32 be) {
    return (be & 0xFF) << 24 | ((be & 0xFF00) << 8) | (be & 0xFF0000) >> 8 | (be & 0xFF000000) >> 24;
}

u32 to_le24(u32 be) {
    return ((be & 0xFF) << 16) | (be & 0xFF00) | ((be & 0xFF0000) >> 16);
}

u16 to_le16(u16 be) {
    return static_cast<u16>((be & 0xFF) << 8 | (be & 0xFF00) >> 8);
}

u32 utf16_to_codepoint(u32 utf16) {
    if ((utf16 >= 0 && utf16 <= 0xD7FF) || (utf16 >= 0xE000 && utf16 <= 0xFFFF))
        return utf16;
    else
        return ((((utf16 & 0xFFFF0000) >> 16) - 0xD800) * 0x400 + ((utf16 & 0xFFFF) - 0xDC00)) + 0x10000;
    return 0;
}

u32 codepoint_to_utf8(u32 codepoint) {
    if (codepoint >= 0 && codepoint <= 0x7F)
        return codepoint;
    else if (codepoint >= 0x80 && codepoint <= 0x7FF)
        return (0b11000000 | (codepoint & 0b11111000000) >> 6) << 8 | (0b10000000 | (codepoint & 0b111111));
    else if (codepoint >= 0x800 && codepoint <= 0xFFFF)
        return (0b11100000 | (codepoint & 0b1111000000000000) >> 12) << 16 |
               (0b10000000 | (codepoint & 0b111111000000) >> 6) << 8 | (0b10000000 | (codepoint & 0b111111));
    else if (codepoint >= 0x10000 && codepoint <= 0x10FFFF)
        return (0b11110000 | (codepoint & 0b1111000000000000000000) >> 18) << 24 |
               (0b10000000 | (codepoint & 0b111111000000000000) >> 12) << 16 |
               (0b10000000 | (codepoint & 0b111111000000) >> 6) << 8 | (0b10000000 | (codepoint & 0b111111));
    return 0;
}

std::vector<char> utf16le_to_utf8(u16 *utf16_buf, u64 n) {
    std::vector<char> utf8_str;
    for (u64 i = 0, j = 0; i < n;) {
        u32 utf16_bytes;
        if ((utf16_buf[i] >= 0 && utf16_buf[i] <= 0xD7FF) ||
            (utf16_buf[i] >= 0xE000 && utf16_buf[i] <= 0xFFFF))
            utf16_bytes = utf16_buf[i++];
        else {
            utf16_bytes = utf16_buf[i] << 16 | utf16_buf[i + 1];
            i += 2;
        }

        u32 codepoint = utf16_to_codepoint(utf16_bytes);
        u32 utf8_bytes = codepoint_to_utf8(codepoint);

        if (utf8_bytes == 0)
            utf8_str.push_back(0);
        else {
            for (u64 k = 0; k < 4; ++k) {
                u8 byte = static_cast<u8>((utf8_bytes & (0xFF000000 >> (k * 8))) >> ((3 - k) * 8));
                if (byte != 0)
                    utf8_str.push_back(byte);
            }
        }
    }

    utf8_str.push_back(0);
    return utf8_str;
}

// n - Number of bytes to read (including BOM)
std::string extract_utf16_string(std::FILE *f, u64 n) {
    u16 bom = 0;
    std::fread(&bom, 2, 1, f);
    assert(bom == 0xFEFF);  // TODO: BE utf16 strings in id3??

    u16 *utf16_data = new u16[n - 2];
    std::fread(utf16_data, 1, n - 2, f);
    std::vector<char> utf8_bytes = utf16le_to_utf8(utf16_data, (n - 2) / 2);
    std::string ret = utf8_bytes.data();

    delete[] utf16_data;

    return ret;
}

std::string extract_iso8859_string(std::FILE *f, u64 n) {
    char *data = new char[n + 1]();
    std::fread(data, 1, n, f);
    std::string ret = data;
    delete[] data;
    return ret;
}

std::string extract_iso8859_string(std::FILE *f, u64 max_len, char delim) {
    char *data = new char[max_len + 1]();

    for (u32 i = 0; i < max_len - 1; ++i) {
        data[i] = std::fgetc(f);
        if (data[i] == delim)
            break;
    }

    std::string ret = data;
    delete[] data;
    return ret;
}

u32 extract_numeric_string(std::FILE *f, u64 n) {
    u8 *numeric_str_buf = new u8[n];
    std::fread(numeric_str_buf, 1, n, f);

    u32 ret = 0;
    for (u32 i = 0; i < n - 1; ++i) {
        if (numeric_str_buf[i] == 0)
            continue;

        // TODO: TRCK frame might contain a slash to denote how many tracks are in the album (6/10)
        if (numeric_str_buf[i] == '/')
            break;

        ret *= 10;
        ret += numeric_str_buf[i] - 0x30;
    }

    delete[] numeric_str_buf;

    return ret;
}

Tags::Tag Tags::id3_read(std::FILE *f) {
    std::rewind(f);
    Tags::Tag ret;

    u8 id3_header[3];
    std::fread(&id3_header, 1, 3, f);
    assert(id3_header[0] == 'I' && id3_header[1] == 'D' && id3_header[2] == '3');

    u8 version_major = static_cast<u8>(std::fgetc(f));
    [[maybe_unused]] u8 version_revision = static_cast<u8>(std::fgetc(f));

    // TODO: Handle flags
    [[maybe_unused]] u8 flags = static_cast<u8>(std::fgetc(f));

    if (version_major != 3 && version_major != 2) {
        std::printf("id3_read: fatal: unsupported ID3 major version: %d\n", version_major);
        return ret;
    }

    u32 id3_size = 0;
    std::fread(&id3_size, 4, 1, f);
    id3_size = to_le32(id3_size);

    // The ID3 tag size is encoded with four bytes where the first bit (bit 7) is set to zero in every byte,
    // making a total of 28 bits. The zeroed bits are ignored.
    u32 new_size = (id3_size & 0xFF) | (id3_size & 0xFF00) >> 1 | (id3_size & 0xFF0000) >> 2 |
                   (id3_size & 0xFF000000) >> 3;
    u32 pos = static_cast<u32>(std::ftell(f));

    while (std::ftell(f) <= pos + new_size) {
        u8 frame_name[5] = {};
        u32 frame_size = 0;
        u16 frame_flags = 0;

        if (version_major == 2) {
            std::fread(frame_name, 1, 3, f);
            std::fread(&frame_size, 1, 3, f);

            frame_size = to_le24(frame_size);
        } else if (version_major == 3) {
            std::fread(frame_name, 1, 4, f);
            std::fread(&frame_size, 1, 4, f);
            std::fread(&frame_flags, 1, 2, f);

            frame_size = to_le32(frame_size);
        }

        if (frame_name[0] == 0) {
            //std::printf("Reached tag padding.\n");
            break;
        } else if (std::memcmp(frame_name, "TIT2", 4) == 0 || std::memcmp(frame_name, "TT2", 3) == 0) {
            u8 type = static_cast<u8>(std::fgetc(f));
            //std::printf("Found TIT2 frame (%d bytes) string type=%d\n", frame_size, type);

            // 16 bit unicode
            if (type == 1)
                ret.title = extract_utf16_string(f, frame_size - 1);
            else if (type == 0)
                ret.title = extract_iso8859_string(f, frame_size - 1);
            else
                std::printf("id3_read: warn: unsupported string type in title frame: %d\n", type);
        } else if (std::memcmp(frame_name, "TLEN", 4) == 0 || std::memcmp(frame_name, "TLE", 3) == 0) {
            u8 type = static_cast<u8>(std::fgetc(f));
            //std::printf("Found TLEN frame (%d bytes) string type=%d\n", frame_size, type);

            if (type == 0)
                ret.length = extract_numeric_string(f, frame_size - 1);
            else
                std::printf("id3_read: warn: unsupported string type in length frame: %d\n", type);
        } else if (std::memcmp(frame_name, "TPE1", 4) == 0 || std::memcmp(frame_name, "TP1", 3) == 0) {
            u8 type = static_cast<u8>(std::fgetc(f));
            //std::printf("Found TPE1 frame (%d bytes) string type=%d\n", frame_size, type);

            // 16 bit unicode
            if (type == 1)
                ret.artist = extract_utf16_string(f, frame_size - 1);
            else if (type == 0)
                ret.artist = extract_iso8859_string(f, frame_size - 1);
            else
                std::printf("id3_read: warn: unsupported string type in artist frame: %d\n", type);
        } else if (std::memcmp(frame_name, "TRCK", 4) == 0 || std::memcmp(frame_name, "TRK", 3) == 0) {
            u8 type = static_cast<u8>(std::fgetc(f));
            //std::printf("Found TRCK frame (%d bytes) string type=%d\n", frame_size, type);

            if (type == 0)
                ret.track = extract_numeric_string(f, frame_size - 1);
            else if (type == 1)
                ret.track = std::atof(extract_utf16_string(f, frame_size - 1).c_str()); // TODO: !Speed
            else
                std::printf("id3_read: warn: unsupported string type in track frame: %d\n", type);
        } else if (std::memcmp(frame_name, "TALB", 4) == 0 || std::memcmp(frame_name, "TAL", 3) == 0) {
            u8 type = static_cast<u8>(std::fgetc(f));
            //std::printf("Found TALB frame (%d bytes) string type=%d\n", frame_size, type);

            // 16 bit unicode
            if (type == 1)
                ret.album = extract_utf16_string(f, frame_size - 1);
            else if (type == 0)
                ret.album = extract_iso8859_string(f, frame_size - 1);
            else
                std::printf("id3_read: warn: unsupported string type in album frame: %d\n", type);
        } else if (std::memcmp(frame_name, "APIC", 4) == 0) {
            i64 start_pos = std::ftell(f);

            u8 encoding = static_cast<u8>(std::fgetc(f));

            std::string mime_type = extract_iso8859_string(f, 64, '\0');
            u8 picture_type = static_cast<u8>(std::fgetc(f));
            std::string description;
            if (encoding == 0) {
                description = extract_iso8859_string(f, 64, '\0');
            } else if (encoding == 1) {
                //std::printf("this file is %s\n", ret.title.c_str());
                description = extract_utf16_string(f, 64);
            }
            //std::printf("Found APIC frame (%d bytes) string type=%d\n\tAPIC MIME type=%s picture_type=%hu description=%s\n", frame_size, encoding, mime_type.c_str(), picture_type, description.c_str());

            const i64 picture_data_length = frame_size - (std::ftell(f) - start_pos);
            std::fseek(f, picture_data_length, SEEK_CUR);
            // u8 *picture_data = new u8[picture_data_length];
            // std::fread(picture_data, 1, picture_data_length, f);

            // // TODO: Change this
            // std::FILE *cover_file = std::fopen("cover.jpg", "wb");
            // std::fwrite(picture_data, 1, picture_data_length, cover_file);
            // std::fclose(cover_file);
            // delete[] picture_data;
        } else {
            //std::printf("Skipping unknown frame: %s (%d bytes)\n", frame_name, frame_size);
            std::fseek(f, frame_size, SEEK_CUR);
        }
    }

    return ret;
}

Tags::Tag Tags::id3_read_path(const std::string &path) {
    std::FILE *f = Ichigo::platform_open_file(path, "rb");
    Tags::Tag ret = id3_read(f);
    std::fclose(f);
    return ret;
}

Tags::Tag Tags::flac_read(std::FILE *f) {
    std::rewind(f);

    Tags::Tag ret;
    u8 flac_header[4];
    std::fread(&flac_header, 1, 4, f);
    assert(flac_header[0] == 'f' && flac_header[1] == 'L' && flac_header[2] == 'a' && flac_header[3] == 'C');

    for (;;) {
        u32 metadata_block_header = 0;
        std::fread(&metadata_block_header, 4, 1, f);
        metadata_block_header = to_le32(metadata_block_header);

        u8 block_type = (metadata_block_header & (0b01111111 << 24)) >> 24;
        u32 metadata_block_length = (metadata_block_header << 8) >> 8;

        //std::printf("BLOCK_TYPE=%d BLOCK_LEN=%d\n", block_type, metadata_block_length);
        if (block_type == 4) {
            //std::printf("VORBIS_COMMENT START IS %lx\n", std::ftell(f));
            // VORBIS_COMMENT block
            u32 vendor_length = 0;
            std::fread(&vendor_length, 4, 1, f);
            assert(vendor_length > 0);
            char *vendor_string = new char[vendor_length + 1];
            std::fread(vendor_string, 1, vendor_length, f);
            vendor_string[vendor_length] = '\0';

            //std::printf("vendor_string=%s\n", vendor_string);

            u32 user_comment_list_length = 0;
            std::fread(&user_comment_list_length, 4, 1, f);

            char field_name_buffer[64] = {};
            for (u32 i = 0; i < user_comment_list_length; ++i) {
                u32 comment_length = 0;
                std::fread(&comment_length, 4, 1, f);

                char *comment_string = new char[comment_length + 1];
                comment_string[comment_length] = '\0';
                std::fread(comment_string, 1, comment_length, f);

                u32 j = 0;
                for (; j < 64; ++j) {
                    if (comment_string[j] == '=')
                        break;

                    field_name_buffer[j] = comment_string[j];
                }

                field_name_buffer[j] = '\0';
                if (str_equal_no_case(field_name_buffer, "TITLE"))
                    ret.title = &comment_string[j + 1];
                else if (str_equal_no_case(field_name_buffer, "ARTIST"))
                    ret.artist = &comment_string[j + 1];
                else if (str_equal_no_case(field_name_buffer, "ALBUM"))
                    ret.album = &comment_string[j + 1];

                delete[] comment_string;
            }

            delete[] vendor_string;
        } else if (block_type == 6) {
            // TODO: PICTURE block
            std::fseek(f, metadata_block_length, SEEK_CUR);
        } else {
            std::fseek(f, metadata_block_length, SEEK_CUR);
        }

        // This is the last metadata block before audio data
        if (metadata_block_header & (1 << 31))
            break;
    }

    return ret;
}

Tags::Tag Tags::flac_read_path(const std::string &path) {
    std::FILE *f = Ichigo::platform_open_file(path, "rb");
    Tags::Tag ret = flac_read(f);
    std::fclose(f);
    return ret;
}
