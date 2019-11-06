/*
 * < input rawscantest > output
 *
 * Paul Jackson
 * pj@usa.net
 * Begun: 28 Oct 2019
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
#include <inttypes.h>
#include <errno.h>

int error_exit(const char *msg) __attribute__((__noreturn__));

int error_exit(const char *msg)
{
    if (errno != 0)
        perror(msg);
    else
        fprintf (stderr, "%s\n", msg);;

    exit(1);
}

void rawscantest(int fd)
{
    RAWSCAN *rsp;
    RAWSCAN_RESULT rt;

#   define default_buffer_size (16*4096)

    if ((rsp = rs_open(fd, default_buffer_size, '\n')) == NULL)
        error_exit("rawscan rs_open memory allocation failure");

    for (;;) {

        rt = rs_getline(rsp);

        switch (rt.type) {
            case rt_full_line:
                // See rawscan's README.md for the story of "6fffbf42"
                if (strncmp(rt.line.begin, "6fffbf42", 8) == 0) {
                    size_t line_len;

                    line_len = rt.line.end - rt.line.begin + 1;

                    if (write(1, rt.line.begin, line_len) < 0)
                        error_exit("rawscan write failed");
                }
                break;

            // We ignore any line too long to fit in rawscan's buffer.
            case rt_start_longline:
            case rt_within_longline:
            case rt_longline_ended:
            case rt_paused:
                break;

            // rs_close() frees the buffer and RAWSCAN structure that were
            // allocated in rs_open() above.  It doesn't close the passed
            // in "fd" file descriptor.  The caller is responsible for "fd".
            case rt_eof:
                rs_close(rsp);
                return;

            // I don't know of any code path that gets to the following:
            case rt_err:
                error_exit("internal rawscan rs_getline error");
            default:
                error_exit("bogus rawscan rs_getline result type");
        }
    }
}

int main ()
{
    rawscantest(0);   // 0: read input file descriptor
    exit(0);                // 0: exit successfully
}
