#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define LEN(a) (sizeof(a) / sizeof(a)[0])

#define TEST(X) void TEST_##X(void)

#define BLOCK_SIZE 4096

typedef struct {
	int len; // 0..BLOCK_SIZE
	int nlines; // 0..BLOCK_SIZE
	char (*buf)[BLOCK_SIZE]; // != NULL
} block_t;

typedef struct {
	int nblocks; // 1..INT_MAX
	int64_t nlines; // 0..INT64_MAX
	block_t *block; // != NULL
} buffer_t;

typedef struct {
	int blk; // 0..INT_MAX
	int off; // 0..BLOCK_SIZE
} address_t;

// for undo use buffer wide offsets
typedef struct {
	address_t start;
	address_t end;
} range_t;

void*
reallocarray(void *optr, size_t nmemb, size_t elem_size)
{
	// FIXME: overflow check
	return realloc(optr, nmemb*elem_size);
}

void
buffer_init(buffer_t *buffer, int nblocks)
{
	// FIXME: check for NULL / xrealloc
	buffer->block = calloc(nblocks, sizeof(*buffer->block));
	for(int i = 0; i < nblocks; i++) {
		// FIXME: check for NULL / xmalloc
		buffer->block[i].buf = malloc(BLOCK_SIZE);
	}
	buffer->nlines = 0;
	buffer->nblocks = nblocks;
}

void
buffer_free(buffer_t *buffer)
{
	for(int i = 0; i < buffer->nblocks; i++) {
		free(buffer->block[i].buf);
	}
	free(buffer->block);
}

#define LEN_TO_NBLOCKS(x) (((x) + BLOCK_SIZE - 1) / BLOCK_SIZE)

int buffer_read_blocks(buffer_t *buffer, range_t *rng, block_t *blk, int nblk, int len);

int
buffer_read(buffer_t *buffer, range_t *rng, char *mod, int len /* 0..BLOCK_SIZE */)
{
	// one extra block may be needed for the tail of the selection
	block_t blk[3];

	for(unsigned i = 0; i < LEN(blk); i++) {
		// FIXME: check for NULL / xmalloc
		blk[i].buf = malloc(BLOCK_SIZE);
		blk[i].len = BLOCK_SIZE;
	}

	// headSEL SEL SELtail
	// copy the head of the first selected block
	memcpy(blk[0].buf, buffer->block[rng->start.blk].buf, rng->start.off);
	
	// copy the front of mod
	int mod_front_len = MIN(len, BLOCK_SIZE - rng->start.off);
	memcpy(&blk[0].buf[rng->start.off], mod, mod_front_len);

	// copy the back of mod (if at all)
	int mod_back_len = len - mod_front_len;
	memcpy(blk[1].buf, mod, mod_back_len);

	return buffer_read_blocks(buffer, rng, blk, LEN(blk), len);
}

int
buffer_read_fd(buffer_t *buffer, range_t *rng, int fd)
{
	struct iovec iov[8];
	// one extra block may be needed for the tail of the selection
	block_t blk[LEN(iov)+1];

	for(unsigned i = 0; i < LEN(blk); i++) {
		// FIXME: check for NULL / xmalloc
		blk[i].buf = malloc(BLOCK_SIZE);
		blk[i].len = BLOCK_SIZE;
	}

	// headSEL SEL SELtail
	// copy the head of the first selected block
	memcpy(blk[0].buf, buffer->block[rng->start.blk].buf, rng->start.off);
	iov[0].iov_base = &blk[0].buf[rng->start.off];
	iov[0].iov_len = BLOCK_SIZE - rng->start.off;

	for(unsigned i = 1; i < LEN(iov); i++) {
		iov[i].iov_base = blk[i].buf;
		iov[i].iov_len = BLOCK_SIZE;
	}
	ssize_t len = readv(fd, iov, LEN(iov));
	
	return buffer_read_blocks(buffer, rng, blk, LEN(blk), len);
}

// it will write to the first block the head of the selection
// it expects extra unused block at the end?
int
buffer_read_blocks(buffer_t *buffer, range_t *rng, block_t *blk, int nblk, int len)
{
	int nmod = 0;
	
	if(len < 0) {
		goto out;
	}
	
	int nsel = rng->end.blk - rng->start.blk + 1;

	int total = rng->start.off + len; // 0..BLOCK_SIZE*LEN(blk)
	nmod = LEN_TO_NBLOCKS(total);
	blk[nmod-1].len = total % BLOCK_SIZE;
	int last_capacity = BLOCK_SIZE - blk[nmod-1].len;

	// prepare the new end
	address_t new_end = {rng->start.blk + nmod, blk[nmod - 1].len};

	// SELtail
	// copy the tail of the last selected block as much as possible to the last modified
	int tail_len = buffer->block[rng->end.blk].len - rng->end.off;
	int tail_front_len = MIN(tail_len, last_capacity);
	memcpy(
		&blk[nmod-1].buf[blk[nmod-1].len],
		&buffer->block[rng->end.blk].buf[rng->end.off],
		tail_front_len
	);
	total += tail_front_len;

	nmod = LEN_TO_NBLOCKS(total);

	// copy the rest of the tail to the extra block (if at all)
	int tail_back_len = tail_len - tail_front_len;
	memcpy(
		&blk[nmod-1].buf,
		&buffer->block[rng->end.blk].buf[rng->end.off+tail_front_len],
		tail_back_len
	);
	total += tail_back_len;

	nmod = LEN_TO_NBLOCKS(total);

	blk[nmod-1].len = total % BLOCK_SIZE;

	// if last modified block would be too small
	if(rng->end.blk < buffer->nblocks-1 &&
		blk[nmod-1].len < BLOCK_SIZE/2
	) {
		// take some from the next block (after the selected one)
		block_t *next = &buffer->block[rng->end.blk+1];

		if(blk[nmod-1].len + next->len > BLOCK_SIZE) {
			int next_back_len = (blk[nmod-1].len + next->len) / 2;
			int next_front_len = next->len - next_back_len;
			// take the front of the next block
			memcpy(
				&blk[nmod-1].buf[blk[nmod-1].len],
				next->buf,
				next_front_len
			);
			blk[nmod-1].len += next_front_len;
			// shift the back of the next block
			memmove(
				next->buf,
				&next->buf[next_front_len],
				next_back_len
			);
			next->len = next_back_len;
			// FIXME: next->nlines
		} else {
			// join the next block
			memcpy(
				&blk[nmod-1].buf[blk[nmod-1].len],
				next->buf,
				next->len
			);
			blk[nmod-1].len += next->len;
			// FIXME: undo does not need the next buffer, for now it would be copied because nsel is incremented
			next->len = 0;
			next->nlines = 0;
			nsel++;
		}
	}
	
	// TODO: undo

	int sel_end = rng->start.blk + nsel;
	int mod_end = rng->start.blk + nmod;

	if(nmod < nsel) {
		for(int i = mod_end; i < sel_end; i++) {
			free(buffer->block[i].buf);
		}
		memmove(
			&buffer->block[mod_end],
			&buffer->block[sel_end],
			(buffer->nblocks - sel_end) * sizeof(buffer->block[0])
		);
	}
	
	buffer->nblocks = buffer->nblocks - nsel + nmod;
	// FIXME: check for NULL / xreallocarray
	buffer->block = reallocarray(buffer->block,
		buffer->nblocks, sizeof(buffer->block[0])
	);
	
	if(nmod > nsel) {
		memmove(
			&buffer->block[mod_end],
			&buffer->block[sel_end],
			(buffer->nblocks - sel_end) * sizeof(buffer->block[0])
		);
		for(int i = sel_end; i < mod_end; i++) {
			buffer->block[i].len = 0;
			buffer->block[i].nlines = 0;
			// FIXME: check for NULL / xmalloc
			buffer->block[i].buf = malloc(BLOCK_SIZE);
		}
	}
	
	memcpy(&buffer->block[rng->start.blk], blk, nmod * sizeof(blk[0]));
	
	rng->end = new_end;

out:
	for(int i = nmod; i < nblk; i++) {
		free(blk[i].buf);
	}
	return len;
}

int
buffer_write_fd(buffer_t *buffer, range_t *rng, int fd)
{
	struct iovec iov[8];
	int nsel = rng->end.blk - rng->start.blk + 1;
	int niov = MIN(nsel, (int)LEN(iov));

	iov[0].iov_base = &buffer->block[rng->start.blk].buf[rng->start.off];
	if(nsel == 1) {
		iov[0].iov_len = rng->end.off - rng->start.off;
	} else {
		iov[0].iov_len = buffer->block[rng->start.blk].len - rng->start.off;
	}
	for(int i = 1; i < niov; i++) {
		iov[i].iov_base = buffer->block[rng->start.blk + i].buf;
		iov[i].iov_len = buffer->block[rng->start.blk + i].len;
	}
	if(nsel > 1 && nsel == niov) {
		iov[nsel-1].iov_len = rng->end.off;
	}
	ssize_t len = writev(fd, iov, niov);
	if(len < 0) {
		return -1;
	}

	int rest = len;
	for(int i = 0; i < niov; i++) {
		rest -= iov[i].iov_len;
		if(rest <= 0) {
			rng->start.blk += i;
			if(i == 0) {
				rest += rng->start.off;
			}
			rng->start.off = iov[i].iov_len + rest;
			return len;
		}
	}

	// error?
	return -2;
}

int
main(int argc, char *argv[])
{
	
	return 0;
}

/*
// Get a block position for an address in a buffer
// Potential optimalizations:
// - begin search from block index and line number
// 	could be useful having just return a block index for a line number
// 	additional parameters: uint32_t start_block, uint64_t start_nr
// - search from the end for adr->nr > buffer->nlines/2
block_pos_t
buffer_address_to_block_pos(buffer_t *buffer, address_t *adr)
{	// 2= [0]_1_2__   0= _____   2= __3__4_
	uint64_t nr = 0;
	for(uint32_t i = 0; i < buffer->nblocks; i++) {
		nr += buffer->block[i].nlines;
		if(nr >= adr.nr) {
			char *prev_line = buffer->block[i].data.buf;
			char *line = buffer->block[i].data.buf;
			uint16_t rest = buffer->block[i].len;
			for(nr -= buffer->block[i].nlines; ; nr++) {
				line = memchr(prev_line, '\n', rest);
				if(line != NULL) {
					line++;
					rest -= line - prev_line;
				} else {
					assert(1 || "broken line count cache");
					return BLOCK_NOT_FOUND;
				}
				if(nr == adr.nr) {
					uint16_t off;
					if(rest <= adr.off) {
						off = //this block
					}
					return (block_pos_t){i, off};
				}
				line = prev_line;
			}
			return BLOCK_NOT_FOUND;
		}
	}
	return BLOCK_NOT_FOUND;
}
*/

