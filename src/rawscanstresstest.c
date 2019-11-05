/*
 * < input  rawscanstresstest > output
 *
 * Copy input to output, one line at a time.
 *
 * Compiles cleanly with:
 *   cc -I../include --std=c11 -Wextra -O3 -pedantic -Wall -o rawscanstresstest rawscanstresstest.c -L../lib -lrawscan
 *
 * Paul Jackson
 * pj@usa.net
 * 28 Oct 2019
 */

#include "rawscan.h"

#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int write_failed()
{
    perror("write to stdout");
    exit(1);
}

/*
 * Print one line, skip skip_n lines, print another line, repeat ...
 * where "skip_n" starts at 1 line skipped, and doubles each time
 * (skip 1 line, then 2 lines, then 4 lines, then ...).
 *
 * This demonstrates that we're parsing the input line by line,
 * while keeping the time (cpu or elapsed) spent doing output
 * quite low, so performance is easier to test between various
 * code changes and ways of linking in the rawscan library code.
 *
 * Spoiler: linking in the static librawscan.a library (just
 * a page or so of code) is much faster than dynamically
 * linking with the librawscan.so shared library.
 */

void rawscanstresstest(const char *fname, int fd)
{
    const char *p, *q;
    size_t x;

    allow_rawscan_force_bufsz_env = true;

#   define default_buffer_size (16*4096)
    RAWSCAN *rsp = rs_open(fd, default_buffer_size, '\n');
    rs_enable_pause(rsp);
    p = q = NULL;

    for (;;) {
        RAWSCAN_RESULT rt = rs_getline(rsp);
        switch (rt.type) {
            case rt_full_line:
                // fall through ...
            case rt_start_longline:
                // fall through ...
            case rt_within_longline: {
                if (p == q) {
                    p = rt.line.begin;
                    q = rt.line.end + 1;
                } else {
                    if (q != rt.line.begin) {
                        fprintf(stderr, "Mismatched oldq %p, newq %p\n", p, rt.line.begin);
                        exit(1);
                    }
                    q = rt.line.end + 1;
                }
                break;
            }
            case rt_longline_ended:
                break;
            case rt_paused: {
                x = q - p;
                if (x > 0) {
                    if (write(1, p, x) != x) {
                        perror("write");
                        exit(1);
                    }
                }
                p = q = NULL;
                rs_resume_from_pause(rsp);
                break;
            }
            case rt_eof: {
                x = q - p;
                if (x > 0) {
                    if (write(1, p, x) != x) {
                        perror("write");
                        exit(1);
                    }
                }
                goto do_return;
            }
            case rt_err:
                fprintf(stderr, "%s: %s\n", fname, strerror(rt.errnum));
                goto do_return;
            default:
                fprintf(stderr,"unrecognized rs_getline type %d\n", rt.type);
                goto do_return;
        }
    }

  do_return:
    rs_close(rsp);
    return;
}

#define __unused__ __attribute__((unused))
int main (int argc __unused__, char *argv[] __unused__)
{
    rawscanstresstest ("stdin", 0);
    exit(0);
}
