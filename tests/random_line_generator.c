/*
 * random_line_generator [-n number_lines] - generate random short lines
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
 * === What random_line_generator does:
 *
 * Generate random short lines of ASCII text. Lines consist of
 * zero to fifteen ASCII characters from the RFC 4648 base64
 * set [A-Za-z0-9+/], terminated by a newline ('\n').
 *
 * === Motivation:
 *
 * The primary motivation for this command is to generate input
 * test for a high performance "getline()" variant that I am
 * working on, which will be called "rawscan_getline()", to be
 * made available in both C (which I know well) and Rust
 * (which I am learning.)
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
 *      -- MELISSA E. O’NEILL , Harvey Mudd College
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
 *    cc -fwhole-program -march=native -O3 -Wall -pedantic -Werror -o random_line_generator random_line_generator.c
 *
 * Paul Jackson
 * pj@usa.net
 * 24 Oct 2019
 */

// The following pcg32 routines are from Melissa E. O’Neill's
//  http://www.pcg-random.org/

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

#define MAPLEN 64               /* compile time power of 2 (16, 32 or 64) */
static char map[MAPLEN];		/* Base 64 Alphabet (from RFC 3548) */

static void build_map()
{
    unsigned char m, i;

    m = 0;

    for (i = 0; i < 26; i++) {
	    if (m < MAPLEN)
            map[m++] = 'A' + i;
    }
    for (i = 0; i < 26; i++) {
        if (m < MAPLEN)
            map[m++] = 'a' + i;
    }
    for (i = 0; i < 10; i++) {
        if (m < MAPLEN)
            map[m++] = '0' + i;
    }
    if (m < MAPLEN)
        map[m++] = '+';
    if (m < MAPLEN)
        map[m++] = '/';
}

#define BUFLEN 17         /* compile time 1 + power of 2 */
static char buf[BUFLEN];  /* 0 to 14 char string of map[] chars, plus "\n\0" */

/*
 * The two mod (%) operations below, mod (BUFLEN - 1) and mod
 * MAPLEN, are both mod by compile time constants that are powers
 * of two. Therefore they both generate quick "and" machine
 * instructions, not slower 64 bit long integer divisions.
 *
 * The following random_string() function is the key performance
 * bottleneck of this command, so perhaps more complex coding
 * could speed it up some more ... unraveling loops or something.
 *
 * But for now, we're way fast enough, so except for the afore
 * mentioned mod by a compile time power of two constants, I'm
 * making no further concessions to performance. For now anyway
 * it's Keep It Simple, Stupid.
 *
 * The pseudo random number generator would perhaps have been
 * another performance bottleneck, but I am quite pleased with the
 * speed, uniformity, and code simplicity of the (for me, newly
 * discovered) PCG routines, so there's nothing further to improve
 * there at this time.
 *
 * Note the "static" qualifier on "mi", which causes subsequent
 * calls to random_string() to pick up where the previous left
 * off, selecting base64 characters from the above map[] in an
 * unending rotating manner.
 */

char *random_string(uint32_t ran)
{
    uint32_t len;               /* build a line this long in buf[] */
    static uint32_t mi;         /* "map index" - index into map[] */
    uint32_t i;                 /* loop index 0 to len in buf[] */

    len = ran % (BUFLEN - 1);   /* len in [0, <=15] */

    for (i = 0; i < len; i++, mi++) {  /* for i in [nil or 0, < 15] */
        buf[i] = map[mi % MAPLEN];
    }

    buf[i++] = '\n';                   /* i <= 15 */
    buf[i] = '\0';                     /* i <= 16 */

    return buf;
}

const char *cmd = "random_line_generator";
const char *usage = "[-n number_lines_to_gen]";

void show_usage_and_exit() __attribute__((__noreturn__));

void show_usage_and_exit()
{
        fprintf(stderr, "random_line_generator - generate many random short lines\n");
        fprintf(stderr, "Usage: %s %s\n", cmd, usage);
        exit(EXIT_FAILURE);
}

void fatal_usage(char *msg, char *opt)
{
        fprintf(stderr, "%s: Invalid option value '%s': %s\n", cmd, opt, msg);
        show_usage_and_exit();
}

int main(int argc, char **argv)
{
    uint32_t i;
    pcg32_random_t rng;
    uint32_t numlines;

    extern int optind;
    extern char *optarg;
    int c;

    numlines = 1000000;     // default - generate one million lines

    while ((c = getopt(argc, argv, "n:")) != EOF) {
        switch (c) {
        case 'n':
            if ((numlines = strtol(optarg, NULL, 0)) <= 0)
                fatal_usage("non-positive number lines argument", optarg);
            break;
        default:
            show_usage_and_exit();
        }
    }

    if (optind != argc) {
        show_usage_and_exit();
    }

    build_map();

    /*
     * M. E. O'Neill reports that these two magic numbers provide good
     * statistical results from PCG.  Since we're (this command is)
     * all about repeatable test case generation, we just hard code
     * these two, and let the dice roll where they will ... the same
     * way every time :).
     */

    pcg32_srandom_r(&rng, 0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL);

    /*
     * Print a million (or other 'numlines') lines of length
     * between zero and sixteen (BUFLEN-1) bytes [>=1, <=16],
     * including trailing newline '\n'.
     */
    for (i = 1; i <= numlines; i++) {
	       printf("%s", random_string(pcg32_random_r(&rng)));
    }
}
