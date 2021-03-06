#include "read_cache.h"

void 
read_cache_init(read_cache_state_t* state, request_context_t* request_context, size_t buffer_size, size_t alignment)
{
	vod_memzero(state, sizeof(*state));
	state->request_context = request_context;
	state->buffer_size = buffer_size;
	state->alignment = alignment;
}

bool_t 
read_cache_get_from_cache(read_cache_state_t* state, int cache_slot_id, uint64_t offset, u_char** buffer, uint32_t* size)
{
	cache_buffer_t* cur_buffer;
	cache_buffer_t* buffers_end = state->buffers + CACHED_BUFFERS;

	// check whether we already have the requested offset
	for (cur_buffer = state->buffers; cur_buffer < buffers_end; cur_buffer++)
	{
		if (offset >= cur_buffer->start_offset && offset < cur_buffer->end_offset)
		{
			*buffer = cur_buffer->buffer + (offset - cur_buffer->start_offset);
			*size = cur_buffer->end_offset - offset;
			return TRUE;
		}
	}

	// don't have the offset in cache
	cache_slot_id %= CACHED_BUFFERS;
	state->target_buffer = &state->buffers[cache_slot_id];

	return FALSE;
}

void
read_cache_disable_buffer_reuse(read_cache_state_t* state)
{
	vod_memzero(state->target_buffer, sizeof(*state->target_buffer));
}

vod_status_t 
read_cache_get_read_buffer(read_cache_state_t* state, uint64_t offset, uint64_t* out_offset, u_char** buffer, uint32_t* size)
{
	cache_buffer_t* target_buffer;
	cache_buffer_t* cur_buffer;
	cache_buffer_t* buffers_end = state->buffers + CACHED_BUFFERS;
	uint32_t read_size;

	offset &= ~(state->alignment - 1);

	// select a buffer
	target_buffer = state->target_buffer;
	
	// make sure the buffer is allocated
	if (target_buffer->buffer == NULL)
	{
		target_buffer->buffer = vod_memalign(state->request_context->pool, state->buffer_size + 1, state->alignment);
		if (target_buffer->buffer == NULL)
		{
			vod_log_debug0(VOD_LOG_DEBUG_LEVEL, state->request_context->log, 0,
				"read_cache_get_read_buffer: vod_memalign failed");
			return VOD_ALLOC_FAILED;
		}
	}
	
	// make sure we don't read anything we already have in cache
	read_size = state->buffer_size;
	for (cur_buffer = state->buffers; cur_buffer < buffers_end; cur_buffer++)
	{
		if (cur_buffer != target_buffer && cur_buffer->start_offset > offset)
		{
			read_size = MIN(read_size, cur_buffer->start_offset - offset);
		}
	}

	// save the state of the active read action
	target_buffer->start_offset = offset;
	
	// return the target buffer pointer and size
	*out_offset = offset;
	*buffer = target_buffer->buffer;
	*size = read_size;

	return VOD_OK;
}

void 
read_cache_read_completed(read_cache_state_t* state, ssize_t bytes_read)
{
	// update the buffer size
	state->target_buffer->buffer_size = bytes_read;
	state->target_buffer->end_offset = state->target_buffer->start_offset + bytes_read;

	// no longer have an active request
	state->target_buffer = NULL;
}
