#ifndef _RAWSCAN_H
#define _RAWSCAN_H 1

// If who ever included us didn't explicitly define "func_static",
// then make it disappear.  The one known (as of this writing) case
// defining "func_static" is our inclusion from the rawscan_static.h
// header file, where the various rs_*() routines are defined to be
// "static", so must be so declared here.  "func_static" will be
// #define'd to be "static" when rawscan_static.h includes us.

#ifndef func_static
#define func_static
#endif

// stddef.h: needed for "size_t" (long unsigned int)

#include <stddef.h>

// pick up bool, false, true (available in C since C99)

#include <stdbool.h>

typedef struct RAWSCAN RAWSCAN; // support opaque pointers to RAWSCAN structs

// Enumerate the various kinds of RAWSCAN_RESULT's that rs_getline returns.

enum rs_result_type {
    // The RAWSCAN_RESULT->line begin and end fields are valid:
    rt_full_line,          // one entire line
    rt_full_line_without_eol, // one entire line w/o delimiterbyte at end
    rt_start_longline,     // first chunk in a long line
    rt_within_longline,    // another chunk in this long line
    rt_longline_ended,     // no more chunks in this long line

    // No further RAWSCAN_RESULT fields are valid:
    rt_paused,             // getline()'s a no-op until resume called
    rt_eof,                // end of file, no more data available

    // The RAWSCAN_RESULT->errnum field is valid:
    rt_err,                // end of data due to read error
};

/*
 * rs_getline() returns a copy of the following structure:
 */

typedef struct {
    enum rs_result_type type;        // one of the above enumerated types

    union {                          // anon union -- need C11 or better
        struct {
            const char *begin;       // ptr to first byte in line or chunk
            const char *end;         // ptr to last byte in line or chunk
        } line;

        int errnum;                  // errno of last read if failed
    };
} RAWSCAN_RESULT;

func_static RAWSCAN *rs_open (
  int fd,              // read input from this (already open) file descriptor
  size_t bufsz,        // main input buffer size
  char delimiterbyte   // newline '\n' or other byte marking end of "lines"
);

func_static void rs_close(RAWSCAN *rsp);
func_static void rs_enable_pause(RAWSCAN *rsp);
func_static void rs_disable_pause(RAWSCAN *rsp);
func_static void rs_resume_from_pause(RAWSCAN *rsp);
func_static RAWSCAN_RESULT rs_getline (RAWSCAN *rsp);
func_static int rs_set_min1stchunklen(RAWSCAN *rsp, size_t min1stchunklen);
func_static size_t rs_get_min1stchunklen(RAWSCAN *rsp);

#endif /* _RAWSCAN_H */
