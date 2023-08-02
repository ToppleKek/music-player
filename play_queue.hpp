#pragma once
#include "common.hpp"
#include "ichigo.hpp"

namespace PlayQueue {
// Enqueue a song at a specified position
void enqueue(u64 song_id, u64 position);
// Enqueue a song after the current song in the queue. Returns the position in the queue the song was added at
u64 enqueue_after_current(u64 song_id);
// Enqueue a song at the end of the queue. Returns the position in the queue the song was added at
u64 enqueue_last(u64 song_id);
// Query whether or not the queue has more songs after the current song
bool has_more_songs();
// Move the queue to the next song and get the next song's id
u64 next_song_id();
// Set the position of the queue. Returns the song id of the new position
u64 set_position(u64 position);
// Remove a song from the specified position. Returns the removed song's id
u64 remove_song_at(u64 position);

void render();
}
