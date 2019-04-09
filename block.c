#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
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
	struct blockbuf { char buf[BLOCK_SIZE]; } *p; // != NULL
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
xmalloc(size_t size)
{
	void *ptr = malloc(size);
	if(ptr == NULL) {
		abort();
	}
	return ptr;
}

void*
xcalloc(size_t nmemb, size_t elem_size)
{
	void *ptr = calloc(nmemb, elem_size);
	if(ptr == NULL) {
		abort();
	}
	return ptr;
}

void*
xreallocarray(void *optr, size_t nmemb, size_t elem_size)
{
	// FIXME: overflow check
	void *ptr = realloc(optr, nmemb*elem_size);
	if(ptr == NULL) {
		abort();
	}
	return ptr;
}

void *
blockmove(block_t *dest, const block_t *src, size_t n)
{
	memmove(dest, src, n * sizeof(src[0]));
	return dest;
}

static int
block_append(block_t *blk, int nblk, int maxblk, char *buf, int len /* 0..BLOCK_SIZE */)
{
	assert(nblk <= maxblk);
	assert(maxblk > 0);
	assert(len > 0);
	assert(len <= BLOCK_SIZE);

	if(nblk <= 0) {
		nblk = 1;
	}

	int capacity = BLOCK_SIZE - blk[nblk-1].len;
	if(nblk < maxblk) {
		capacity += BLOCK_SIZE;
	}

	if(capacity < len) {
		return -1;
	}

	capacity = BLOCK_SIZE - blk[nblk-1].len;
	int head_len = MIN(capacity, len);
	memcpy(&blk[nblk-1].p->buf[blk[nblk-1].len], buf, head_len);
	blk[nblk-1].len += head_len;

	nblk++;
	capacity = BLOCK_SIZE;
	int tail_len = len - head_len;
	memcpy(blk[nblk-1].p->buf, &buf[head_len], tail_len);
	blk[nblk-1].len = tail_len;

	return nblk;
}

void
buffer_init(buffer_t *buffer, int nblocks)
{
	// FIXME: check for NULL / xrealloc
	buffer->block = xcalloc(nblocks, sizeof(*buffer->block));
	for(int i = 0; i < nblocks; i++) {
		// FIXME: check for NULL / xmalloc
		buffer->block[i].p = xmalloc(BLOCK_SIZE);
	}
	buffer->nlines = 0;
	buffer->nblocks = nblocks;
}

void
buffer_free(buffer_t *buffer)
{
	for(int i = 0; i < buffer->nblocks; i++) {
		free(buffer->block[i].p);
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
		blk[i].p = xmalloc(BLOCK_SIZE);
		blk[i].len = 0;
		blk[i].nlines = 0;
	}

	// headSEL SEL SELtail
	// copy the head of the first selected block
	int nblk = block_append(blk, 1, LEN(blk), buffer->block[rng->start.blk].p->buf, rng->start.off);

	nblk = block_append(blk, nblk, LEN(blk), mod, len);

	return buffer_read_blocks(buffer, rng, blk, nblk, LEN(blk));
}

int
buffer_read_fd(buffer_t *buffer, range_t *rng, int fd)
{
	struct iovec iov[8];
	// one extra block may be needed for the tail of the selection
	block_t blk[LEN(iov)+1];

	for(unsigned i = 0; i < LEN(blk); i++) {
		// FIXME: check for NULL / xmalloc
		blk[i].p = xmalloc(BLOCK_SIZE);
		blk[i].len = 0;
		blk[i].nlines = 0;
	}

	// headSEL SEL SELtail
	// copy the head of the first selected block
	int nblk = block_append(blk, 1, LEN(blk), buffer->block[rng->start.blk].p->buf, rng->start.off);
	iov[0].iov_base = &blk[0].p->buf[rng->start.off];
	iov[0].iov_len = BLOCK_SIZE - rng->start.off;

	for(unsigned i = 1; i < LEN(iov); i++) {
		iov[i].iov_base = blk[i].p->buf;
		iov[i].iov_len = BLOCK_SIZE;
	}
	ssize_t len = readv(fd, iov, LEN(iov));

	// FIXME: check if correct
	if(len >= 0) {
		nblk = LEN_TO_NBLOCKS(len);
		for(unsigned i = 0; i < nblk-1; i++) {
			blk[i].len = BLOCK_SIZE;
		}
		blk[nblk-1].len = len % BLOCK_SIZE;
	} else {
		nblk = 0;
	}

	return buffer_read_blocks(buffer, rng, blk, nblk, LEN(blk));
}

static size_t
count_chr(const void *buf, int c, size_t len)
{
	size_t n = 0;
	const void *orig_buf = buf;
	const void *next;
	for(;;) {
		next = memchr(buf, c, len - (buf - orig_buf));
		if(next == NULL) {
			break;
		}
		n++;
		buf = next;
		buf++;
	}
	return n;
}

static size_t
index_nrchr(const void *buf, int c, size_t len, size_t nr)
{
	size_t n = 0;
	const void *orig_buf = buf;
	const void *next;
	for(; n < nr;) {
		next = memchr(buf, c, len - (buf - orig_buf));
		if(next == NULL) {
			break;
		}
		n++;
		buf = next;
	}
	return buf - orig_buf;
}

void
buffer_nr_to_address(buffer_t *buffer, int64_t nr, address_t *adr)
{
	// OPTIM?: search backwards if nr > buffer->nlines/2

	int64_t sofar = 0;

	for(int i = 0; i < buffer->nblocks; i++) {
		// FIXME:
		if(sofar >= nr) {
			adr->blk = i;
			adr->off = index_nrchr(buffer->block[i].p->buf, '\n', buffer->block[i].len, sofar - nr);
			return;
		}
		sofar += buffer->block[i].nlines;
	}
}

// it will write to the first block the head of the selection
// it expects extra unused block at the end?
int
buffer_read_blocks(buffer_t *buffer, range_t *rng, block_t *blk, int nmod, int maxblk)
{
	if(nmod == 0) {
		goto out;
	}
	
	int nsel = rng->end.blk - rng->start.blk + 1;

	// prepare the new end
	address_t new_end = {rng->start.blk + nmod - 1, blk[nmod - 1].len};

	// SELtail
	// copy the tail of the last selected block
	block_t *last_sel = &buffer->block[rng->end.blk];
	int tail_len = last_sel->len - rng->end.off;
	nmod = block_append(blk, nmod, maxblk,
		&last_sel->p->buf[rng->end.off], tail_len
	);

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
			nmod = block_append(blk, nmod, maxblk,
				next->p->buf, next_front_len
			);
			// shift the back of the next block
			memmove(
				next->p->buf,
				&next->p->buf[next_front_len],
				next_back_len
			);
			next->len = next_back_len;
			next->nlines = count_chr(next->p->buf, '\n', next->len);
		} else {
			puts("join");
			// join the next block
			nmod = block_append(blk, nmod, maxblk,
				next->p->buf, next->len
			);
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
			free(buffer->block[i].p);
		}
		blockmove(
			&buffer->block[mod_end],
			&buffer->block[sel_end],
			buffer->nblocks - sel_end
		);
	}
	
	buffer->block = xreallocarray(buffer->block,
		buffer->nblocks - nsel + nmod, sizeof(buffer->block[0])
	);

	if(nmod > nsel) {
		blockmove(
			&buffer->block[mod_end],
			&buffer->block[sel_end],
			buffer->nblocks - sel_end
		);
		for(int i = sel_end; i < mod_end; i++) {
			buffer->block[i].len = 0;
			buffer->block[i].nlines = 0;
			buffer->block[i].p = xmalloc(BLOCK_SIZE);
		}
	}

	buffer->nblocks = buffer->nblocks - nsel + nmod;

	for(int i = 0; i < nmod; i++) {
		blk[i].nlines = count_chr(blk[i].p->buf, '\n', blk[i].len);
	}

	for(int i = 0; i < nmod; i++) {
		free(buffer->block[rng->start.blk + i].p);
	}
	memcpy(&buffer->block[rng->start.blk], blk, nmod * sizeof(blk[0]));
	
	rng->end = new_end;

out:
	for(int i = nmod; i < maxblk; i++) {
		free(blk[i].p);
	}
	// FIXME: len!!!
	return len;
}

int
buffer_write_fd(buffer_t *buffer, range_t *rng, int fd)
{
	struct iovec iov[8];
	int nsel = rng->end.blk - rng->start.blk + 1;
	int niov = MIN(nsel, (int)LEN(iov));

	iov[0].iov_base = &buffer->block[rng->start.blk].p->buf[rng->start.off];
	if(nsel == 1) {
		iov[0].iov_len = rng->end.off - rng->start.off;
	} else {
		iov[0].iov_len = buffer->block[rng->start.blk].len - rng->start.off;
	}
	for(int i = 1; i < niov; i++) {
		iov[i].iov_base = buffer->block[rng->start.blk + i].p->buf;
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
	buffer_t buf = {0};
	buffer_init(&buf, 1);
	range_t rng = {0};
	int fd;
	int len;

	fd = open(argv[1], O_RDONLY);
	do {
		len = buffer_read_fd(&buf, &rng, fd);
		rng.start = rng.end;
	} while(len > 0);

	{
		size_t len = 0;
		size_t nl = 0;
		for(int i = 0; i < buf.nblocks; i++) {
			len += buf.block[i].len;
			nl += buf.block[i].nlines;
		}
		fprintf(stderr, "len %zu nl %zu nb %d\n", len, nl, buf.nblocks);
	}

	rng = (range_t){{0, BLOCK_SIZE - 10}, {1, 10}};
	buffer_read(&buf, &rng, "dUPa", 4);

	rng.start = (address_t){0, 0};
	rng.end = (address_t){buf.nblocks-1, buf.block[buf.nblocks-1].len};
	do {
		len = buffer_write_fd(&buf, &rng, 1);
	} while(len > 0);


	buffer_free(&buf);
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

