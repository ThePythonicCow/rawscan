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
    long int x;
    int linenum = 0;
    int next_line_to_write = 1;
    int skip_n = 1;

    allow_rawscan_force_bufsz_env = true;

#   define default_buffer_size (16*4096)
    RAWSCAN *rsp = rawscan_open(fd, default_buffer_size, '\n');
    for (;;) {
        RAWSCAN_RESULT res = rawscan_getline(rsp);

        switch (res.type) {
            case rt_full_line:
                linenum++;
                // fall through ...
            case rt_start_longline:
                // fall through ...
            case rt_within_longline: {
                if (linenum == next_line_to_write) {
                    next_line_to_write += skip_n;
                    skip_n *= 2;
                    x = (res.line.end - res.line.begin + 1);
                    if (write(1, res.line.begin, x) != x)
                        write_failed();
                }
                break;
            }
            case rt_longline_ended: {
                linenum++;
                break;
            }
            case rt_paused:
                fprintf(stderr,"unexpected pause\n");
                goto do_return;
            case rt_eof:
                // printf("%ld\t%s\n", llen, fname);
                goto do_return;
            case rt_err:
                fprintf(stderr, "%s: %s\n", fname, strerror(res.errnum));
                goto do_return;
            default:
                fprintf(stderr,"unrecognized rawscan_getline type %d\n", res.type);
                goto do_return;
        }
    }

  do_return:
    rawscan_close(rsp);
    return;
}

#define __unused__ __attribute__((unused))
int main (int argc __unused__, char *argv[] __unused__)
{
    rawscanstresstest ("stdin", 0);
    exit(0);
}
