/*
 * rawscan - read input, one line at a time, quickly and safely.
 *
 * As of this writing, in November of 2019, this code is only known
 * to compile on Linux using gcc.  Compile has no warnings if using
 * "gcc -Wextra -pedantic -Wall" flags.  Requires "gcc --std=c11"
 * (C11) for (at least) anonymous unions.
 *
 * This code is packaged in an unusual way, that allows for two
 * different ways of using it:
 *
 *  1) The "normal" way, in which the using application includes the
 *     "rawscan.h" header file that describes the interface, and then
 *     links with a "librawscan.so" shared library, which implements
 *     the rawscan library routines to open, read, close, and
 *     otherwise operate on streams containing the lines to be read.
 *
 *  2) The "faster" way, in which the using application only needs
 *     to include this present file, "rawscan_static.h".  As you can
 *     see below, this file contains the actual implementations.
 *     Compared to a "normal" dynamic linking, this "static"
 *     linking shaves about 5 to 25 per-cent off the runtime cost
 *     (user CPU cycles) of using this library, thanks to the more
 *     aggressive (if using -O3) optimizations that gcc can do, when
 *     calling into "static" subroutines in the same source file as
 *     the caller.  Gcc can drop all pretense of providing externally
 *     callable entry points for "static" subroutines in same source
 *     file, which enables it to shave cycles off their invocation.
 *     This "static" linking does however add roughly 4000 bytes to
 *     the size of the applications executable binary text segment.
 *
 * Don't worry about code duplication of the implementation of
 * the rawscan library routines.  The one and only implementation
 * appears below in this "rawscan_static.h" header file.  The file
 * that appears to be the source for the librawscan.so shared library
 * is just two lines long, which two lines include this present file
 * with a special "building_rawscan_dynamic_library" symbol defined
 * in order to remove a few "func_static" function attributes below.
 *
 * Paul Jackson
 * pj@usa.net
 * Begun: 28 Oct 2019
 */

// Need C11 for anonymous unions
#define _POSIX_C_SOURCE 200112L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stddef.h>

/* Need glibc Feature Test Macro _GNU_SOURCE to pick up rawmemchr() */
#define __USE_GNU
#include <string.h>

#ifndef _bool_defined_
#define _bool_defined_
    typedef enum {
        false,
        true
    } bool;
#endif

// cmake debug builds enable asserts (NDEBUG not defined),
// whereas cmake release builds define NDEBUG to disable asserts.
#include <assert.h>

/*
 * Use "rawscan" rs_*() routines to scan input, line by
 * line, faster than "Standard IO" stdio buffered input routines.
 *
 * The "raw" means that these routines don't use any of the buffered
 * stdio apparatus, but rather directly invoke the read(2) system
 * call.  The "scan" means that these routines handle input only,
 * not output.
 *
 * rs_getline() reads large chunks into a buffer, and returns
 * portions of that buffer terminated by nul, newline, or whatever
 * other "delimiterbyte" that rawscan stream is configured for.
 * These portions are called "lines" below, though they can
 * be any sequence of bytes terminated either by the specified
 * delimiterbyte, or at the very end of the input stream, if that
 * last byte is not that rawscan stream's specified delimiterbyte.
 *
 * Except as noted in the pause/resume discussion below, whenever
 * rs_getline() finds that it only has a partial line left in the
 * upper end of its buffer, it then tries to move that partial
 * line lower down in its buffer and continue.
 *
 * Thus rs_getline() is not "zero-copy", but "infrequent copy",
 * so long as it's configured in the rs_open() call to have an
 * internal buffer that is usually longer than the typical line it
 * will be returning.
 *
 * This strategy relies on having a fixed upper length to the line
 * length that we need to parse conveniently (in one piece), and
 * being willing to accept returns of any lines longer than that
 * length in multiple chunks, rather than as a single string.
 *
 * Unless (1) using the optional pause/resume states and (2)
 * layering suitable mutual exclusion locks over the rawscan streams
 * implemented here, ordinary usage of rawscan on any particular
 * input stream must be single threaded.  If two threads invoked
 * rs_getline() on the same RAWSCAN stream, then one of these
 * calls could cause the buffered data already returned to the
 * other thread to be moved or overwritten, while the other thread
 * was still accessing it.
 *
 * Multi-thread access to a RAWSCAN stream might be safe under the
 * management of a wrapper that handled the synchronizing locking,
 * and that copied out data being returned on a rs_getline() call
 * to the invoking thread's private data area, before returning.
 * A sufficiently sophisticated such wrapper manager could use the
 * pause/resume facility in order to allow parallel read-only access
 * to the buffer, while locking and single threading rs_getline()
 * calls that update the thread, and joining or blocking all
 * other threads from read-only access during a rs_getline()
 * and rs_resume_from_pause() whenever the rawscan stream paused.
 * Any such multi-threading of this library is beyond the scope of
 * this current code, but perhaps could be implemented on top of
 * this current code.
 *
 * The rawscan calling routine can gain some control over when the
 * contents of the buffer are invalidated by using the optional
 * pause/resume states.  First invoke rs_enable_pause() on the
 * RAWSCAN stream.  Then whenever a rs_getline() or similar call
 * would need to invalidate the current contents of the buffer for
 * that stream, that rs_getline() call will instead return with
 * a RAWSCAN_RESULT.type of rt_paused, without invalidating the
 * current buffer contents.  When the calling routine has finished
 * using or copying out whatever data it's still needs from the
 * RAWSCAN buffer, it can then call the rs_resume_from_pause()
 * function on that stream, which will unpause the stream and enable
 * subsequent rs_getline() calls to return more lines.
 *
 * For applications that can ignore overly long lines, the rawscan
 * interface makes that quick and easy to do so.  Just ignore
 * RAWSCAN_RESULT's with result types of rt_start_longline,
 * rt_within_longline or rt_longline_ended.
 *
 * One key technique that can be used to obtain excellent performance
 * (several times fewer user CPU cycles than stdio buffer based
 * solutions) is the use of routines such as strchr, strchrnul,
 * memchr, or rawmemchr to scan considerable lengths of input. These
 * routines are very heavily optimized, both by gcc, and further by
 * the Intel(tm) MultiMedia eXtension (MMX) and Intel(tm) Advanced
 * Vector Extensions (AVX) instructions. Whenever a problem can
 * be reduced to scanning large spans of buffer looking for a
 * single particular character, these routines can race through
 * the data at maximum speed, operating on data perhaps 128, 256
 * or 512 bits at a time, depending on compiler and CPU technology.
 * This is significantly faster than doing character at a time:
 *
 *      while ((c = getchar()) != EOF) switch (c) { ... }
 * loops in hand-coded C from a STDIO input buffer, with multiple
 * tests for state and the value of each character 'c' in each
 * loop iteration.
 *
 * The fastest scanner in glibc of strchr, strchrnul, memchr, or
 * rawmemchr is rawmemchr, as it -only- stops scanning when it sees
 * the requested character.  The x86_64 assembly code in glibc for
 * these routines is very fast, at least on "modern" (recent years
 * as of this writing in 2019) Intel and AMD x86_64 processors
 * with AVX vector instructions.  Rawmemchr will happily run to,
 * and beyond, the limits of the memory segment it's searching,
 * causing a SIGSEGV or other such crash, if the character it's
 * looking for is not found sooner.  So the rawscan implementation
 * sets up a read- only page, just past the main buffer, with the
 * configured delimiterbyte in the first byte of that read-only
 * page, ensuring that calls to rawmemchr() will terminate there,
 * if not sooner, regardless of what's in the buffer at the time.
 *
 * The return value from rs_getline() is the RAWSCAN_RESULT
 * structure below.
 *
 * When returning a line, the RAWSCAN_RESULT.end pointer will
 * point to the newline '\n' character (or whatever delimiterbyte
 * was established in the rs_open() call) that ends the line
 * being returned.  If the caller wants that newline replaced with
 * (for example) a nul, such as when using the returned line as a
 * potential filename to be passed back into the kernel as a nul-
 * terminated pathname string, then the caller can overwrite that
 * byte, directly in the returned buffer.
 *
 * Lines (sequences of bytes ending in the delimiterbyte
 * byte) returned by rs_getline() are byte arrays defined by
 * [RAWSCAN_RESULT.begin, RAWSCAN_RESULT.end], inclusive.
 * They reside somewhere in a heap allocated buffer of the size
 * specified in the rs_open() call.
 *
 * That heap allocated buffer is freed in the rs_close() call,
 * invalidating any previously returned rs_getline() results.  That
 * heap allocated buffer is never moved, once setup in the rs_open()
 * call, until the rs_close() call.  But subsequent rs_getline()
 * calls may invalidate data in that buffer by overwriting or
 * shifting it downward.  So while accessing stale results from an
 * earlier rs_getline() call, after additional calls to rs_getline(),
 * prior to the rs_close() of that stream, won't directly cause an
 * invalid memory access, such accesses might still return invalid
 * data, unless carefully sequenced using the pause/resume facility.
 */

/*
 * "func_static":
 *
 * Used to disable static attribute on functions when profiling or
 * building as the dynamic library librawscan.so.
 *
 * For easier profiling, also optimize no more than cc -O1 (try
 * -g -Og).  This will let one see individual subroutines in gprof
 * and gcov results.
 *
 * When profiling this code OR when building a dynamic shared
 * library, we need to turn off the "static" label on the main
 * exported routines (rs_open, rs_getline, rs_close, ...) so that
 * those function entry points are visible to the profiler or
 * linker tools.
 *
 * When doing neither (not profiling and only compiling static
 * inline) then we want to make those main rs_*() routines static,
 * so that the optimizer can more agressively inline and optimize
 * them, knowing that no one outside of the present compilation
 * unit can see or call them.
 *
 * The preprocessor symbol "building_rawscan_dynamic_library" is
 * set in the tiny, two line, lib/rawscan.c source for what becomes
 * the librawscan.so shared library ... which then gets its "real"
 * source from this rawscan_static.h header file.
 */

#define profiling_this_code 1   // set to '1' and rebuild to profile

#if profiling_this_code || building_rawscan_dynamic_library
#define func_static
#else
#define func_static static
#endif

// We had to define (just above) "func_static" before including
// "rawscan.h", in order to have the "static" or not attribute of the
// rs_*() routines declared in rawscan.h match their corresponding
// definitions, seen below, in this file.

#include <rawscan.h>

bool allow_rawscan_force_bufsz_env = false;

func_static RAWSCAN *rs_open (
  int fd,              // read input from this file descriptor
  size_t bufsz,        // handle lines at least this many bytes in one chunk
  char delimiterbyte)  // newline '\n' or other char marking end of "lines"
{
    RAWSCAN *rsp;        // build new RAWSCAN struct here
    void *arena;         // allocate new buffers here
    size_t pgsz;         // hardware memory page size
    size_t arenasz;      // total number bytes to allocate
    char *buftop;        // l.u.b. of buf, start of read-only sentinel page
    char *envbufszstr;   // getenv _RAWSCAN_FORCE_BUFSZ_ override
    size_t envbufsz;     // _RAWSCAN_FORCE_BUFSZ_ as an integer

    // We must allocate enough memory to hold the requested
    // bufsz buffer, _plus_ a page aligned page immediately
    // above that buffer, to serve as the sentinel page
    // with a copy of the delimiterbyte in it first byte,
    // and with its permissions changed to read-only.
    //
    // There are various ways we could do this, using the various
    // malloc, memalign, and sbrk calls available to us.
    //
    // I choose to round up the requested buffer size to the next
    // multiple of a page size, and then increase that by one more
    // page for the sentinel page.  This entire allocation we call
    // the "arena".  The user requested buffer will be in the bottom
    // N pages of this N+1 page allocation, and will be placed
    // so that its top ends exactly at the top of those N pages.
    // One byte past that buffer will be the read-only sentinel
    // byte with a copy of the specified delimiterbyte.
    //
    // Our reads into this "bufsz" buffer, and the return of lines
    // by rs_getline() from that buffer, will walk their way up
    // that buffer, until such time as the next line that would be
    // returned does not fit in the remaining buffer.  At that point,
    // if not using the pause/resume logic, and if the partial line
    // we have so far in the buffer does not already entirely fill
    // the buffer, we shift that partial line down to the beginning
    // of the buffer and continue reading the rest of that line into
    // the freed up space higher in the buffer.
    //
    // If the rs_getline() routine finds that it cannot shift a
    // partial line down any further, because that line is longer than
    // fits in the buffer, then rs_getline() begins "too long line"
    // processing, returning the line in partial chunks.  This only
    // happens when an input line is longer than bufsz.

    // The read-only protections on the sentinel page must be
    // on page aligned boundaries.

    pgsz = sysconf(_SC_PAGESIZE);

    // For more efficient testing, especially testing the
    // critically fussy code where a "line" ends within a couple
    // of bytes of the upper bound of page aligned buf, it's
    // quicker to test with a tiny bufsz. Also might want to test
    // larger buf sizes.  Let an environment variable override
    // bufsz.  Only do if global allow_rawscan_force_bufsz is
    // first set true by caller, to better protect against covert
    // DOS attack.  Caller should not allow_rawscan_force_bufsz
    // in production code.

#ifdef _gigabyte_
#error _gigabyte_ already defined so can not redefine
#endif
#   define _gigabyte_ (1024UL*1024UL*1024UL)
    if ((allow_rawscan_force_bufsz_env == true) &&
        (envbufszstr = getenv("_RAWSCAN_FORCE_BUFSZ_")) != NULL &&
        (envbufsz = atoi(envbufszstr)) >= 1 && // can't do < 1 byte bufsz's
        (envbufsz <= (2UL * _gigabyte_))) {    // doesn't do > 2Gb bufsz's
            bufsz = envbufsz;
    }
#   undef _gigabyte_

// Round up x to nearest multiple of y
#ifdef _Rnd
#error _Rnd already defined so can not redefine
#endif
#   define _Rnd(x,y)    (((((unsigned long)(x))+(y)-1)/(y))*(y))

    arenasz = _Rnd(bufsz, pgsz) + pgsz;  // allocate this many bytes to arena

#   undef _Rnd

    // Allocate arenasz bytes of memory, on pgsz alignment ...
    if (posix_memalign(&arena, pgsz, arenasz) != 0) {
        return NULL;
    }

    // The top page becomes a read-on sentinel page, with a copy
    // of the delimiterbyte in its first byte, to ensure that
    // rawmemchr() scans don't run off allowed memory.
    buftop = (char *)arena + arenasz - pgsz;
    *buftop = delimiterbyte;

    // Protect our sentinel delimiterbyte from stray writes:
    if (mprotect (buftop, pgsz, PROT_READ) < 0) {
        free(arena);
        return NULL;
    }

    if ((rsp = calloc(1, sizeof(RAWSCAN))) == NULL) {
        free(arena);
        return NULL;
    }

    rsp->fd = fd;
    rsp->arena = arena;
    rsp->pgsz = pgsz;
    rsp->bufsz = bufsz;
    rsp->buftop = buftop;
    rsp->buf = buftop - bufsz;
    rsp->delimiterbyte = delimiterbyte;
    rsp->p = rsp->q = rsp->buf;
    rsp->end_this_chunk = NULL;
    rsp->in_longline = false;
    rsp->longline_ended = false;
    rsp->eof_seen = false;
    rsp->err_seen = false;
    rsp->errnum = 0;
    rsp->pause_on_inval = false;
    rsp->stop_this_pause = false;

    assert (((uintptr_t)(rsp->buftop) % pgsz) == 0);
    assert (rsp->buf >= (const char *)arena);
    assert (rsp->buf + rsp->bufsz == rsp->buftop);

    return rsp;
}

func_static void rs_close(RAWSCAN *rsp)
{
    /* We don' t close rsp->fd ... we got it open and we leave it open */
    (void) mprotect ((char *)(rsp->buftop), rsp->pgsz, PROT_READ|PROT_WRITE);
    free(rsp->arena);
    free(rsp);
}

// Suppress warnings if the pause/resume functions aren't used.
#define __unused__ __attribute__((unused))

__unused__ func_static void rs_enable_pause(RAWSCAN *rsp)
{
    rsp->pause_on_inval = true;
}

__unused__ func_static void rs_disable_pause(RAWSCAN *rsp)
{
    rsp->pause_on_inval = false;
    rsp->stop_this_pause = false;
}

__unused__ func_static void rs_resume_from_pause(RAWSCAN *rsp)
{
    rsp->stop_this_pause = true;
}

/*
 * Return next "line" from rsp input.
 *
 * The return value of rs_getline() is a RAWSCAN_RESULT
 * structure, various fields of which will be valid, depending on
 * the value of that struct's first field, "type", an enum of type
 * rs_result_type.
 *
 * In the most common case, the RAWSCAN_RESULT type will be
 * rt_full_line, the RAWSCAN_RESULT begin pointer will point
 * to the first character (byte) in a line, and the
 * RAWSCAN_RESULT end pointer will point to the
 * delimiterbyte, as specfied in the rs_open() call, such as
 * the '\n' or '\0', that ends that line.
 *
 * This code guarantees that the returned byte array,
 * including the end of line delimiter byte, is in writable
 * memory, in case for example caller wants to replace the
 * trailing newline with a different byte, such as a nul,
 * or in case the caller wants to trim trailing whitespace.
 *
 * This code does not guarantee that any bytes, even one byte
 * above the returned array, are writable.  Indeed, sometimes,
 * depending on the input and on the buffer size, the very next
 * byte past a returned byte array will be in the read-only
 * sentinel page, just above the main buffer.
 *
 * The heap memory holding the returned character array ("line")
 * will remain valid at least until the next rs_getline() or
 * rs_close() call on that same RAWSCAN stream, but not
 * necessarily longer.  The rs_getline() caller may modify
 * any bytes in a such a returned array between calls, but should
 * not modify any other bytes that are in that buffer but outside
 * the range of bytes [RAWSCAN.begin, RAWSCAN.end],
 * at risk of confusing the line scanning and parsing logic on
 * subsequent rs_getline() calls.
 *
 * If the input stream didn't end with the delimiter byte, e.g. a
 * file without a final newline, then the RAWSCAN_RESULT.end
 * for the rs_getline() call that returns the final line will
 * be pointing into the buffer at the last character that was read
 * from the input, that last character won't be the specified
 * delimiterbyte in this case, but rather it will be whatever
 * other character was the stream's final character, and the
 * RAWSCAN_RESULT.type field will be "rt_full_line (or
 * rt_longline_ended)" after the last line.  If rs_getline() is
 * called one more time on such a stream, then the type field for
 * that result will finally be set to "rt_eof".
 *
 * Regarding the "reset pause/resume logic" noted in a few comments
 * below, here's the sequence of events that result in this reset:
 *
 *  1) rawscan pauses incoming processing, to not overwrite
 *      already returned data the caller might still be using
 *  2) caller copies out or finishes using any such data
 *  3) caller invokes the rs_resume_from_pause() call
 *      to tell rawscan it's done with any such buffered data
 *  4) rs_resume_from_pause() sets "rsp->stop_this_pause = true;"
 *      telling rawscan that it is safe to overwrite buffered
 *      data in order to reuse buffer space
 *  5) "rsp->stop_this_pause" is left set to "true" for a while,
 *      preventing more pauses
 *  6) only when rt_getline() is about to return more data
 *      (either as a full line or a chunk of a long line)
 *      will it set "rsp->stop_this_pause = false;", once again
 *      enabling a pause to happen, the next time that rawscan
 *      has to overwrite more of what it's already returned.
 */

// Private helper routines used by rs_getline():

static RAWSCAN_RESULT rawscan_full_line(RAWSCAN *rsp)
{
    // The "normal" case - return another full line all at once.
    // The line to return is [rsp->p, rsp->end_this_chunk].
    // *(rsp->end_this_chunk) is either a delimiterbyte or
    //    else we're at end of file and it's the last byte.

    RAWSCAN_RESULT rt;

    assert(rsp->p != NULL);
    assert(rsp->next_val_p != NULL);
    assert(rsp->end_this_chunk != NULL);

    assert(rsp->p <= rsp->end_this_chunk);
    assert(rsp->next_val_p <= rsp->q);
    assert(rsp->end_this_chunk <= rsp->q);
    assert(rsp->end_this_chunk <= rsp->buftop);
    assert(*rsp->end_this_chunk == rsp->delimiterbyte || rsp->eof_seen);

    if (*rsp->end_this_chunk == rsp->delimiterbyte)
        rt.type = rt_full_line;
    else
        rt.type = rt_full_line_without_eol;
    rt.line.begin = rsp->p;
    rt.line.end = rsp->end_this_chunk;

    rsp->p = rsp->next_val_p;
    rsp->stop_this_pause = false;        // reset pause/resume logic

    return rt;
}

static RAWSCAN_RESULT rawscan_eof(RAWSCAN *rsp)
{
    RAWSCAN_RESULT rt;

    rt.type = rt_eof;
    rsp->eof_seen = true;

    return rt;
}

static RAWSCAN_RESULT rawscan_err(RAWSCAN *rsp)
{
    RAWSCAN_RESULT rt;

    rt.type = rt_err;
    rt.errnum = rsp->errnum;
    assert(rsp->err_seen == true);

    return rt;
}

static const char *rawscan_read (RAWSCAN *rsp)
{
    int cnt;

    cnt = read (rsp->fd, (void *)(rsp->q), rsp->buftop - rsp->q);

    if (cnt < 0) {
        rsp->errnum = errno;
        rsp->err_seen = true;
        return NULL;
    } else if (cnt == 0) {
        rsp->eof_seen = true;
        return NULL;
    } else {
        const char *pre_read_q = rsp->q;
        rsp->q += cnt;
        return pre_read_q;      // returns to start_next_rawmemchr_here
    }
}

static RAWSCAN_RESULT rawscan_start_of_longline(RAWSCAN *rsp)
{
    RAWSCAN_RESULT rt;

    assert(rsp->p != NULL);
    assert(rsp->next_val_p != NULL);
    assert(rsp->end_this_chunk != NULL);

    assert(rsp->p <= rsp->end_this_chunk);       // non-empty return chunk

    assert(rsp->in_longline == false);
    assert(rsp->longline_ended == false);

    rt.type = rt_start_longline;
    rt.line.begin = rsp->p;
    rt.line.end = rsp->end_this_chunk;

    rsp->p = rsp->next_val_p;

    rsp->in_longline = true;
    rsp->longline_ended = false;

    rsp->end_this_chunk = NULL;     // force rs_getline to set again
    rsp->next_val_p = NULL;         // force rs_getline to set again

    rsp->stop_this_pause = false;        // reset pause/resume logic

    return rt;
}

static RAWSCAN_RESULT rawscan_within_longline(RAWSCAN *rsp)
{
    RAWSCAN_RESULT rt;

    assert(rsp->p != NULL);
    assert(rsp->next_val_p != NULL);
    assert(rsp->end_this_chunk != NULL);

    assert (rsp->in_longline == true);
    // rsp->longline_ended might be true or false

    assert(rsp->p <= rsp->end_this_chunk);       // non-empty return chunk

    rt.type = rt_within_longline;
    rt.line.begin = rsp->p;
    rt.line.end = rsp->end_this_chunk;
    rsp->p = rsp->next_val_p;

    rsp->end_this_chunk = NULL;     // force rs_getline to set again
    rsp->next_val_p = NULL;         // force rs_getline to set again

    rsp->stop_this_pause = false;        // reset pause/resume logic

    return rt;
}

static RAWSCAN_RESULT rawscan_terminate_longline(RAWSCAN *rsp)
{
    RAWSCAN_RESULT rt;

    // To keep the interface to rs_getline() simple(r),
    // we never both (1) return another chunk of a longline, and
    // (2) tell the caller that this is the end of a longline,
    // in the same response.  All longline responses either
    //   (1) start a new longline, with one chunk of data,
    //   (2) return yet another chunk of data, or (exclusive or)
    //   (3) terminate, with no more data, a longline.
    // This routine handles case (3).  It is only called by
    // rawscan_handle_end_of_longline(), which distinguishes
    // between doing both (2) and (3), versus doing just (3),
    // and sequences the control in either case.

    assert(rsp->in_longline == true);
    assert(rsp->longline_ended == true);

    assert(rsp->next_val_p == NULL);
    assert(rsp->end_this_chunk == NULL);

    rsp->in_longline = false;
    rsp->longline_ended = false;

    rt.type = rt_longline_ended;
    rt.line.begin = rt.line.end = NULL;

    return rt;
}

static RAWSCAN_RESULT rawscan_paused()
{
    RAWSCAN_RESULT rt;

    rt.type = rt_paused;

    return rt;
}

static void rawscan_shift_buffer_contents_down(RAWSCAN *rsp)
{
    size_t howfartoshift = rsp->p - rsp->buf;
    size_t howmuchtoshift = rsp->q - rsp->p;

    const char *old_p = rsp->p;
    const char *new_p = rsp->p - howfartoshift;
    const char *new_q = rsp->q - howfartoshift;

    assert (howmuchtoshift > 0);

    // only call rawscan_shift_buffer_contents_down if buf
    // no longer has any free space at its top
    assert(rsp->q == rsp->buftop);

    assert(new_q == rsp->buf + howmuchtoshift);

    memmove((void *)new_p, old_p, howmuchtoshift);
    rsp->p = new_p;
    rsp->q = new_q;

    assert(rsp->q < rsp->buftop);
}

static RAWSCAN_RESULT rawscan_handle_part_of_longline(
    RAWSCAN *rsp, const char *end_this_part)
{
    rsp->end_this_chunk = end_this_part - 1;
    rsp->next_val_p = end_this_part;

    if (rsp->in_longline) {
        return rawscan_within_longline(rsp);
    } else {
        return rawscan_start_of_longline(rsp);
    }
}

static RAWSCAN_RESULT rawscan_handle_end_of_longline(RAWSCAN *rsp)
{
    // If we come upon the end of a longline, either by finding a
    // delimiterbyte in the input, or by hitting the end of a file
    // lacking a trailing newline, we might have one more chunk of
    // that longline to return to the caller, before telling them
    // in a separate response that the longline ended, or we might
    // not have any more such data and need to immediately tell
    // the caller that the currently active longline just
    // terminated.  The "rsp->longline_ended = true" setting below
    // triggers the "if (rsp->longline_ended)" code at the top of
    // rs_getline(), in order to get back here for the second
    // step (the longline termination) after returning the last
    // longline data chunk to the caller in the first step.

    if ( ! rsp->longline_ended ) {
        rsp->longline_ended = true;
        return rawscan_within_longline(rsp);
    } else {
        return rawscan_terminate_longline(rsp);
    }
}

// verbose bool flags for current RAWSCAN status:

typedef struct {
    bool have_more_chars_in_buf;
    bool found_a_delimiterbyte;
    bool end_of_input_seen;
    bool have_bytes_in_p_q;
    bool have_space_above_q;
    bool have_space_below_p;
} rawscan_internal_status_t;

func_static RAWSCAN_RESULT rs_getline (RAWSCAN *rsp)
{
    rawscan_internal_status_t status_struct;
    rawscan_internal_status_t *pstat;
    const char *next_delim_ptr;
    const char *start_next_rawmemchr_here;

    if (rsp->in_longline) {
        // finish off two-step longline termination
        if (rsp->longline_ended) {
            return rawscan_handle_end_of_longline(rsp);
        }
        start_next_rawmemchr_here = rsp->p;
        goto slow_loop;
    }

    start_next_rawmemchr_here = rsp->p;

  fast_loop:

    // We have to call rawmemchr() almost every rs_getline() call,
    // but we try to avoid calling it more often than we have to,
    // and we try to avoid rescanning any data twice.

    next_delim_ptr = rawmemchr(start_next_rawmemchr_here, rsp->delimiterbyte);
    assert(next_delim_ptr != NULL);

    // fastpath the two common cases, where performance counts most:
    //      1) We have a full line in buffer, ready to return.
    //      2) We have a partial line, and room in buffer to read more.

    if (rsp->p < rsp->q) {
        if (next_delim_ptr < rsp->q) {
            // return full line
            RAWSCAN_RESULT rt;

            rt.type = rt_full_line;
            rt.line.begin = rsp->p;
            rt.line.end = next_delim_ptr;
            rsp->p = next_delim_ptr + 1;
            rsp->stop_this_pause = false;        // reset pause/resume logic
            return rt;
        } else if (rsp->q < rsp->buftop) {
            // have space above q: read more and try again
            const char *next_q;

            if ((next_q = rawscan_read(rsp)) != NULL) {
                start_next_rawmemchr_here = next_q;
                goto fast_loop;
            } // else fall into the slow loop ...
        } // else fall into the slow loop ...
    } // else fall into the slow loop ...

    // Falling into the slow loop, to handle all the rare or corner
    // cases.  Now pedantic exhaustive clarity matters more than speed.

  slow_loop:

    next_delim_ptr = rawmemchr(start_next_rawmemchr_here, rsp->delimiterbyte);
    assert(next_delim_ptr != NULL);

    // Phase 1: Express current status using above verbose terms.

    pstat = &status_struct;

    pstat->have_more_chars_in_buf = rsp->p < rsp->q;

    if (pstat->have_more_chars_in_buf) {
        // Now next_delim_ptr points to one of:
        //   1) next delimiter byte in [p, q),
        //   2) some stale delimiter byte in [q, buftop), or
        //   3) the read only sentinel delimiter at buftop.
        // We're only interested in case (1) here:
        pstat->found_a_delimiterbyte = (next_delim_ptr < rsp->q);
    } else {
        pstat->found_a_delimiterbyte = false;
    }

    pstat->end_of_input_seen = rsp->eof_seen || rsp->err_seen;
    pstat->have_bytes_in_p_q = rsp->p < rsp->q;
    pstat->have_space_above_q = rsp->q < rsp->buftop;
    pstat->have_space_below_p = (rsp->p > rsp->buf);

    // Phase 2: Choose and invoke next action, based on status
    //          "Rawscanner - the heart of your input."
    //          "Refill your coffee cup before reading this."

    if (pstat->found_a_delimiterbyte) {
        assert(next_delim_ptr < rsp->q);
        rsp->end_this_chunk = next_delim_ptr;
        rsp->next_val_p = next_delim_ptr + 1;
        if (rsp->in_longline) {
            return rawscan_handle_end_of_longline(rsp);
        } else {
            return rawscan_full_line(rsp);
        }
    } else if (pstat->end_of_input_seen) {
        if (pstat->have_more_chars_in_buf) {
            assert (pstat->have_space_above_q);
            // We know that we "have_space_above_q" because we've seen
            // "end_of_input_seen", which we only see in rawscan_read(),
            // which is only called when we "have_space_above_q" into
            // which to read.
            rsp->end_this_chunk = rsp->q - 1;
            rsp->next_val_p = rsp->q;
            if (rsp->in_longline) {
                return rawscan_handle_end_of_longline(rsp);
            } else {
                return rawscan_full_line(rsp);
            }
        } else if (rsp->in_longline) {
            rsp->longline_ended = true;
            return rawscan_handle_end_of_longline(rsp);
        } else if (rsp->eof_seen) {
            return rawscan_eof(rsp);
        } else {
            return rawscan_err(rsp);
        }
    } else if (pstat->have_space_above_q) {
        const char *next_q;

        if ((next_q = rawscan_read(rsp)) != NULL) {
            start_next_rawmemchr_here = next_q;
        }
        goto slow_loop;
    } else if (pstat->have_bytes_in_p_q) {
        if (pstat->have_space_below_p) {
            if (rsp->pause_on_inval && !rsp->stop_this_pause) {
                return rawscan_paused(rsp);
            } else {
                rawscan_shift_buffer_contents_down(rsp);
                start_next_rawmemchr_here = rsp->q;
                goto slow_loop;
            }
        } else {
            return rawscan_handle_part_of_longline(rsp, rsp->q);
        }
    } else {
        // We have more input, but no where to put it.  Our buffer
        // is stuffed with lines we've already returned to our caller.
        // Time to reset buffers, or pause awaiting a resume.
        if (rsp->pause_on_inval && !rsp->stop_this_pause) {
            return rawscan_paused(rsp);
        } else {
            rsp->p = rsp->q = rsp->buf;    // reset buffers
            start_next_rawmemchr_here = rsp->q;
            goto slow_loop;
        }
    }

    // Not reached: every code path above returns or goes to "slow_loop".
}
