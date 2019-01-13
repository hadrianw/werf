#if 0
exec gcc -Og -g -std=c99 -Wall -Wextra -pedantic block.c -o block
#endif

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
block_append(block_t *blk, int nblk, const int maxblk, char *buf, int len /* 0..BLOCK_SIZE */)
{
	fprintf(stderr, "ap %d\n", maxblk);
	assert(nblk <= maxblk);
	assert(maxblk > 0);
	assert(len >= 0);
	assert(len <= BLOCK_SIZE);

	if(nblk <= 0) {
		nblk = 1;
	}

	if(len == 0) {
		return nblk;
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

	capacity = BLOCK_SIZE;
	int tail_len = len - head_len;
	if(tail_len > 0) {
		nblk++;
		memcpy(blk[nblk-1].p->buf, &buf[head_len], tail_len);
		blk[nblk-1].len = tail_len;
	}

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

int buffer_read_blocks(buffer_t *buffer, range_t *rng, block_t *blk, int nblk, int maxblk, int len);

int
buffer_read(buffer_t *buffer, range_t *rng, char *mod, int len /* 0..BLOCK_SIZE */)
{
	// one extra block may be needed for the tail of the selection
	block_t blk[4];

	for(unsigned i = 0; i < LEN(blk); i++) {
		blk[i].p = xmalloc(BLOCK_SIZE);
		blk[i].len = 0;
		blk[i].nlines = 0;
	}

	// headSEL SEL SELtail
	// copy the head of the first selected block
	int nblk = block_append(blk, 1, LEN(blk), buffer->block[rng->start.blk].p->buf, rng->start.off);

	nblk = block_append(blk, nblk, LEN(blk), mod, len);

	return buffer_read_blocks(buffer, rng, blk, nblk, LEN(blk), len);
}

int
buffer_read_fd(buffer_t *buffer, range_t *rng, int fd)
{
	struct iovec iov[8];
	// one extra block may be needed for the tail of the selection
	block_t blk[LEN(iov)+1];

	fprintf(stderr, "so %zu %zu\n", LEN(iov), LEN(blk));

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
		for(int i = 0; i < nblk-1; i++) {
			blk[i].len = BLOCK_SIZE;
		}
		blk[nblk-1].len = len % BLOCK_SIZE;
	} else {
		nblk = 0;
	}

	return buffer_read_blocks(buffer, rng, blk, nblk, LEN(blk), len);
}

static size_t
count_chr(const void *buf, int c, size_t len)
{
	size_t n = 0;
	const char *pbuf = buf;
	const char *orig_buf = buf;
	const char *next;
	for(;;) {
		next = memchr(pbuf, c, len - (pbuf - orig_buf));
		if(next == NULL) {
			break;
		}
		n++;
		pbuf = next;
		pbuf++;
	}
	return n;
}

static size_t
index_nrchr(const void *buf, int c, size_t len, size_t nr)
{
	size_t n = 0;
	const char *pbuf = buf;
	const char *orig_buf = buf;
	const char *next;
	for(; n < nr;) {
		next = memchr(buf, c, len - (pbuf - orig_buf));
		if(next == NULL) {
			break;
		}
		n++;
		pbuf = next;
	}
	return pbuf - orig_buf;
}

// it will write to the first block the head of the selection
// it expects extra unused block at the end?
int
buffer_read_blocks(buffer_t *buffer, range_t *rng, block_t *blk, int nmod, const int maxblk, int len)
{
	if(len < 0) {
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

	int mod_nlines_new = 0;
	int mod_nlines_old = 0;

	for(int i = 0; i < nmod; i++) {
		blk[i].nlines = count_chr(blk[i].p->buf, '\n', blk[i].len);
		mod_nlines_new += blk[i].nlines;

		mod_nlines_old += buffer->block[rng->start.blk + i].nlines;
		free(buffer->block[rng->start.blk + i].p);
	}
	memcpy(&buffer->block[rng->start.blk], blk, nmod * sizeof(blk[0]));
	
	buffer->nlines += mod_nlines_new - mod_nlines_old;
	rng->end = new_end;

out:
	for(int i = nmod; i < maxblk; i++) {
		free(blk[i].p);
	}
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

int64_t
buffer_address_move_off(buffer_t *buffer, address_t *adr, int64_t move);
void
buffer_address_move_lines(buffer_t *buffer, address_t *adr, int64_t move);
void
buffer_nr_to_address(buffer_t *buffer, int64_t nr, address_t *adr);
void
buffer_nr_off_to_address(buffer_t *buffer, int64_t nr, int64_t off, address_t *adr);
static int
block_count_nl(char *buf, int len, int *nl_off);
void
buffer_address_to_nr_off(buffer_t *buffer, address_t *adr, int64_t *nr, int64_t *off);

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
		fprintf(stderr, ".nl %ld\n", buf.nlines);
	}

	// replace on a block boundary
	rng = (range_t){{0, BLOCK_SIZE - 10}, {1, 10}};
	buffer_read(&buf, &rng, "dUPa", 4);

	for(int i = 0; i < buf.nblocks; i++) {
		fprintf(stderr, "%d: %d %d\n", i, buf.block[i].len, buf.block[i].nlines);
	}
	fprintf(stderr, ".nl %ld\n", buf.nlines);

	// more blocks
	// less blocks

	rng.start = (address_t){0, 0};
	rng.end = (address_t){buf.nblocks-1, buf.block[buf.nblocks-1].len};
	do {
		len = buffer_write_fd(&buf, &rng, 1);
	} while(len > 0);

	int64_t nr, off;
	buffer_address_to_nr_off(&buf, &rng.end, &nr, &off);
	fprintf(stderr, "adr %d %d n/o %ld %ld\n",
		rng.end.blk, rng.end.off,
		nr, off);

	buffer_free(&buf);
	return 0;
}

int64_t
buffer_address_move_off(buffer_t *buffer, address_t *adr, int64_t move)
{
	assert(move >= 0);

	for(int i = adr->blk; i < buffer->nblocks; i++) {
		int rest = buffer->block[i].len - adr->off;
		if(rest >= move) {
			adr->blk = i;
			adr->off += move;
			return 0;
		}
		move -= rest;
		adr->off = 0;
	}
	// move at this point will be > 0
	// as it's a reminder over the end of file
	return move;
}

void
buffer_address_move_lines(buffer_t *buffer, address_t *adr, int64_t move)
{
	char *nl;

	if(buffer->block[adr->blk].nlines != 0 &&
		(nl = memchr(
		&buffer->block[adr->blk].p->buf[adr->off],
		'\n', buffer->block[adr->blk].len - adr->off)) != NULL
	) {
		adr->off = nl - buffer->block[adr->blk].p->buf;
		buffer_address_move_off(buffer, adr, 1);
		return;
	}

	int i;
	for(i = adr->blk + 1; i < buffer->nblocks &&
		buffer->block[i].nlines == 0; i++);

	if(i >= buffer->nblocks) {
		// no next new lines
		return;
	}

	adr->blk = i;
	nl = memchr(buffer->block[i].p->buf, '\n', buffer->block[i].len);
	adr->off = nl - buffer->block[i].p->buf;
	buffer_address_move_off(buffer, adr, 1);
}

void
buffer_nr_to_address(buffer_t *buffer, int64_t nr, address_t *adr)
{
	// OPTIM?: search backwards if nr > buffer->nlines/2

	if(nr == 0) {
		adr->blk = 0;
		adr->off = 0;
		return;
	}

	int64_t sofar = 0;

	for(int i = 0; i < buffer->nblocks; i++) {
		sofar += buffer->block[i].nlines;
		if(nr < sofar) {
			adr->blk = i;
			adr->off = index_nrchr(buffer->block[i].p->buf, '\n', buffer->block[i].len, sofar - nr);
			buffer_address_move_off(buffer, adr, 1);
			return;
		}
	}
}

void
buffer_nr_off_to_address(buffer_t *buffer, int64_t nr, int64_t off, address_t *adr)
{
	buffer_nr_to_address(buffer, nr, adr);
	buffer_address_move_off(buffer, adr, off);
}

static int
block_count_nl(char *buf, int len, int *nl_off)
{
	int count;
	char *nl;

	count = count_chr(buf, '\n', len);
	if(count == 0) {
		return 0;
	}

	nl = memrchr(buf, '\n', len);
	*nl_off = nl - buf;
	if(*nl_off == len - 1) {
		count--;
		nl = memrchr(buf, '\n', *nl_off);
		*nl_off = nl - buf;
		if(nl == NULL) {
			return 0;
		}
	}
	return count;
}

void
buffer_address_to_nr_off(buffer_t *buffer, address_t *adr, int64_t *nr, int64_t *off)
{
	int i = 0;
	char *nl;
	int nl_off;
	*nr = 0;

	for(; i < adr->blk; i++) {
		*nr += buffer->block[i].nlines;
	}
	int count;
	if(buffer->block[i].nlines > 0 &&
		(count = block_count_nl(buffer->block[i].p->buf, adr->off+1, &nl_off)) > 0
	) {
		*nr += count;
		*off = adr->off - (nl_off + 1);
	} else {
		*off = adr->off;
		for(i--; i >=0 && buffer->block[i].nlines == 0; i--) {
			*off += buffer->block[i].len;
		}
		if(i >= 0) {
			nl = memrchr(buffer->block[i].p->buf, '\n', buffer->block[i].len);
			nl_off = nl - buffer->block[i].p->buf;
			*off += buffer->block[i].len - (nl_off + 1);
		}
	}
}

/*
nr and offset to buffer address
// Get a block position for an address in a buffer
// Potential optimalizations:
// - begin search from block index and line number
// 	could be useful having just return a block index for a line number
// 	additional parameters: uint32_t start_block, uint64_t start_nr
// - search from the end for adr->nr > buffer->nlines/2
*/

