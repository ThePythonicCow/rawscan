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
 *
 * The rawscan project README.md file provides performance
 * measurement results, comparing these two ways of using rawscan,
 * and also comparing rawscan with other common line scanning
 * options.
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
#include <stddef.h>

/* Need glibc Feature Test Macro _GNU_SOURCE to pick up rawmemchr() */
#define __USE_GNU
#include <string.h>

 /* Need glibc Feature Test Macro __USE_MISC to pick up sbrk(), brk() */
#define __USE_MISC
#include <unistd.h>

// cmake debug builds enable asserts (NDEBUG not defined),
// whereas cmake release builds define NDEBUG to disable asserts.
#include <assert.h>

/*
 * Use "rawscan" rs_*() routines to scan input, line by
 * line, faster than "Standard IO" stdio buffered input routines.
 *
 * The "raw" means that these routines don't use any of the buffered
 * stdio apparatus, but rather directly invoke the read(2) system
 * call.  The "scan" means that these routines only handle input.
 *
 * rs_getline() reads large chunks into a buffer, and returns
 * portions of that buffer terminated by nul, newline, or whatever
 * other "delimiterbyte" that rawscan stream is configured for.
 *
 * These chunks are called "lines" below, though they can
 * be any sequence of bytes terminated either by the specified
 * delimiterbyte, or at the very end of the input stream, if that
 * last byte is not that rawscan stream's specified delimiterbyte.
 *
 * Except as noted in the pause discussion below, whenever
 * rs_getline() finds that it only has a partial line left in the
 * upper end of its buffer, it then tries to move that partial line
 * lower down in its buffer and continue reading, hoping to find a
 * return that line in full, or at least the first min1stchunklen
 * chunk of the line, if too long for its buffer.
 *
 * Thus rs_getline() is not "zero-copy", but "infrequent copy", so
 * long as it's configured in the rs_open() call to have an internal
 * buffer that is usually long enough to several lines at once.
 *
 * This strategy relies on having a fixed upper length to the line
 * length that we need to parse conveniently (in one piece), and
 * being willing to accept returns of any lines longer than that
 * length in multiple chunks, rather than as a single string.
 *
 * Unless (1) using the optional pause states and (2) layering
 * suitable mutual exclusion locks over the rawscan streams
 * implemented here, ordinary usage of rawscan on any particular
 * input stream must be single threaded.  If two threads invoked
 * rs_getline() on the same RAWSCAN stream, then one of these calls
 * could cause the buffered data already returned to the other
 * thread to be moved or overwritten, while the other thread was
 * still accessing it.  That would result in "Undefined Behavior".
 *
 * Multi-thread access to a RAWSCAN stream might be safe under the
 * management of a wrapper that handled the synchronizing locking,
 * and that copied out data being returned on a rs_getline() call
 * to the invoking thread's private data area, before returning.
 * A sufficiently sophisticated such wrapper manager could use the
 * pause facility in order to allow parallel read-only access to the
 * buffer, while locking and single threading rs_getline() calls that
 * update the thread, and joining or blocking all other threads from
 * read-only access during a rs_getline() and rs_resume_from_pause()
 * whenever the rawscan stream paused.  Any such multi-threading
 * of this library is beyond the scope of this current code, but
 * perhaps could be implemented on top of this current code.
 *
 * The rawscan calling routine can gain some control over when the
 * contents of the buffer are invalidated by using the optional pause
 * states.  First invoke rs_enable_pause() on the RAWSCAN stream.
 * Then whenever a rs_getline() call would need to invalidate the
 * current contents of the buffer for that stream, that rs_getline()
 * call will instead return with a RAWSCAN_RESULT.type of rt_paused,
 * without invalidating the current buffer contents.  When the
 * calling routine has finished using or copying out whatever data
 * it's still needs from the RAWSCAN_RESULT buffer, it can then call
 * the rs_resume_from_pause() function on that stream, which will
 * unpause the stream and enable the next subsequent rs_getline()
 * call to return the next line.
 *
 * For applications that can ignore overly long lines, the rawscan
 * interface makes that quick and easy to do so.  Just ignore
 * RAWSCAN_RESULT's with result types of rt_start_longline,
 * rt_within_longline or rt_longline_ended.
 *
 * One key technique that is often used to obtain excellent
 * performance in input scanning routines is the use of routines such
 * as strchr, strchrnul, memchr, or rawmemchr to scan considerable
 * lengths of input. These routines are very heavily optimized, both
 * by gcc, and further by the Intel(tm) MultiMedia eXtension (MMX)
 * and Intel(tm) Advanced Vector Extensions (AVX) instructions.
 * Whenever a problem can be reduced to scanning large spans of
 * buffer looking for a single particular character, these routines
 * can race through the data at maximum speed, operating on data
 * perhaps 128, 256 or 512 bits at a time, depending on compiler
 * and CPU technology.
 * 
 * This is significantly faster than doing such character at a time
 * looping as:
 *
 *      while ((c = getchar()) != EOF) switch (c) { ... }
 * 
 * in hand-coded C from a STDIO input buffer, which has multiple
 * tests for state and the value of each character 'c' in each
 * loop iteration.
 *
 * The fastest scanner in glibc of strchr, strchrnul, memchr,
 * or rawmemchr is rawmemchr, as it -only- stops scanning when it
 * sees the requested character.  The x86_64 assembly code in glibc
 * for each of these routines is very fast, at least on "modern"
 * (recent years as of this writing in 2019) Intel and AMD x86_64
 * processors with AVX vector instructions.  But rawmemchr is
 * (well, should be) the fastest of these, with the limitation
 * that it will happily run to, and beyond, the limits of the
 * memory segment it's searching, potentially causing a SIGSEGV
 * or other such crash if the character for which it is looking
 * is not found sooner.  So this rawscan implementation sets up a
 * read-only page, just past the main buffer, with the configured
 * delimiterbyte in the first byte of that read-only page, ensuring
 * that calls to rawmemchr() will terminate there, if not sooner,
 * regardless of what's in the buffer at the time.
 *
 * The return value from rs_getline() is the RAWSCAN_RESULT
 * structure defined in rawscan.h.
 *
 * Perhaps the biggest downside to this rawscan implementation
 * is that the code to loop over and handle the returns from
 * rs_getline() appears at first glance to be quite a bit more
 * complex than the usual couple of lines it takes to loop over
 * lines of input using other interfaces.  One has to handle,
 * perhaps with a switch() statement, some eight different return
 * types from rs_getline(), to write correct and complete code.
 * However (at least in the probably biased view of its author)
 * that code is easy to write correctly, and less prone to some of
 * the coding errors, such as buffer overruns and off by one errors,
 * that can plague other input scanners.
 *
 * The result should be more robust, and more performant, code
 * when writing tools that scan input a line (or other such byte
 * terminated sequence) at a time.
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
 * [RAWSCAN_RESULT.begin, RAWSCAN_RESULT.end], inclusive.  They
 * reside somewhere in an allocated buffer of the size specified
 * in the rs_open() call.
 *
 * That memory allocated buffer is never moved, once setup in the
 * rs_open() call.  But subsequent rs_getline() calls may invalidate
 * data in that buffer by overwriting or shifting it downward.
 * So while accessing stale results from an earlier rs_getline()
 * call, after additional calls to rs_getline(), prior to the
 * rs_close() of that stream, won't directly cause an invalid
 * memory access, such accesses might still return invalid data,
 * unless carefully sequenced using the pause facility.
 */

/*
 * "func_static":
 *
 * Used to disable static attribute on functions when profiling or
 * building as the dynamic library librawscan.so.
 *
 * For easier profiling, also optimize no more than cc -O1 (try
 * such gcc options as -g, -Og, and/or --coverage).  This will let
 * one see individual subroutines in gprof and gcov results.
 *
 * When profiling this code OR when building a dynamic shared
 * library, we need to turn off the "static" label on the main
 * exported routines (rs_open, rs_getline, rs_close, ...) so that
 * those function entry points are visible to the profiler or
 * linker tools.
 *
 * When doing neither profiling or building a shared library, then
 * we want to make those main rs_*() routines static, so that the
 * optimizer can more agressively inline and optimize them, knowing
 * that no one outside of the present compilation unit can see or
 * call them.
 *
 * The preprocessor symbol "building_rawscan_dynamic_library" is
 * set in the tiny, two line, lib/rawscan.c source for what becomes
 * the librawscan.so shared library ... which then gets its "real"
 * source from this rawscan_static.h header file.
 */

#ifndef profiling_this_code
#define profiling_this_code 0   // define as '1' and rebuild to profile
#endif

#if profiling_this_code || building_rawscan_dynamic_library
#define func_static
#else
#define func_static static inline
#endif

// We had to define (just above) "func_static" _before_ including
// "rawscan.h" (below), in order to have the "static" or not
// attribute of the rs_*() routines declared in rawscan.h match
// their corresponding definitions, seen below, in this file.

#include <rawscan.h>

/*
 * The following internal rawscan code uses all of the following
 * "RAWSCAN" structure.  It also passes opaque pointers to that
 * structure back and forth to its callers as the type "RAWSCAN *",
 * and it's rs_getline() function returns copies (structure copy,
 * not pointer to structure) of the RAWSCAN_RESULT result structure
 * that is embedded within the "RAWSCAN" structure.
 *
 * This _does_ mean however that the details of RAWSCAN are
 * exposed to applications that include this <rawscan_static.h>
 * file directly (probably to gain performance).  It would be
 * Undefined Behavior for such applications to depend on the
 * internals or even the size of the "RAWSCAN" structure.
 *
 * Such applications using <rawscan_static.h> would of course need
 * to be recompiled to make use of changes to the "RAWSCAN"
 * structure or internal rawscan code in different versions of
 * rawscan, but on the other hand, existing such application binaries
 * would have no dependency on any librawscan.so dynamic library.
 */

typedef struct RAWSCAN {
    const char *buf;        // bufsz buffer
    const char *buftop;     // l.u.b. of buf; put read-only delimiterbyte here
    const char *p, *q;      // [begin, end) of not yet returned chars in buf

    int fd;                 // open file descriptor to read rawscan input from
    int errnum;             // stashed errno from failed system calls

    size_t pgsz;            // hardware memory page size
    size_t bufsz;           // main input buffer size
    size_t min1stchunklen;  // guaranteed min len of first chunk of long line

    // When rs_getline() calls a subroutine to return the next
    // line or chunk (part of a line too long to fit in buffer)
    // then it must tell the subroutine:
    //  1) ptr to first byte in chunk/line: rsp->p
    //  2) ptr to last byte in chunk/line: rsp->end_this_chunk
    //  3) new value of rsp->p: next byte in buf, else rsp->q if none

    const char *end_this_chunk;  // ptr to last byte in this line/chunk
    const char *next_val_p;      // start next chunk/line (or rsp->q if none)

    // cache one next_delim_ptr ahead if in fast loop, to
    // optimize returning many short lines from one buffer read

    const char *next_delim_ptr_peek;

    RAWSCAN_RESULT result;  // rs_getline() returns a copy of this result

    char delimiterbyte;     // byte @ end of "lines" (e.g. '\n' or '\0')
    bool in_longline;       // seen begin of too long line, but not yet end
    bool terminate_current_pause;  // resume from current pause

    bool longline_ended;    // end of long line seen
    bool eof_seen;          // eof seen - can read no more into buffer
    bool err_seen;          // read err seen - can read no more into buffer
    bool pause_on_inval;    // pause when need to invalidate buffer
} RAWSCAN;

// We must allocate enough memory to hold:
//  1) our RAWSCAN *rsp structure,
//  2) our input buffer, and
//  3) a read-only sentinel page just above this buffer.
//
// The sentinel page will have copy of the delimiterbyte in its
// first byte, with its permissions changed to read-only.
//
// There are various ways we could do this, using the various malloc,
// memalign, brk, and sbrk calls available to us.
//
// We will allocate N+2 pages for this, with the input buffer
// occupying the upper end of the middle N pages, placed so that
// its top ends exactly at the top of those N pages, where N ==
// (buffer_pg_size_in_bytes/pgsz).
//
// The RAWSCAN *rsp structure will be occupying a small portion
// of the bottom most page, and the read-only sentinel byte, a copy
// of the specified delimiterbyte, will be occupying the first byte
// at the very beginning of the top most page.
//
// Our reads into this "bufsz" buffer, and the return of lines by
// rs_getline() from that buffer, will walk their way up that buffer,
// until such time as the next line that would be returned does not
// fit in the remaining buffer.  At that point, if not using the
// pause logic, and if the partial line we have so far in the buffer
// does not already entirely fill the buffer, we do one of:
//
//  1) shift that partial line down to the beginning of the buffer
//     and try to read more of the line (which is the default),
//  2) return that partial line as the first chunk of a long line
//     if the length of that chunk is >= min1stchunklen, or
//  3) shift that partial line down to buftop - min1stchunklen and
//     try to read and return at least min1stchunklen bytes of that
//     line, which is equivalent to (1) if min1stchunklen has its
//     default bufsiz value.

func_static RAWSCAN *rs_open (
  int fd,              // read input from this already open file descriptor
  size_t bufsz,        // handle lines at least this many bytes in one chunk
  char delimiterbyte)  // newline '\n' or other char marking end of "lines"
{
    size_t pgsz;                // runtime hardware memory page size
    void *old_sbrk;             // initial data break (top of data seg)
    void *new_sbrk;             // new data break after our allocations
    void *start_our_pages;      // start of full pages we'll allocate

    RAWSCAN *rsp;          // build new RAWSCAN here

    size_t buffer_pg_size_in_bytes;  // num bytes to allocate to buffer pages

    char *buf;                  // input buffer starts here
    // size_t bufsz   ...       buffer size in bytes, above input parameter
    char *buftop;               // l.u.b. (top) of buffer

    pgsz = sysconf(_SC_PAGESIZE);

// Round up x to next pgsz boundary
#   define PageSzRnd(x)  (((((unsigned long)(x))+(pgsz)-1)/(pgsz))*(pgsz))

    buffer_pg_size_in_bytes = PageSzRnd(bufsz);

#   undef PageSzRnd

    old_sbrk = sbrk(0);
    assert (((uintptr_t)(old_sbrk) % pgsz) == 0);
    start_our_pages = old_sbrk;

    new_sbrk =
        (char *)start_our_pages +   // start of next page at or above old_sbrk
        1*pgsz +                    // one page for RAWSCAN *rsp structure
        buffer_pg_size_in_bytes +   // size in bytes of pages for input buffer
        1*pgsz;                     // one page for read-only sentinel page

    if ((brk(new_sbrk)) != 0)
        return NULL;

    rsp = (RAWSCAN *)start_our_pages;

    buftop = (char *)start_our_pages + 1*pgsz + buffer_pg_size_in_bytes;
    assert(buftop == (char *)new_sbrk - 1*pgsz);
    buftop[0] = delimiterbyte;
    buftop[1] = '\0';   // guard rail for functions of nul-terminated strings
    buf = buftop - bufsz;

    // Protect our sentinel delimiterbyte from stray writes:
    if (mprotect (buftop, pgsz, PROT_READ) < 0)
        return NULL;

    memset(rsp, 0, sizeof(*rsp));

    rsp->buf = buf;
    rsp->buftop = buftop;

    // Initializing p and q to buftop, not to buf, tricks rs_getline()
    // into calling rawscan_read() before rawmemchr() on first call.
    rsp->p = rsp->q = rsp->buftop;

    rsp->fd = fd;
    rsp->pgsz = pgsz;
    rsp->bufsz = bufsz;
    rsp->min1stchunklen = bufsz;
    rsp->delimiterbyte = delimiterbyte;
    rsp->next_delim_ptr_peek = rsp->buftop;

    // Above memset() handles following:
    // rsp->end_this_chunk = NULL;
    // rsp->next_val_p = NULL;
    // rsp->result = ...;
    // rsp->in_longline = false;
    // rsp->terminate_current_pause = false;
    // rsp->longline_ended = false;
    // rsp->eof_seen = false;
    // rsp->err_seen = false;
    // rsp->pause_on_inval = false;

    assert (((uintptr_t)(rsp->buftop) % pgsz) == 0);
    assert (rsp->buf >= (const char *)buf);
    assert (rsp->buf + rsp->bufsz == rsp->buftop);
    assert ((char *)rsp + pgsz <= rsp->buf);
    assert (sizeof(rsp) <= pgsz);

    return rsp;
}

// Suppress warnings if the pause functions, or some parameters, aren't used.
#define __unused__ __attribute__((unused))

func_static void rs_close(RAWSCAN *rsp __unused__)
{
    // We don' t close rsp->fd ... we got it open and so we leave it open.
    //
    // We could get fancy and see if the current data break, sbrk(0),
    // has not moved any further up since we moved it upward in the
    // above rs_open() call, and if it has not moved up further, move
    // it back down to the "old_sbrk" we had before the rs_open().
    //
    // But I'm not presently sufficiently motivated to do that.
    //
    // So this routine is currently a no-op.
}

__unused__ func_static void rs_enable_pause(RAWSCAN *rsp)
{
    rsp->pause_on_inval = true;
}

__unused__ func_static void rs_disable_pause(RAWSCAN *rsp)
{
    rsp->pause_on_inval = false;
    rsp->terminate_current_pause = false;
}

__unused__ func_static void rs_resume_from_pause(RAWSCAN *rsp)
{
    rsp->terminate_current_pause = true;
}

/*
 * Return next "line" from rsp input.
 *
 * The return value of rs_getline() is a RAWSCAN_RESULT that
 * distinguishes various possible returns, such as returning a
 * whole line, a chunk of a long line, or seeing EOF or an error
 * on the input stream.
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
 * the range of bytes [RAWSCAN_RESULT.begin, RAWSCAN_RESULT.end],
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
 * Regarding the "reset pause logic" noted in two comments below,
 * here's the sequence of events that result in this reset:
 *
 *  1) rawscan pauses incoming processing, to not overwrite
 *      already returned data the caller might still be using
 *  2) caller copies out or finishes using any such data
 *  3) caller invokes the rs_resume_from_pause() call
 *      to tell rawscan it's done with any such buffered data
 *  4) rs_resume_from_pause() sets "rsp->terminate_current_pause = true;"
 *      telling rawscan that it is safe to overwrite buffered
 *      data in order to reuse buffer space
 *  5) "rsp->terminate_current_pause" is left set to "true" for a while,
 *      preventing more pauses
 *  6) when rs_getline() resets or overwrites some of its buffer, then
 *      it will set "rsp->terminate_current_pause = false", once again
 *      enabling more future pauses to happen, the next time that rawscan
 *      has to overwrite more of what it's already returned.
 */

// Private helper routines used by rs_getline():

static RAWSCAN_RESULT rawscan_full_line(RAWSCAN *rsp)
{
    // The "normal" case - return another full line all at once.
    // The line to return is [rsp->p, rsp->end_this_chunk].
    // *(rsp->end_this_chunk) is either a delimiterbyte or
    //    else we're at end of file and it's the last byte.

    assert(rsp->p != NULL);
    assert(rsp->next_val_p != NULL);
    assert(rsp->end_this_chunk != NULL);

    assert(rsp->p <= rsp->end_this_chunk);
    assert(rsp->next_val_p <= rsp->q);
    assert(rsp->end_this_chunk <= rsp->q);
    assert(rsp->end_this_chunk <= rsp->buftop);
    assert(*rsp->end_this_chunk == rsp->delimiterbyte || rsp->eof_seen);

    if (*rsp->end_this_chunk == rsp->delimiterbyte)
        rsp->result.type = rt_full_line;
    else
        rsp->result.type = rt_full_line_without_eol;
    rsp->result.line.begin = rsp->p;
    rsp->result.line.end = rsp->end_this_chunk;

    rsp->p = rsp->next_val_p;

    return rsp->result;
}

static RAWSCAN_RESULT rawscan_eof(RAWSCAN *rsp)
{
    rsp->result.type = rt_eof;
    rsp->eof_seen = true;

    return rsp->result;
}

static RAWSCAN_RESULT rawscan_err(RAWSCAN *rsp)
{
    rsp->result.type = rt_err;
    rsp->result.errnum = rsp->errnum;
    assert(rsp->err_seen == true);

    return rsp->result;
}

static const char *rawscan_read (RAWSCAN *rsp)
{
    int cnt;

    cnt = read (rsp->fd, (void *)(rsp->q), rsp->buftop - rsp->q);

    if (cnt > 0) {
        const char *pre_read_q = rsp->q;
        rsp->q += cnt;
        if (rsp->q < rsp->buftop)   // reduce useless rawmemchr scanning
            *(char *)(rsp->q) = rsp->delimiterbyte;
        return pre_read_q;          // returns to start_next_rawmemchr_here
    } else if (cnt == 0) {
        rsp->eof_seen = true;
        return NULL;
    } else {
        rsp->errnum = errno;
        rsp->err_seen = true;
        return NULL;
    }
}

static RAWSCAN_RESULT rawscan_start_of_longline(RAWSCAN *rsp)
{
    assert(rsp->p != NULL);
    assert(rsp->in_longline == false);
    assert(rsp->longline_ended == false);
    assert(rsp->q == rsp->buftop);

    rsp->result.type = rt_start_longline;
    rsp->result.line.begin = rsp->p;
    rsp->result.line.end = rsp->end_this_chunk;

    rsp->p = rsp->q;
    rsp->in_longline = true;
    rsp->longline_ended = false;

    return rsp->result;
}

static RAWSCAN_RESULT rawscan_within_longline(RAWSCAN *rsp)
{
    assert(rsp->p != NULL);
    assert(rsp->next_val_p != NULL);
    assert(rsp->end_this_chunk != NULL);

    assert (rsp->in_longline == true);
    // rsp->longline_ended might be true or false

    assert(rsp->p <= rsp->end_this_chunk);       // non-empty return chunk

    rsp->result.type = rt_within_longline;
    rsp->result.line.begin = rsp->p;
    rsp->result.line.end = rsp->end_this_chunk;
    rsp->p = rsp->next_val_p;

    rsp->end_this_chunk = NULL;     // force rs_getline to set again
    rsp->next_val_p = NULL;         // force rs_getline to set again

    return rsp->result;
}

static RAWSCAN_RESULT rawscan_terminate_longline(RAWSCAN *rsp)
{
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

    rsp->result.type = rt_longline_ended;
    rsp->result.line.begin = rsp->result.line.end = NULL;

    return rsp->result;
}

static RAWSCAN_RESULT rawscan_paused(RAWSCAN *rsp)
{
    rsp->result.type = rt_paused;

    return rsp->result;
}

static void rawscan_shift_buffer_contents_down(RAWSCAN *rsp)
{
    size_t howmuchtoshift;
    size_t howfartoshift;

    const char *old_p;
    const char *new_p;
    const char *new_q;

    // Here we're shifting down a small (less than min1stchunklen)
    // segment just enough to be able to get a full min1stchunklen
    // length segment in the upper end of the buffer.  I am not
    // yet certain that this routine will never be called while
    // processing a multi-chunk longline. Let's see if the following
    // assert ever fires.

    assert(!rsp->in_longline);

    howmuchtoshift = rsp->q - rsp->p;
    assert(howmuchtoshift > 0);
    assert(howmuchtoshift < rsp->min1stchunklen || rsp->in_longline);

    howfartoshift = rsp->p - (rsp->buftop - rsp->min1stchunklen);
    assert(howfartoshift > 0);

    old_p = rsp->p;
    new_p = rsp->p - howfartoshift;
    new_q = rsp->q - howfartoshift;

    assert(rsp->q == rsp->buftop);

    memmove((void *)new_p, old_p, howmuchtoshift);

    rsp->p = new_p;
    rsp->q = new_q;
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

func_static RAWSCAN_RESULT rs_getline (RAWSCAN *rsp) __attribute__ ((hot));
static RAWSCAN_RESULT rs_getline_morecode (RAWSCAN *rsp);

func_static RAWSCAN_RESULT rs_getline (RAWSCAN *rsp)
{
    // Optimized for short lines in long buffer.

#   define likely(x)     __builtin_expect((x), 1)

    if (likely(rsp->p <= rsp->next_delim_ptr_peek &&
        rsp->next_delim_ptr_peek < rsp->q)) {

            assert(rsp->result.type == rt_full_line);
            assert(rsp->next_delim_ptr_peek < rsp->buftop);

            rsp->result.line.begin = rsp->p;
            rsp->result.line.end = rsp->next_delim_ptr_peek;
            rsp->p = rsp->next_delim_ptr_peek + 1;
            rsp->next_delim_ptr_peek = (const char *)rawmemchr(rsp->p,
                                                    rsp->delimiterbyte);
            return rsp->result;
    }
    return rs_getline_morecode(rsp);
}

static RAWSCAN_RESULT rs_getline_morecode (RAWSCAN *rsp)
{
    const char *next_delim_ptr;
    const char *start_next_rawmemchr_here;
    size_t len;                                 // how many chars in [p, q)

    if (rsp->in_longline) {
        // finish off two-step longline termination
        if (rsp->longline_ended) {
            return rawscan_handle_end_of_longline(rsp);
        } else {
            start_next_rawmemchr_here = rsp->buftop;
            goto slow_loop;
        }
    }

    // Disables "peek".  Only successful fast_loop re-enables.
    rsp->next_delim_ptr_peek = rsp->buftop;

    start_next_rawmemchr_here = rsp->p;

  fast_loop:

    // We have to call rawmemchr() almost every rs_getline() call,
    // but we try to avoid calling it more often than we have to,
    // and we try to avoid rescanning any data twice.

    next_delim_ptr = (const char *)rawmemchr(start_next_rawmemchr_here,
                                            rsp->delimiterbyte);
    assert(next_delim_ptr != NULL);

    // fastpath the two common cases, where performance counts most:
    //      1) We have a full line in buffer, ready to return.
    //      2) We have a partial line, and room in buffer to read more.

    if (rsp->p < rsp->q) {
        if (next_delim_ptr < rsp->q) {
            rsp->result.type = rt_full_line;
            rsp->result.line.begin = rsp->p;
            rsp->result.line.end = next_delim_ptr;
            rsp->p = next_delim_ptr + 1;

            // If there is another delimiter between rsp->p and rsp->q,
            // then the next line will re-enable above "peek" code.
            rsp->next_delim_ptr_peek = (const char *)rawmemchr(rsp->p,
                                                rsp->delimiterbyte);
            return rsp->result;
        } else if (rsp->q < rsp->buftop) {
            // have space above q: read more and try again
            start_next_rawmemchr_here = rawscan_read(rsp);
            if (start_next_rawmemchr_here == NULL) {
                start_next_rawmemchr_here = rsp->buftop;
                goto slow_loop;
            }
            goto fast_loop;
        } // else fall into the slow loop ...
    } // else fall into the slow loop ...

    // Falling into the slow loop, to handle all the rare or corner
    // cases.  Now pedantic exhaustive clarity matters more than speed.

  slow_loop:

    assert(rsp->buf <= start_next_rawmemchr_here);
    assert(start_next_rawmemchr_here <= rsp->buftop);

    next_delim_ptr = (const char *)rawmemchr(start_next_rawmemchr_here,
                                                rsp->delimiterbyte);
    assert(next_delim_ptr >= rsp->p);
    len = (size_t)(rsp->q - rsp->p);

    if (next_delim_ptr < rsp->q) {                  // got delimiter in [p, q)
        assert(next_delim_ptr < rsp->q);
        rsp->end_this_chunk = next_delim_ptr;
        rsp->next_val_p = next_delim_ptr + 1;
        if (rsp->in_longline) {
            return rawscan_handle_end_of_longline(rsp);
        } else {
            return rawscan_full_line(rsp);
        }
    } else if (rsp->eof_seen || rsp->err_seen) {    // end of input seen
        if (len > 0) {                              // have more chars in buf
            assert (rsp->q < rsp->buftop);          // have space above q
            // We know we have buffer space above q because we've
            // seen the end of the input, which only happens after
            // a call to rawscan_read() has tried, but failed,
            // to read more bytes into some empty space above q.
            rsp->end_this_chunk = rsp->q - 1;
            rsp->next_val_p = rsp->q;
            if (rsp->in_longline) {
                return rawscan_handle_end_of_longline(rsp);
            } else {
                return rawscan_full_line(rsp);
            }
        } else if (rsp->in_longline) {
            rsp->longline_ended = true;
            rsp->end_this_chunk = NULL;
            rsp->next_val_p = NULL;
            return rawscan_handle_end_of_longline(rsp);
        } else if (rsp->eof_seen) {
            return rawscan_eof(rsp);
        } else {
            return rawscan_err(rsp);
        }
    } else if (rsp->q < rsp->buftop) {
        start_next_rawmemchr_here = rawscan_read(rsp);
        if (start_next_rawmemchr_here == NULL) {
            start_next_rawmemchr_here = rsp->buftop;
        }
        goto slow_loop;
    } else if (len >= rsp->min1stchunklen && ! rsp->in_longline) {
        rsp->end_this_chunk = rsp->q - 1;
        rsp->next_val_p = rsp->q;
        return rawscan_start_of_longline(rsp);
    } else if (len > 0) {                           // have more chars in buf
        assert(len < rsp->min1stchunklen || rsp->in_longline);
        if (rsp->p > rsp->buf) {                    // have space below p
            if (rsp->pause_on_inval && !rsp->terminate_current_pause) {
                return rawscan_paused(rsp);
            } else {
                rawscan_shift_buffer_contents_down(rsp);
                start_next_rawmemchr_here = rsp->buftop;
                rsp->terminate_current_pause = false;    // reset pause logic
                goto slow_loop;
            }
        } else {
            // Buffer is stuffed with one chunk of a long line.

            assert(rsp->p == rsp->buf);
            assert(rsp->q == rsp->buftop);
            assert(rsp->in_longline);

            rsp->end_this_chunk = rsp->q - 1;
            rsp->next_val_p = rsp->q;

            return rawscan_within_longline(rsp);
        }
    } else {
        // Buffer is stuffed with already returned lines.  Time to
        // reset buffers and read some more, or pause awaiting a resume.

        if (rsp->pause_on_inval && !rsp->terminate_current_pause) {
            return rawscan_paused(rsp);
        } else {
            rsp->p = rsp->q = rsp->buf;                 // reset buffers
            rsp->terminate_current_pause = false;       // reset pause logic
            start_next_rawmemchr_here = rawscan_read(rsp);
            if (start_next_rawmemchr_here == NULL) {
                start_next_rawmemchr_here = rsp->buftop;
            }
            goto slow_loop;
        }
    }

    // Not reached: every code path above returns or loops to "slow_loop".
    assert("internal rawscan library logic error" ? 0 : 0);
}

/*
 * "min1stchunklen" is the guaranteed minimum length of the first
 * chunk of a long line, or minimum length of a full line, that
 * rs_getline() will return, for all lines at least that long.
 * 
 * By default, in rs_open(), min1stchunklen is set to be the same
 * value as the caller specified bufsz.  This means that rs_getline()
 * will "move heaven and earth" (well, move an almost entirely
 * full buffer of a partial line that initially extended past the
 * upper end of the buffer) in an effort to get the entire line
 * into a single buffer and return it as a full line, rather than
 * as multiple chunks.
 * 
 * However it is often the case that programs don't need to see an
 * entire line in a single piece in order to decide whether or not
 * that line is worth further consideration.
 *
 * If a program knows that it can easily distinguish which lines
 * it is most interested in by examining, say, just the first 42
 * bytes of the line, then that program can change "min1stchunklen"
 * from the default size of the entire buffer, down to just 42.
 *
 * Then the rs_getline() code will not waste CPU, cache and memory
 * cycles copying a longer (than 42) but still incomplete line
 * prefix from the upper end of the buffer to the bottom of the
 * buffer, trying to get the entire line in a single full line,
 * without chunking.  Rather rs_getline() can just return the 42+
 * byte chunk it has in the top of its buffer, for the caller to
 * quicky evaluate for further relevancy.
 * 
 * Moreover, if some smaller prefix, less than our example 42
 * bytes, is found in the upper end of the buffer, rs_getline
 * need only copy that smaller prefix down a little, to 42
 * bytes below buftop.
 *
 * All this enables an application using rawscan to allocate a
 * large input buffer, say 64 kbytes or 128 kbytes, for performance
 * without risking losing that performance when a substantial portion
 * of the leading part of a very long, say 50 kbyte, 100 kbyte or
 * even longer line ends up in the upper end of the buffer, when
 * by default, rs_getline() would copy that substantial portion to
 * the low end of the buffer, only to have the caller decide after
 * looking at, say, the first 42 bytes of the line that it had no
 * interest in the rest of that line.
 *
 * In other words, the default to always return full lines, not
 * chunked into multiple pieces, for all lines that can be made to
 * fit in the buffer, can be inefficient, especially if both of:
 * 
 *  1) Using a large buffer primarily for performance (such as
 *     minimizing read() system calls) rather than for the guarantee
 *     that any line shorter than the full buffer size will always
 *     be returned as a single line, rather than multiple chunks,
 *     even if that means that rs_getline() internals had to copy
 *     part of a long line from the upper end of the buffer down
 *     to the lower end.
 *  2) Long lines that fill a significant fraction of this large
 *     buffer are seen frequently (or at least they are not rare.)
 * 
 * If the caller prefers a large input buffer for performance,
 * but would rather not pay the price of internal rs_getline()
 * copying partial lines that hit the top of the buffer, then the
 * caller can choose to minimize such internal copies, and instead
 * receive such lines potentially in multiple chunks, for any line
 * of length (including trailing delimiter) at or above the length
 * specified in the most recent rs_set_min1stchunklen() call.
 *
 * In short, rs_set_min1stchunklen(rsp, min1stchunklen) tells the
 * RAWSCAN_RESULT input stream "rsp" to return at least "min1stchunklen"
 * bytes from longer input lines in single chunks (whether as full
 * and complete lines, or as the initial chunk of a longer line),
 * but perhaps more importantly, this call also tells that RAWSCAN_RESULT
 * stream _not_ to waste cycles moving stuff around in its buffer
 * trying to get more of a line for its first return for that line.
 *
 * The min1stchunklen passed into a rs_set_min1stchunklen() call
 * must be less than or equal to RAWSCAN_RESULT stream's buffer
 * size, as specified in the rs_open() call for that buffer.
 * rs_set_min1stchunklen() will fail, returning -1 and doing
 * nothing else, if the requested min1stchunklen value is greater
 * than the size of that RAWSCAN stream's buffer.  Otherwise,
 * rs_set_min1stchunklen() will successfully change that stream's
 * min1stchunklen value and return 0.
 *
 * Potential Future Extension: I can imagine adding another rs_*()
 * routine that will, on a one time basis, have the rawscan code
 * shift a partial line (or even multiple consecutive lines) lower
 * in the buffer, in order to fit more of that line (or of a several
 * line sequence) into the buffer, for more convenient parsing
 * from a single consecutive byte sequence.
 */

func_static int rs_set_min1stchunklen(RAWSCAN *rsp, size_t min1stchunklen)
{
    if (min1stchunklen > rsp->bufsz)
        return -1;

    rsp->min1stchunklen = min1stchunklen;
    return 0;
}

func_static size_t rs_get_min1stchunklen(RAWSCAN *rsp)
{
    return rsp->min1stchunklen;
}
