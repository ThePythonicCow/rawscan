#include <rawscan.h>

/*
 * < input rawscan_test > output
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

        if (write(1, rt.line.begin, line_len) < (ssize_t) line_len)
            error_exit("rawscan write failed");
}

func_static void rawscan_test(int fd, size_t bufsz)
{
    RAWSCAN *rsp;
    RAWSCAN_RESULT rt;
    bool good_long_line = false;
    const char *abc_pattern = "abc";
    const int abc_len = strlen(abc_pattern);
    typedef unsigned short ushort;

    if ((rsp = rs_open(fd, bufsz, '\n')) == NULL)
        error_exit("rawscan rs_open memory allocation failure");

    rs_set_min1stchunklen(rsp, abc_len);

    for (;;) {

        rt = rs_getline(rsp);

        switch (rt.type) {
            case rt_full_line:
            case rt_full_line_without_eol:
                // rust "str.starts_with()" is faster than C "strncmp()",
                // so to beat rust_bstr on many short lines (65536 lines
                // of 8 chars plus '\n') I had to replace the two strncmp()
                // tests below with an unsigned short cast hack, only good
                // as coded on architectures that support unaligned memory
                // access, when matching 3 byte patterns.

                // if (strncmp(rt.line.begin, abc_pattern, abc_len) == 0)

                if (*(ushort *)(rt.line.begin) == *(ushort *)(abc_pattern) &&
                                        rt.line.begin[2] == abc_pattern[2])
                    emit(rt);
                break;
            case rt_start_longline:

                // if (strncmp(rt.line.begin, abc_pattern, abc_len) == 0)

                if (*(ushort *)(rt.line.begin) == *(ushort *)(abc_pattern) &&
                                        rt.line.begin[2] == abc_pattern[2])
                   good_long_line = true;
                // fall through ...
            case rt_within_longline:
                if (good_long_line)
                        emit(rt);
                break;
            case rt_longline_ended:
                good_long_line = false;
                break;
            case rt_paused:
                break;
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

#define default_buffer_size (16*1024)

int main (int argc, char **argv)
{
    size_t bufsz = default_buffer_size;
    extern int optind;
    extern char *optarg;
    int c;

    while ((c = getopt(argc, argv, "b:")) != EOF) {
        char *optend;

        switch (c) {
            case 'b':
                bufsz = strtoul(optarg, &optend, 0);
                if (bufsz < 1 || bufsz > (1<<30)) {
                    fprintf(stderr, "Fatal error: rawscan_static_test: "
                                    "-b bufsz not in [1, %u]\n", (1<<30));
                    exit(1);
                }
                break;
            default:
                fprintf(stderr, "Usage: rawscan_static_test [-b bufsz]\n");
                exit(1);
        }
    }

    rawscan_test(0, bufsz);     // 0: read input file descriptor
    exit(0);                    // 0: exit successfully
}
