/* the Music Player Daemon (MPD)
 * Copyright (C) 2003-2007 by Warren Dukes (warren.dukes@gmail.com)
 * This project's homepage is: http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "pipe.h"
#include "buffer.h"
#include "chunk.h"

#include <glib.h>

#include <assert.h>

struct music_pipe {
	/** the first chunk */
	struct music_chunk *head;

	/** a pointer to the tail of the chunk */
	struct music_chunk **tail_r;

	/** the current number of chunks */
	unsigned size;

	/** a mutex which protects #head and #tail_r */
	GMutex *mutex;
};

struct music_pipe *
music_pipe_new(void)
{
	struct music_pipe *mp = g_new(struct music_pipe, 1);

	mp->head = NULL;
	mp->tail_r = &mp->head;
	mp->size = 0;
	mp->mutex = g_mutex_new();

	return mp;
}

void
music_pipe_free(struct music_pipe *mp)
{
	assert(mp->head == NULL);
	assert(mp->tail_r == &mp->head);

	g_mutex_free(mp->mutex);
	g_free(mp);
}

const struct music_chunk *
music_pipe_peek(const struct music_pipe *mp)
{
	return mp->head;
}

struct music_chunk *
music_pipe_shift(struct music_pipe *mp)
{
	struct music_chunk *chunk;

	g_mutex_lock(mp->mutex);

	chunk = mp->head;
	if (chunk != NULL) {
		mp->head = chunk->next;
		--mp->size;

		if (mp->head == NULL) {
			assert(mp->size == 0);
			assert(mp->tail_r == &chunk->next);

			mp->tail_r = &mp->head;
		} else {
			assert(mp->size > 0);
			assert(mp->tail_r != &chunk->next);
		}

#ifndef NDEBUG
		/* poison the "next" reference */
		chunk->next = (void*)0x01010101;
#endif
	}

	g_mutex_unlock(mp->mutex);

	return chunk;
}

void
music_pipe_clear(struct music_pipe *mp, struct music_buffer *buffer)
{
	struct music_chunk *chunk;

	while ((chunk = music_pipe_shift(mp)) != NULL)
		music_buffer_return(buffer, chunk);
}

void
music_pipe_push(struct music_pipe *mp, struct music_chunk *chunk)
{
	g_mutex_lock(mp->mutex);

	chunk->next = NULL;
	*mp->tail_r = chunk;
	mp->tail_r = &chunk->next;

	++mp->size;

	g_mutex_unlock(mp->mutex);
}

unsigned
music_pipe_size(const struct music_pipe *mp)
{
	return mp->size;
}
