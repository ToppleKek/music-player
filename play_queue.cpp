#include "play_queue.hpp"
#include "util.hpp"
#include "db.hpp"
#include "thirdparty/imgui/imgui.h"

static Util::IchigoVector<u64> g_queue;
static u64 g_current_position = 0;

void PlayQueue::enqueue(u64 song_id, u64 position) {
    g_queue.insert(position, song_id);
}

u64 PlayQueue::enqueue_after_current(u64 song_id) {
    if (g_queue.size() == 0) {
        g_queue.insert(0, song_id);
        return 0;
    }

    g_queue.insert(g_current_position + 1, song_id);
    return g_current_position + 1;
}

u64 PlayQueue::enqueue_last(u64 song_id) {
    g_queue.append(song_id);
    return g_queue.size() - 1;
}

bool PlayQueue::has_more_songs() {
    return g_current_position < g_queue.size() - 1;
}

u64 PlayQueue::next_song_id() {
    assert(has_more_songs());
    return g_queue.at(++g_current_position);
}

u64 PlayQueue::set_position(u64 position) {
    assert(position < g_queue.size());
    g_current_position = position;
    return g_queue.at(g_current_position);
}

u64 PlayQueue::remove_song_at(u64 position) {
    assert(position != g_current_position);
    if (position < g_current_position)
        --g_current_position;

    return g_queue.remove(position);
}

void PlayQueue::render() {
    ImGui::BeginChild("play_queue", ImVec2(ImGui::GetContentRegionAvail().x * 0.3, -ImGui::GetFrameHeightWithSpacing() - ImGui::GetTextLineHeightWithSpacing() * 4));
     if (ImGui::BeginTable("queue_songs_table", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableSetupColumn("*", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Song", ImGuiTableColumnFlags_WidthStretch);
        u64 selected_queue_index = 0;
        Ichigo::Song *selected_song = nullptr;
        u64 size = g_queue.size();

        ImGuiListClipper clipper;
        clipper.Begin(size, ImGui::GetTextLineHeightWithSpacing());
        while (clipper.Step()) {
            for (i32 i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                if (static_cast<u32>(i) == g_current_position)
                    ImGui::Text("*");

                ImGui::TableNextColumn();

                Ichigo::Song *song = IchigoDB::song(g_queue.at(i));

                ImGui::PushID(i);
                if (ImGui::Selectable(song->tag.title.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_queue_index = i;
                    selected_song = song;
                }

                ImGui::PopID();
            }
        }

        clipper.End();
        ImGui::EndTable();

        if (selected_song) {
            g_current_position = selected_queue_index;
            Ichigo::play_song(selected_song->id);
        }
    }

    ImGui::EndChild();
}

