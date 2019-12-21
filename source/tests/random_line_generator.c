/*
 *  _generator [options] - generate random lines
 *
 *  -n num: 'num' is number of lines to generate (default 1000000)
 *  -m min: 'min' is minimum length of each line (default 0)
 *  -M max: 'max' is maximum length of each line (default 100)
 *  -e eol: 'eol' is end of line byte (defaults to '\n' line feed)
 *  -B:     use Base64 [A-Za-z0-9+/] bytes (default)
 *  -C:     use some Consecutive range of bytes
 *  -L lo:  if (-C) 'lo' byte of range to use
 *  -H hi:  if (-C) 'hi' byte of range to use
 *  -R:     Randomly and independently select each output byte
 *  -S:     Sequentially rotate through output byte range (default)
 *  -T:     Do not issue the final Terminating end of line byte
 *
 * If invoked with no options, it is equivalent to using the defaults:
 *
 *    random_line_generator -n 1000000 -m 0 -M 100 -e '\n' -B -R
 *
 * This is the default.  It will generate a million lines, each of
 * length 0 to 100 bytes (plus a linefeed), rotating sequentially
 * through the base64 [A-Za-z0-9+/] set of bytes.
 *
 * If invoked with the following options, it replicates the output
 * if the original, option-less, simpler random_line_generator
 * that was in use briefly in early November 2019:
 *
 *    random_line_generator -n 1000000 -m 0 -M 16 -B -S
 *
 * The -B and -C options are mutually exclusive, and the -L and -H
 * options must be specified if the -C option is used.  In the -C
 * case, all bytes in the interval [lo, hi] specified by -L and -H
 * will be available for use in generating lines.
 *
 * The 'lo' 'hi' and 'eol' bytes can be entered on the command line
 * using character constants (including backslash escape sequences
 * with sufficient shell quoting) or as hex (0x), decimal, or (0)
 * octal constants.  These bytes must each be in the range [0, 255].
 *
 * More exactly, if the first byte of the value provided to the -e,
 * -L, or -H options is a backslash '\', then the value is taken
 * to be one of the usual C standard backslash escape sequences,
 * else if the length of the value is two or more bytes long,
 * then the value is obtained from the string to unsigned long
 * (stroul(3)) conversion of that string, which will fail if that
 * doesn't convert to a value between 0 and 255, else if the value
 * is a single ASCII decimal integer '0' to '9' then that's an error
 * (to avoid error prone ambiguity), else the value is taken to be
 * the literal single byte character to be used for that option.
 *
 * It is not allowed for the the 'eol' byte (whether the default
 * '\n' or as specified with the -e option) to also be in the
 * range of bytes to use in forming lines.  This prohibition is
 * simply to avoid code complexities for a case that seems to be
 * of little value.
 *
 * If the -T option is given, then the very last and final end of
 * line byte will not be issued.  This is useful for testing the
 * case of files without a trailing newline.
 *
 * If the -R option is given, or taken as the default, then the
 * the bytes output will rotate through the specified -B or -L/-H
 * range, even across lines.  If the -S option is given, then each
 * byte in a line (but for the end of line byte, of course) will
 * be randomly and independently selected.  The -S option requires
 * more CPU cycles than the -R option.
 *
 * === Licensing:
 *
 * Except for the PCG source code below, the following source
 * code is Copyright 2019 by Paul Jackson <pj@usa.net> and
 * licensed under your choice of:
 *
 *  Apache License, Version 2.0 (see "../licenses/LICENSE-APACHE"),
 *  MIT (see "../licenses/LICENSE-MIT.txt"), and/or
 *  GNU GENERAL PUBLIC LICENSE Version 2 (see "../licenses/COPYING.GPLv2").
 *
 * The PCG source code below is Copyright 2014 Melissa O'Neill
 * <oneill@pcg-random.org>, and is licensed by her under the
 *  Apache License, Version 2.0 (see "../licenses/LICENSE-APACHE").
 *
 * === Motivation:
 *
 * The primary motivation for this command is to generate input test
 * for a high performance "getline()" variant that I am working on,
 * to be called "rawscan", to be made available in both C (which
 * I know well) and Rust (which I am learning.)
 *
 * === Background on this use of PCG:
 *
 * This command uses PCG to provide the repeatable pseudo random
 * number generation that is suitable for generating repeatable
 * test cases.
 *
 * This is my first use of the PCG pseudo random number generator.
 * To put PCG into context, this article (where I first heard of
 * of PCG) is an easy read on the current (2019) state of random
 * affairs:
 *
 *   https://hg.sr.ht/~icefox/oorandom
 *
 * The above article led to the following, with more details:
 *
 *   https://github.com/igiagkiozis/PCGSharp
 *
 * That in turn led to this paper on PCG:
 *
 * PCG: A Family of Simple Fast Space-Efficient Statistically
 * Good Algorithms for Random Number Generation
 *      -- MELISSA E. O'NEILL , Harvey Mudd College
 *
 * http://www.pcg-random.org/pdf/toms-oneill-pcg-family-v1.02.pdf
 *
 * The above website of M. E. O'Neill's, http://www.pcg-random.org,
 * has quite a bit more on PCG.
 *
 * === Build instructions:
 *
 * As of Oct 2019, compiles cleanly using gcc on Linux with:
 *
 *    cc -fwhole-program -march=native -O3 -Wall -pedantic -Werror \
 *          -o random_line_generator random_line_generator.c
 *
 * Paul Jackson
 * pj@usa.net
 * Begun: 24 Oct 2019
 */

 // cmake debug builds enable asserts (NDEBUG not defined),
 // whereas cmake release builds define NDEBUG to disable asserts.
 #include <assert.h>

// The following pcg32 random number generator (rng) routines and
// types are from Melissa E. O’Neill's http://www.pcg-random.org/.

#include <inttypes.h>

struct pcg_state_setseq_64 {    // Internals are *Private*.
    uint64_t state;             // RNG state.  All values are possible.
    uint64_t inc;               // Controls which RNG sequence (stream) is
                                // selected. Must *always* be odd.
};

typedef struct pcg_state_setseq_64 pcg32_random_t;
uint32_t pcg32_random_r(pcg32_random_t* rng);

// pcg32_srandom_r(rng, initstate, initseq):
//     Seed the rng.  Specified in two parts, state initializer and a
//     sequence selection constant (a.k.a. stream id)

 void pcg32_srandom_r(pcg32_random_t* rng, uint64_t initstate, uint64_t initseq)
 {
     rng->state = 0U;
     rng->inc = (initseq << 1u) | 1u;
     pcg32_random_r(rng);
     rng->state += initstate;
     pcg32_random_r(rng);
 }

 // pcg32_random_r(rng)
 //     Generate a uniformly distributed 32-bit random number

 uint32_t pcg32_random_r(pcg32_random_t* rng)
 {
     uint64_t oldstate = rng->state;
     rng->state = oldstate * 6364136223846793005ULL + rng->inc;
     uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
     uint32_t rot = oldstate >> 59u;
     return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
 }

// End of the small subset of Melissa E. O’Neill's pcg32 code
// that we need here.

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

// We're dealing in 8 bit bytes, not Unicode or some other
// potentially multibyte characters.

typedef unsigned char byte;
#define BYTE_MAX UCHAR_MAX      // 255
#define BYTE_WIDTH CHAR_BIT     //   8

#ifndef _bool_defined_
#define _bool_defined_
    typedef enum {
        false,
        true
    } bool;
#endif

#define profiling_this_code 0
#if profiling_this_code
#define func_static
#else
#define func_static static
#endif

// A random helper - to issue random values in range [0, top),
// taken from caching one 4 byte pcg32_random_r() result.

func_static size_t get_random_less_than(pcg32_random_t *rng, size_t top)
{
    static uint32_t ranc;       // our little cache
    static uint32_t remaining;  // remaining range of values in cache
    size_t r;                   // return value

    // If top == 0, would fail with divide by zero in main code below.
    // If top == 1, already know answer ... 0.
    // So handle both cases in one test:
    if (top <= 1)
        return 0;

    // For even distribution of random values, need to discard cache
    // when its remaining range of values [0, remaining) is less
    // than the range [0, top) in which we're looking for a random.

    if (remaining < top) {
        // refill our little cache
        ranc = pcg32_random_r(rng);
        remaining = UINT32_MAX;
    }

    r = ranc % top;
    ranc /= top;
    remaining /= top;

    return r;
}

/*
 * If (as in the default) we're generating lines using base64,
 * then we use the b64map array and associated routine, else we
 * use the bytemap array and associated routine.
 */

#define B64MAPLEN 64
func_static byte b64map[B64MAPLEN];  // Base 64 Alphabet (from RFC 3548)

func_static void build_b64map(byte *map, int len)
{
    int m, bufi;

    assert(len == B64MAPLEN);  // We have one job - put 64 bytes in map
    assert(map == b64map);

    m = 0;

    for (bufi = 0; bufi < 26; bufi++) {
        map[m++] = 'A' + bufi;
    }
    for (bufi = 0; bufi < 26; bufi++) {
        map[m++] = 'a' + bufi;
    }
    for (bufi = 0; bufi < 10; bufi++) {
        map[m++] = '0' + bufi;
    }

    map[m++] = '+';
    map[m++] = '/';

    assert(m == len);    /* just being sure I still know how to count */
}

func_static int bytemaplen;              // How many bytes in [lo, hi] range
#define MAXNUMBYTES 256                  // Upper limit on bytemaplen
func_static byte bytemap[MAXNUMBYTES];   // Maps [0, bytemaplen) to [lo, hi]

// build_bytemap(), below, initializes bytemap[], above.

func_static void build_bytemap(byte *map, int len, byte lo, byte hi)
{
    int bufi;
    byte c;

    bytemaplen = hi - lo + 1;
    assert(bytemaplen < len);

    bufi = 0;
    c = lo;

    while (bufi < bytemaplen)
        map[bufi++] = c++;
}

/*
 * "map" is either b64map or bytemap, as chosen by options -B, -L/-H
 */

func_static int maplen;  // number bytes in map we can use to fill lines
func_static byte *map;   // Map [0, maplen) to one of the bytes used to fill lines

/*
 * Our end of line byte (default '\n')
 */

 func_static byte eol_byte;

/*
 * outbuf: dynamically allocated buffer to hold each output line while
 *         we build it. This outbuf will be "one plus -M bytes" long
 *         (default 1 + 100), to hold one terminating eol byte as well
 *         as up to -M bytes.
 */

func_static byte *outbuf;       /* build lines in this buffer */
func_static size_t outbuflen;   /* length of outbuf[] */

/*
 * mk_random_line() -- build random line in outbuf
 */

func_static size_t mk_random_line(uint64_t minlen, pcg32_random_t* rng,
                    bool emit_random_bytes)
{
    static size_t static_mi;     // static index into map[]
    size_t mi;                   // dynamic index into map[]
    size_t bufi;                 // index inserting into outbuf[]
    size_t bufi_top;             // l.u.b. on bufi

    bufi = 0;

    bufi_top = minlen + get_random_less_than(rng, outbuflen - minlen);

    while (bufi < bufi_top) {
        if (emit_random_bytes)
            mi = get_random_less_than(rng, maplen);
        else
            mi = (static_mi++ % maplen);

        outbuf[bufi++] = map[mi];
    }

    outbuf[bufi++] = eol_byte;

    return bufi;
}

func_static const char *cmd = "random_line_generator";
func_static const char *usage = "[ -n numlines -m minlen -M maxlen -e eolbyte] "
    "-B -L lobyte -H hibyte -R -S -T ]\n\n"
    " -n num: 'num' is number of lines to generate (default 1000000)\n"
    " -m min: 'min' is minimum length of each line (default 0)\n"
    " -M max: 'max' is maximum length of each line (default 100)\n"
    " -e eol: 'eol' is end of line byte (defaults to '\\n' linefeed)\n"
    " -B:     use Base64 [A-Za-z0-9+/] bytes (default)\n"
    " -C:     use Consecutive sequence of bytes instead of -B\n"
    " -L lo:  'lo' byte of range to use in -C sequence\n"
    " -H hi:  'hi' byte of range to use in -C sequence\n"
    " -R:     Randomly and independently select each output byte\n"
    " -S:     Sequentially rotate through output byte range  (default)\n"
    " -T:     Do not issue the final Terminating end of line byte\n";

func_static void show_usage_and_exit() __attribute__((__noreturn__));

func_static void show_usage_and_exit()
{
        fprintf(stderr, "random_line_generator - generate many random short lines\n");
        fprintf(stderr, "Usage: %s %s\n", cmd, usage);
        exit(EXIT_FAILURE);
}

func_static void fatal_usage(char *msg, char *opt)
{
        fprintf(stderr, "\n\t%s: Invalid option value '%s': %s\n\n", cmd, opt, msg);
        show_usage_and_exit();
}

func_static inline int validnumlines(uint64_t numlines)
{
    return numlines < ULONG_MAX && errno == 0;
}

func_static inline int validlen(uint64_t len)
{
    return len < UINT_MAX && errno == 0;
}

func_static bool in_map(int eol)
{
    int i;

    for (i = 0; i < maplen; i++) {
        if (map[i] == eol)
            return true;
    }
    return false;
}

func_static int parsebyte(char *optarg)
{
    // See "More exactly" explanation, early in this file above.

    if (optarg[0] == '\\' && optarg[1] == '\0')
        return -1;                       // Empty backslash escape

    if (optarg[0] == '\\' && optarg[2] == '\0') {
        switch (optarg[1]) {
            case 'a':  return  0x07;     // Alert (Beep, Bell)
            case 'b':  return  0x08;     // Backspace
            case 'e':  return  0x1B;     // Escape character
            case 'f':  return  0x0C;     // Formfeed Page Break
            case 'n':  return  0x0A;     // Newline (Line Feed)
            case 'r':  return  0x0D;     // Carriage Return
            case 't':  return  0x09;     // Horizontal Tab
            case 'v':  return  0x0B;     // Vertical Tab
            case '\\': return  0x5C;     // Backslash
            case '\'': return  0x27;     // Single quotation mark
            case '"':  return  0x22;     // Double quotation mark
            case '?':  return  0x3F;     // Question mark
            default:   return    -1;     // Unrecognized backslash escape
        }
    }

    if (optarg[0] == '\\' && optarg[2] != '\0')
        return -1;                       // Unrecognized backslash escape

    if (strlen(optarg) >= 2) {
        uint64_t c;
        char *optend;

        c = strtoul(optarg, &optend, 0);

        if (optarg == optend && c == 0) // optarg didn't start with a number
            return -1;

        if (c > BYTE_MAX)
            return -1;

        return c;
    }

    if (strlen(optarg) == 0)
        return -1;

    assert(strlen(optarg) == 1);

    if (optarg[0] >= '0' && optarg[0] <= '9')
        return -1;                       // avoid error prone ambiguity

    return optarg[0];
}

int main(int argc, char **argv)
{
    uint64_t n;
    pcg32_random_t rng;

    uint64_t numlines = 1000000;    // By default, output a million lines.
    uint64_t minlen = 0;            // of length 0 bytes to ...
    uint64_t maxlen = 100;          // ... 100 bytes (plus the eol byte).
    int eol = '\n';                 // The default eol byte.
    bool useBase64 = true;          // By default, lines have Base64 bytes
    bool useConsecutive = false;    // ... not some Consecutive bytes,
    bool use_B_or_C_set = false;    // Flag to keep B, C mutually exclusive
    int lo = -1;                    // but if useConsecutive ...
    int hi = -1;                    // ... then use bytes in range [lo, hi].
    bool emit_random_bytes = false; // If true, randomly pick each byte.
    bool suppress_last_eol = false; // If true, suppress last eol

    extern int optind;
    extern char *optarg;
    int c;

    while ((c = getopt(argc, argv, "n:m:M:e:BCL:H:RST")) != EOF) {
        errno = 0;  // valid*() routines below depend on this.
        char *optend;

        switch (c) {
        case 'n':
            if (!validnumlines(numlines = strtoul(optarg, &optend, 0)) ||
                    (optarg == optend && numlines == 0))
                fatal_usage("invalid number of numlines", optarg);
            break;
        case 'm':
            if (!validlen(minlen = strtoul(optarg, &optend, 0)) ||
                    (optarg == optend && minlen == 0))
                fatal_usage("invalid minimum line length", optarg);
            break;
        case 'M':
            if (!validlen(maxlen = strtoul(optarg, &optend, 0)) ||
                    (optarg == optend && maxlen == 0))
                fatal_usage("invalid maximum line length", optarg);
            break;
        case 'e':
            if ((eol = parsebyte(optarg)) == -1)
                fatal_usage("invalid end of line byte", optarg);
            break;
        case 'B':
            if (use_B_or_C_set && useConsecutive) {
                fatal_usage("options -B and -C are mutually exclusive", optarg);
            }
            useBase64 = true;
            useConsecutive = false;
            use_B_or_C_set = true;
            break;
        case 'C':
            if (use_B_or_C_set && useBase64) {
                fatal_usage("options -B and -C are mutually exclusive", optarg);
            }
            useBase64 = false;
            useConsecutive = true;
            use_B_or_C_set = true;
            break;
        case 'L':
            if ((lo = parsebyte(optarg)) == -1)
                fatal_usage("invalid lo byte", optarg);
            break;
        case 'H':
            if ((hi = parsebyte(optarg)) == -1)
                fatal_usage("invalid hi byte", optarg);
            break;
        case 'R':
            emit_random_bytes = true;
            break;
        case 'S':
            emit_random_bytes = false;
            break;
        case 'T':
            suppress_last_eol = true;
            break;
        default:
            show_usage_and_exit();
        }
    }

    if (optind != argc) {
        show_usage_and_exit();
    }

    if (useConsecutive && (lo == -1 || hi == -1)) {
        fprintf(stderr, "\n\tSetting -C option also requires setting -L and -H\n\n");
        show_usage_and_exit();
    }
    if (!useConsecutive && (lo != -1 || hi != -1)) {
        fprintf(stderr, "\n\tSetting -L and -H options also requires setting -C\n\n");
        show_usage_and_exit();
    }
    if (useConsecutive && ! (lo <= hi)) {
        fprintf(stderr,"\n\tInvalid -L, -H range <%d, %d>\n\n", lo, hi);
        show_usage_and_exit();
    }

    if (useBase64) {
        assert(!useConsecutive);
        build_b64map(b64map, sizeof(b64map));
        map = b64map;
        maplen = sizeof(b64map);
    } else {
        assert(useConsecutive);
        build_bytemap(bytemap, sizeof(bytemap), lo, hi);
        map = bytemap;
        maplen = bytemaplen;
    }

    if (in_map(eol)) {
        fprintf(stderr, "\n\tNot allowed to have eol (0x%2x) "
                        "in -B or -C byte set.\n\n", eol);
        show_usage_and_exit();
    }

    eol_byte = eol;

    /*
     * M. E. O'Neill reports that these two magic numbers provide good
     * statistical results from PCG.  Since we're (this command is)
     * all about repeatable test case generation, we just hard code
     * these two, and let the dice roll where they will ... the same
     * way every time :).
     */

    pcg32_srandom_r(&rng, 0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL);

    /*
     * Allocate output line buffer
     */

     outbuflen = maxlen + 1;        // "+ 1" for eol
     if ((outbuf = malloc(outbuflen)) == NULL) {
         perror("malloc");
         exit(2);
     }

    /*
     * Print lots of lines of varying length and content.
     *
     * mk_random_line() puts "len" bytes into buffer "outbuf",
     * and then fwrite writes that out, via buffered stdio.
     */

    for (n = 1; n <= numlines; n++) {
        size_t len;

        len = mk_random_line(minlen, &rng, emit_random_bytes);

        if (suppress_last_eol && n == numlines)
            len--;

        if (fwrite(outbuf, 1, len, stdout) < len) {
            perror("fwrite");
            exit(3);
        }
    }

    free(outbuf);
    exit(0);
}
