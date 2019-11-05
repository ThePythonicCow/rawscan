#include <stddef.h>

#ifndef _RAWSCAN_H
#define _RAWSCAN_H 1

#ifndef _bool_defined_
#define _bool_defined_
    typedef enum {
        false,
        true
    } bool;
#endif

/*
 * The internal state of a rawscan stream:
 */

typedef struct {
    int fd;                // open file descriptor to be read
    void *arena;           // memory allocated arena
    size_t pgsz;           // hardware memory page size
    size_t bufsz;          // handle lines at least this long
    const char *bufa;      // lower bufsz buffer
    const char *bufb;      // upper bufsz buffer
    const char *bufbtop;   // l.u.b. of bufb; put read-only delimiterbyte here
    char delimiterbyte;    // byte @ end of "lines" (e.g. '\n' or '\0')
    const char *p, *q;     // [begin, end) of not yet returned chars

    // When rs_getline() calls a subroutine to return the next
    // line or chunk (part of a line too long to fit in buffer)
    // then it must tell the subroutine:
    //  1) ptr to first byte in chunk/line: rsp->p
    //  2) ptr to last byte in chunk/line: rsp->end_this_chunk
    //  3) new value of rsp->p: next byte in buf, else rsp->q if none

    const char *end_this_chunk;  // ptr to last byte in this line/chunk
    const char *next_val_p;      // start next chunk/line (or rsp->q if none)

    bool in_longline;      // seen begin of too long line, but not yet end
    bool longline_ended;   // end of long line seen
    bool eof_seen;         // eof seen - can no longer read into buffer
    bool err_seen;         // read err seen - can no longer read into buffer
    int errnum;            // errno of last read if failed
    bool pause_on_inval;   // pause when need to invalidate buffer
    bool resume_now;       // resume from current pause
} RAWSCAN;

enum rs_result_type {
    // The RAWSCAN_RESULT->line begin and end fields are valid:
    rt_full_line,          // one entire line
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
    enum rs_result_type type;

    union {
        struct {
            const char *begin;       // ptr to first byte in line or chunk
            const char *end;         // ptr to last byte in line or chunk
        } line;

        int errnum;                  // errno of last read if failed
    };
} RAWSCAN_RESULT;

RAWSCAN *rs_open (
  int fd,              // read input from this (already open) file descriptor
  size_t bufsz,        // handle lines at least this many bytes in one chunk
  char delimiterbyte   // newline '\n' or other byte marking end of "lines"
);

void rs_close(RAWSCAN *rsp);
void rs_enable_pause(RAWSCAN *rsp);
void rs_disable_pause(RAWSCAN *rsp);
void rs_resume_from_pause(RAWSCAN *rsp);
RAWSCAN_RESULT rs_getline (RAWSCAN *rsp);

// By default, don't allow bufsz env override. Only commands that
// set allow_rawscan_force_bufsz_env to true at runtime allow it.
// If allowed, then rawscan library code overrides rs_open()
// bufsz specified by calling application with whatever value is
// set in the environment variable _RAWSCAN_FORCE_BUFSZ_ (if that
// variable is set to a positive integer value.)

extern bool allow_rawscan_force_bufsz_env;

#endif /* _RAWSCAN_H */
