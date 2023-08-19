#include "song_table_view.hpp"
#include "db.hpp"
#include "thirdparty/imgui/imgui.h"
#include "util.hpp"

#include "play_queue.hpp"

enum { SongTableTitleColumnID, SongTableArtistColumnID, SongTableAlbumColumnID };

using GetSongCountProc = u64();

struct SongTableContext {
    u64 last_total_song_count;
    u64 *sorted_song_indicies;
    u64 sorted_song_indicies_length;
    ImGuiTableSortSpecs *current_song_table_sort_specs;
    i64 *last_playlist_song_list;
    GetSongCountProc *total_song_count_getter;
    GetSongCountProc *processed_song_count_getter;
};

// contexts[0] is the full library view
static Util::IchigoVector<SongTableContext> g_contexts;
static u32 g_current_context = 0;
static u32 g_last_playlist_count = 0;

static GetSongCountProc *get_current_playlist_total_song_count = []() { return IchigoDB::playlists().at(g_current_context - 1).size; };
static GetSongCountProc *calculate_current_playlist_count = []() {
    if (!IchigoDB::playlists().at(g_current_context - 1).unresolved_paths)  // All songs in playlist were resolved
        return IchigoDB::playlists().at(g_current_context - 1).size;
    // Calculate total resolved size of playlist
    u64 ret = 0;
    const Ichigo::Playlist &p = IchigoDB::playlists().at(g_current_context - 1);
    for (u64 i = 0; i < p.size; ++i) {
        if (p.songs[i] != -1)
            ++ret;
    }

    return ret;
};

#define CTX g_contexts.at(g_current_context)

#define SORT_GETTER
#define DO_SORT(START, END, PROPERTY, COMPARE_OPERATOR)                                                                      \
    {                                                                                                                        \
        for (u64 i = START; i < END; ++i) {                                                                                  \
            u64 j = 0;                                                                                                       \
            if (SORT_GETTER < 0)                                                                                             \
                continue;                                                                                                    \
            Ichigo::Song *song_to_insert = IchigoDB::song(SORT_GETTER);                                                      \
            for (; j < CTX.sorted_song_indicies_length; ++j) {                                                               \
                if (IchigoDB::song(CTX.sorted_song_indicies[j])->tag.PROPERTY COMPARE_OPERATOR song_to_insert->tag.PROPERTY) \
                    break;                                                                                                   \
            }                                                                                                                \
            for (u64 k = CTX.sorted_song_indicies_length; k > j; --k) {                                                      \
                assert(k < CTX.last_total_song_count && k > 0);                                                              \
                CTX.sorted_song_indicies[k] = CTX.sorted_song_indicies[k - 1];                                               \
            }                                                                                                                \
            assert(j < CTX.last_total_song_count);                                                                           \
            CTX.sorted_song_indicies[j] = song_to_insert->id;                                                                \
            ++CTX.sorted_song_indicies_length;                                                                               \
        }                                                                                                                    \
    }

static void sorted_song_index_list_insert_range(u64 start, u64 end) {
    if (!CTX.current_song_table_sort_specs)
        return;

        // u64 start_time = __rdtsc();

#undef SORT_GETTER
#define SORT_GETTER i
    switch (CTX.current_song_table_sort_specs->Specs[0].ColumnUserID) {
    case SongTableTitleColumnID: {
        if (CTX.current_song_table_sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending)
            DO_SORT(start, end, title, >=)
        else
            DO_SORT(start, end, title, <=)
    } break;
    case SongTableArtistColumnID: {
        if (CTX.current_song_table_sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending)
            DO_SORT(start, end, artist, >=)
        else
            DO_SORT(start, end, artist, <=)
    } break;
    case SongTableAlbumColumnID: {
        if (CTX.current_song_table_sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending)
            DO_SORT(start, end, album, >=)
        else
            DO_SORT(start, end, album, <=)
    } break;
    }
}

static void sorted_song_index_list_insert_items(const i64 *song_ids, const u64 song_id_count) {
    if (!CTX.current_song_table_sort_specs)
        return;

        // u64 start_time = __rdtsc();

#undef SORT_GETTER
#define SORT_GETTER song_ids[i]
    switch (CTX.current_song_table_sort_specs->Specs[0].ColumnUserID) {
    case SongTableTitleColumnID: {
        if (CTX.current_song_table_sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending)
            DO_SORT(0, song_id_count, title, >=)
        else
            DO_SORT(0, song_id_count, title, <=)
    } break;
    case SongTableArtistColumnID: {
        if (CTX.current_song_table_sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending)
            DO_SORT(0, song_id_count, artist, >=)
        else
            DO_SORT(0, song_id_count, artist, <=)
    } break;
    case SongTableAlbumColumnID: {
        if (CTX.current_song_table_sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending)
            DO_SORT(0, song_id_count, album, >=)
        else
            DO_SORT(0, song_id_count, album, <=)
    } break;
    }

#undef DO_SORT
#undef SORT_GETTER

    // u64 end_time = __rdtsc();
    // std::printf("sorted_song_index_list_insert_range: finished in %llu cycles.\n", end_time - start_time);
}

static void do_sorted_song_index_list_resort() {
    CTX.sorted_song_indicies_length = 0;
    const u64 size = CTX.processed_song_count_getter();
    std::printf("doing full re-sort with size=%llu\n", size);

    // FIXME: Retarded
    if (g_current_context == 0)
        sorted_song_index_list_insert_range(0, size);
    else {
        const Ichigo::Playlist &current_playlist = IchigoDB::playlists().at(g_current_context - 1);
        sorted_song_index_list_insert_items(current_playlist.songs, current_playlist.size);
    }
}

// FIXME: Actually make the playlist view have slightly different elements
static Ichigo::Song *render_table([[maybe_unused]] bool playlist) {
    Ichigo::Song *selected_song = nullptr;

    if (ImGui::BeginTable("song_table", 3,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable | ImGuiTableFlags_BordersOuter |
                              ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableSetupColumn("Title", 0, 0, SongTableTitleColumnID);
        ImGui::TableSetupColumn("Artist", 0, 0, SongTableArtistColumnID);
        ImGui::TableSetupColumn("Album", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortAscending, 0, SongTableAlbumColumnID);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        if (CTX.sorted_song_indicies) {
            u64 size = CTX.processed_song_count_getter();
            ImGuiTableSortSpecs *sort_specs;
            if (size && (sort_specs = ImGui::TableGetSortSpecs()) && sort_specs->SpecsDirty) {
                CTX.current_song_table_sort_specs = sort_specs;
                do_sorted_song_index_list_resort();
                sort_specs->SpecsDirty = false;
            }

            ImGuiListClipper clipper;
            clipper.Begin(CTX.sorted_song_indicies_length, ImGui::GetTextLineHeightWithSpacing());
            while (clipper.Step()) {
                for (i32 i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    Ichigo::Song *song = IchigoDB::song(CTX.sorted_song_indicies[i]);

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    ImGui::PushID(i);

                    if (ImGui::Selectable(song->tag.title.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
                        selected_song = song;

                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::Selectable("Play next"))
                            PlayQueue::enqueue_after_current(song->id);
                        if (ImGui::Selectable("Add to queue"))
                            PlayQueue::enqueue_last(song->id);

                        ImGui::EndPopup();
                    }

                    ImGui::PopID();

                    ImGui::TableNextColumn();
                    ImGui::Text("%s", song->tag.artist.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", song->tag.album.c_str());
                }
            }

            clipper.End();
        }

        ImGui::EndTable();
    }

    return selected_song;
}

void SongTableView::init() {
    // Full library view context
    g_contexts.append({0, nullptr, 0, nullptr, nullptr, []() { return IchigoDB::total_size(); }, []() { return IchigoDB::processed_size(); }});
}

void SongTableView::render() {
    // TODO: Reload playlists properly (ie. don't reload every context when the number of playlists changes)
    if (g_last_playlist_count != IchigoDB::playlists().size()) {
        // Remove old contexts
        for (u64 i = 0; i < g_last_playlist_count; ++i) {
            delete[] g_contexts.at(1).last_playlist_song_list;
            g_contexts.remove(1);
        }

        g_last_playlist_count = IchigoDB::playlists().size();

        for (u64 i = 0; i < g_last_playlist_count; ++i) {
            const Ichigo::Playlist &playlist = IchigoDB::playlists().at(i);
            i64 *last_playlist_song_list = new i64[playlist.size];
            std::memcpy(last_playlist_song_list, playlist.songs, playlist.size * sizeof(i64));

            g_contexts.append({0, nullptr, 0, nullptr, last_playlist_song_list, get_current_playlist_total_song_count, calculate_current_playlist_count});
        }
    }

    u64 new_total_size = CTX.total_song_count_getter();

    if (CTX.last_total_song_count != new_total_size) {
        if (CTX.sorted_song_indicies)
            delete[] CTX.sorted_song_indicies;

        CTX.last_total_song_count = new_total_size;
        CTX.sorted_song_indicies = new u64[CTX.last_total_song_count];
    }

    const u64 new_processed_size = CTX.processed_song_count_getter();
    if (CTX.sorted_song_indicies_length != new_processed_size) {
        // FIXME: Retarded
        if (g_current_context == 0)
            sorted_song_index_list_insert_range(CTX.sorted_song_indicies_length, new_processed_size);
        else {
            // This is ultra retarded. We determine what songs just got added since last frame, and then add them to the playlist's context sorted indicies.
            Util::IchigoVector<i64> new_songs;
            const Ichigo::Playlist &current_playlist = IchigoDB::playlists().at(g_current_context - 1);
            i64 *current_playlist_songs = new i64[current_playlist.size];
            std::memcpy(current_playlist_songs, current_playlist.songs, current_playlist.size * sizeof(i64));

            for (u64 i = 0; i < current_playlist.size; ++i) {
                if (current_playlist_songs[i] != CTX.last_playlist_song_list[i])
                    new_songs.append(current_playlist_songs[i]);
            }

            delete[] CTX.last_playlist_song_list;
            CTX.last_playlist_song_list = current_playlist_songs;

            sorted_song_index_list_insert_items(new_songs.data(), new_songs.size());
        }
    }

    ImGui::BeginChild("song_table", ImVec2(ImGui::GetContentRegionAvail().x * 0.8f, -ImGui::GetFrameHeightWithSpacing() - ImGui::GetTextLineHeightWithSpacing() * 4));
    if (ImGui::BeginTabBar("song_table_tab_bar", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)) {
        Ichigo::Song *selected_song = nullptr;

        if (ImGui::BeginTabItem("Library")) {
            g_current_context = 0;
            selected_song = render_table(false);
            ImGui::EndTabItem();
        }

        for (u64 i = 1; i < g_contexts.size(); ++i) {
            ImGui::PushID(-i);
            if (ImGui::BeginTabItem(IchigoDB::playlists().at(i - 1).name.c_str())) {
                g_current_context = i;
                selected_song = render_table(true);
                ImGui::EndTabItem();
            }
            ImGui::PopID();
        }

        if (selected_song) {
            std::printf("playing and adding song_id=%llu next in the queue\n", selected_song->id);
            Ichigo::play_song(PlayQueue::set_position(PlayQueue::enqueue_after_current(selected_song->id)));
        }

        ImGui::EndTabBar();
    }

    ImGui::EndChild();
}
