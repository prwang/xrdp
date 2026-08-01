/**
 * Cork/uncork on a transport (BACKLOG #61f).
 *
 * The contract these tests hold trans_cork()/trans_uncork() to is the one
 * written in common/trans.h, and nothing beyond it:
 *
 *   1. while corked, trans_write_copy_s() makes no socket call at all
 *      -- so the peer sees nothing until the uncork;
 *   2. the bytes and their order are unchanged -- corking is a batching
 *      of system calls, never a change to the stream;
 *   3. the accumulation reaches the send queue as ONE buffer, not as one
 *      node per write;
 *   4. the calls nest, and the flush happens on the outermost uncork;
 *   5. one send is capped at TRANS_MAX_SEND_CHUNK, but a peer that keeps
 *      accepting is drained in chunks until the queue is empty.
 *
 * The expected values come from those four sentences. Nothing here is
 * read off the implementation: the payload is a caller-chosen pattern
 * and the assertion is that the peer receives that same pattern.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <sys/socket.h>
#include <unistd.h>

#include "os_calls.h"
#include "parse.h"
#include "trans.h"

#include "test_common.h"

#define BLOCK_BYTES 1500
#define BLOCK_COUNT 64

/* the pattern the peer must read back: block i is BLOCK_BYTES copies of
   the byte i, so both content and ORDER are checked by one comparison */
static char
pattern_byte(int block)
{
    return (char) (block & 0xff);
}

/**
 * A transport wrapped round one end of a socketpair, with the peer fd
 * returned so a test can read what actually left.
 */
static struct trans *
make_pair(int *peer)
{
    int sck[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sck) != 0)
    {
        return NULL;
    }
    struct trans *t = trans_create(TRANS_MODE_UNIX, 8192, 8192);
    if (t == NULL)
    {
        close(sck[0]);
        close(sck[1]);
        return NULL;
    }
    g_sck_set_non_blocking(sck[0]);
    g_sck_set_non_blocking(sck[1]);
    t->sck = sck[0];
    t->status = TRANS_STATUS_UP;
    *peer = sck[1];
    return t;
}

/**
 * Write one block through the transport.
 */
static int
write_block(struct trans *t, int block)
{
    struct stream *s = trans_get_out_s(t, BLOCK_BYTES + 16);
    int i;

    for (i = 0; i < BLOCK_BYTES; ++i)
    {
        out_uint8(s, pattern_byte(block));
    }
    s_mark_end(s);
    return trans_write_copy_s(t, s);
}

/**
 * Bytes immediately readable on the peer, without waiting.
 */
static int
peer_pending(int peer, char *buf, int max)
{
    int total = 0;
    int got;

    while (total < max)
    {
        got = (int) recv(peer, buf + total, max - total, MSG_DONTWAIT);
        if (got <= 0)
        {
            break;
        }
        total += got;
    }
    return total;
}

/**
 * Drain everything the transport still holds, reading the peer as we go
 * so the socket cannot fill and deadlock the drain.
 */
static int
drain(struct trans *t, int peer, char *buf, int max)
{
    struct stream *empty;
    int total;
    int guard;

    total = peer_pending(peer, buf, max);
    make_stream(empty);
    init_stream(empty, 16);
    s_mark_end(empty);
    for (guard = 0; guard < 100000 && t->wait_s != NULL; ++guard)
    {
        /* trans_write_copy_s() flushes what is waiting before it looks
           at its own (here empty) payload; only ever called while
           something IS waiting, since with an empty queue a zero-length
           write is a no-op the transport reports as an error */
        trans_write_copy_s(t, empty);
        total += peer_pending(peer, buf + total, max - total);
    }
    free_stream(empty);
    total += peer_pending(peer, buf + total, max - total);
    return total;
}

static void
close_pair(struct trans *t, int peer)
{
    trans_delete(t);
    close(peer);
}

/******************************************************************************/
START_TEST(test_trans_cork__nothing_leaves_while_corked)
{
    int peer;
    struct trans *t = make_pair(&peer);
    char buf[BLOCK_BYTES * BLOCK_COUNT];
    int i;

    ck_assert_ptr_ne(t, NULL);
    ck_assert_int_eq(trans_cork(t), 0);
    for (i = 0; i < BLOCK_COUNT; ++i)
    {
        ck_assert_int_eq(write_block(t, i), 0);
    }
    /* contract 1: no socket call was made, so the peer has nothing */
    ck_assert_int_eq(peer_pending(peer, buf, sizeof(buf)), 0);
    /* contract 3: and the send queue has not been touched either */
    ck_assert_ptr_eq(t->wait_s, NULL);

    ck_assert_int_eq(trans_uncork(t), 0);
    /* contract 3: one buffer reached the queue, not BLOCK_COUNT of them.
       It may already have been sent in full, in which case the queue is
       empty -- what must never happen is a chain of nodes. */
    if (t->wait_s != NULL)
    {
        ck_assert_ptr_eq(t->wait_s->next, NULL);
    }
    close_pair(t, peer);
}
END_TEST

/******************************************************************************/
START_TEST(test_trans_cork__bytes_and_order_survive)
{
    int peer;
    struct trans *t = make_pair(&peer);
    char buf[BLOCK_BYTES * BLOCK_COUNT];
    int i;
    int j;
    int got;

    ck_assert_ptr_ne(t, NULL);
    ck_assert_int_eq(trans_cork(t), 0);
    for (i = 0; i < BLOCK_COUNT; ++i)
    {
        ck_assert_int_eq(write_block(t, i), 0);
    }
    ck_assert_int_eq(trans_uncork(t), 0);

    got = drain(t, peer, buf, sizeof(buf));
    /* contract 2: every byte written, and no others */
    ck_assert_int_eq(got, BLOCK_BYTES * BLOCK_COUNT);
    for (i = 0; i < BLOCK_COUNT; ++i)
    {
        for (j = 0; j < BLOCK_BYTES; ++j)
        {
            /* contract 2: in the order they were written */
            ck_assert_int_eq(buf[i * BLOCK_BYTES + j], pattern_byte(i));
        }
    }
    close_pair(t, peer);
}
END_TEST

/******************************************************************************/
START_TEST(test_trans_cork__uncorked_is_unchanged)
{
    int peer;
    struct trans *t = make_pair(&peer);
    char buf[BLOCK_BYTES * BLOCK_COUNT];
    int i;
    int j;
    int got;

    /* the same payload with no cork at all must arrive identically --
       corking is not allowed to be the only way the stream is correct */
    ck_assert_ptr_ne(t, NULL);
    for (i = 0; i < BLOCK_COUNT; ++i)
    {
        ck_assert_int_eq(write_block(t, i), 0);
    }
    got = drain(t, peer, buf, sizeof(buf));
    ck_assert_int_eq(got, BLOCK_BYTES * BLOCK_COUNT);
    for (i = 0; i < BLOCK_COUNT; ++i)
    {
        for (j = 0; j < BLOCK_BYTES; ++j)
        {
            ck_assert_int_eq(buf[i * BLOCK_BYTES + j], pattern_byte(i));
        }
    }
    close_pair(t, peer);
}
END_TEST

/******************************************************************************/
START_TEST(test_trans_cork__nests)
{
    int peer;
    struct trans *t = make_pair(&peer);
    char buf[BLOCK_BYTES * BLOCK_COUNT];
    int got;

    ck_assert_ptr_ne(t, NULL);
    ck_assert_int_eq(trans_cork(t), 0);
    ck_assert_int_eq(trans_cork(t), 0);
    ck_assert_int_eq(write_block(t, 0), 0);
    ck_assert_int_eq(trans_uncork(t), 0);
    /* contract 4: still corked after the inner uncork */
    ck_assert_int_eq(peer_pending(peer, buf, sizeof(buf)), 0);
    ck_assert_int_eq(write_block(t, 1), 0);
    ck_assert_int_eq(trans_uncork(t), 0);

    got = drain(t, peer, buf, sizeof(buf));
    /* contract 4: both blocks, in order, released by the outermost one */
    ck_assert_int_eq(got, BLOCK_BYTES * 2);
    ck_assert_int_eq(buf[0], pattern_byte(0));
    ck_assert_int_eq(buf[BLOCK_BYTES - 1], pattern_byte(0));
    ck_assert_int_eq(buf[BLOCK_BYTES], pattern_byte(1));
    close_pair(t, peer);
}
END_TEST

/******************************************************************************/
/* a trans_send that records what it was offered and accepts all of it */
static int g_offer_count;
static int g_offer_max;
static int g_offer_total;

static int
recording_send(struct trans *self, const char *data, int len)
{
    (void) self;
    (void) data;
    g_offer_count++;
    g_offer_total += len;
    if (len > g_offer_max)
    {
        g_offer_max = len;
    }
    return len;
}

/******************************************************************************/
START_TEST(test_trans_cork__send_is_chunked)
{
    int peer;
    struct trans *t = make_pair(&peer);
    /* four whole chunks and a bit, so the cap has to bind more than once
       and the remainder has to come out too */
    const int payload = TRANS_MAX_SEND_CHUNK * 4 + 1234;
    int blocks = payload / BLOCK_BYTES;
    char sink[64];
    int i;

    ck_assert_ptr_ne(t, NULL);
    t->trans_send = recording_send;
    g_offer_count = 0;
    g_offer_max = 0;
    g_offer_total = 0;

    ck_assert_int_eq(trans_cork(t), 0);
    for (i = 0; i < blocks; ++i)
    {
        ck_assert_int_eq(write_block(t, i), 0);
    }
    ck_assert_int_eq(trans_uncork(t), 0);
    /* contract 5: the cap bounds one SEND, not one call. A peer that
       keeps accepting is fed until the queue is empty, in chunks. */
    ck_assert_int_le(g_offer_max, TRANS_MAX_SEND_CHUNK);
    ck_assert_int_eq(g_offer_total, blocks * BLOCK_BYTES);
    ck_assert_ptr_eq(t->wait_s, NULL);
    /* and it took more than one send to do it, or the cap did nothing */
    ck_assert_int_gt(g_offer_count, 1);

    (void) sink;
    (void) peer;
    close_pair(t, peer);
}
END_TEST

/******************************************************************************/
Suite *
make_suite_test_trans_cork(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("TransCork");

    tc = tcase_create("trans_cork");
    tcase_add_test(tc, test_trans_cork__nothing_leaves_while_corked);
    tcase_add_test(tc, test_trans_cork__bytes_and_order_survive);
    tcase_add_test(tc, test_trans_cork__uncorked_is_unchanged);
    tcase_add_test(tc, test_trans_cork__nests);
    tcase_add_test(tc, test_trans_cork__send_is_chunked);
    suite_add_tcase(s, tc);

    return s;
}
