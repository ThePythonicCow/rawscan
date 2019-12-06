#include <rawscan_static.h>

/*
 * < input rawscan_static_test > output
 *
 * Paul Jackson
 * pj@usa.net
 * Begun: 28 Oct 2019
 */

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

func_static int error_exit(const char *msg) __attribute__((__noreturn__));

func_static int error_exit(const char *msg)
{
    if (errno != 0)
        perror(msg);
    else
        fprintf (stderr, "%s\n", msg);;

    exit(1);
}

func_static void emit(RAWSCAN_RESULT rt)
{
        size_t line_len;

        // "+ 1" because line.end points at trailing eol byte,
        // not one byte past that.
        line_len = rt.line.end - rt.line.begin + 1;

        if (fwrite(rt.line.begin, 1, line_len, stdout) < line_len)
            error_exit("rawscan write failed");
}

func_static void rawscan_test(int fd)
{
    RAWSCAN *rsp;
    RAWSCAN_RESULT rt;
    bool good_long_line = false;

#   define default_buffer_size (16*4096)

    if ((rsp = rs_open(fd, default_buffer_size, '\n')) == NULL)
        error_exit("rawscan rs_open memory allocation failure");

    for (;;) {

        rt = rs_getline(rsp);

        switch (rt.type) {
            case rt_full_line:
            case rt_full_line_without_eol:
                if (strncmp(rt.line.begin, "abc", 3) == 0)
                    emit(rt);
                break;

            case rt_start_longline:
                good_long_line = (strncmp(rt.line.begin, "abc", 3) == 0);
                if (good_long_line)
                        emit(rt);
                break;
            case rt_within_longline:
                if (good_long_line)
                        emit(rt);
                break;
            case rt_longline_ended:
                good_long_line = false;
                break;
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
    rawscan_test(0);   // 0: read input file descriptor
    exit(0);           // 0: exit successfully
}
