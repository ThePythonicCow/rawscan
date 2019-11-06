# Rawscan

*`Rawscan`* is written for programmers of utilities that read
data one line at a time.  A line is any sequence of bytes terminated
by a designated delimiter byte.

Use the *`rawscan`* library routines `rs_open`(), `rs_getline`(), and
`rs_close`() to read input, line by line, from any any input stream
(any readable open file descriptor).  See doc/HowToUse.md for the
details of how to use *`rawscan`*.

*`Rawscan`*'s advantages include:

    - fast, safe and robust
    - rarely moves or copies data internally
    - fixed length caller specified buffer doesn't grow
    - can be used in memory constrained situations
    - can be initialized with bigger buffer for performance
    - handles arbitrarily long input lines
    - all lines that fit in buffer returned in one chunk
    - no confusing sscanf format risking bugs and overruns
    - handles Windows style "\r\n" line endings
    - correctly and easily handles embedded nuls
    - beats `gets`, `fgets`, `scanf`, `getline` and `getdelim`
    - .. see "Comparative Analysis" below for details
    - fast ... did I say fast?

# How `rawscan` works internally

Except as noted in **Special Memory Handling**, below, *`rawscan`*
uses a single fixed length buffer that is allocated during the
`rs_open`() call.  All lines short enough to fit in that buffer
are returned in one piece, using pointers directly into that buffer,
with zero copies.  Lines too long to fit are returned in multiple
chunks.

Except as noted in the **pause/resume** discussion below, whenever
`rs_getline`() finds that it only has a partial line left in
the upper end of its buffer, it shifts that partial line lower down
in its buffer and continues, always trying to return full lines in
one piece.  For this reason in particular, __*note that*__ each
`rs_getline`() call might invalidate the pointers returned
in prior calls (unless the **pause/resume** feature is used.)

### (nearly) zero-copy

Thus `rs_getline`() is not "zero-copy", but "infrequent copy",
so long as it's configured in the `rs_open`() call to have an
internal buffer that is usually longer than the typical lines it's
returning.

This strategy works especially well when there is a given upper
length to the line length that needs to be parsed conveniently (in
one piece), and longer lines can be ignored or handled in chunks.

For applications that can ignore or quickly skip over overly
long lines, the *`rawscan`* interface makes that especially easy
to do so.  Just ignore RAWSCAN_RESULT's with result types of
`rt_start_longline`, `rt_within_longline` or `rt_longline_ended`,
as described in doc/HowToUse.md.

### not safely multi-threaded (sorry)

The use of *`rawscan`* on any particular input stream should be
single threaded.  Multiple parallel threads trying to access and
update the same *`rawscan`* stream will likely confuse the `RAWSCAN`
control structure for that stream, and might prematurely overwrite
some data still being used by another thread.

### rawmemchr

One key technique that is used to improve performance (several times
fewer user CPU cycles than stdio buffer based solutions) is the
use of `strchr`, `strchrnul`, or `rawmemchr` to scan considerable
lengths of input. These routines are very heavily optimized, both
by modern compilers, and further by the Intel(tm) MultiMedia
eXtension (MMX) and Intel(tm) Advanced Vector Extensions (AVX) instructions.

Whenever a problem can be reduced to scanning large spans of buffer
looking for a single particular character, these routines can race
through the data at maximum speed, operating on data perhaps 128,
256 or 512 bits at a time, depending on compiler and CPU technology.

This is all much faster than doing character at a time:
     `while ((c = getchar()) != EOF) switch (c) { ... }`
loops in hand-coded C from a stdio input buffer, with multiple tests
for state and the value of each character 'c' in each loop iteration.

Of the three byte scanners `strchr`, `strchrnul`, and `rawmemchr`,
*`rawscan`* uses the fastest one, which is `rawmemchr`. It is fastest
because it _only_ stops scanning when it sees the requested
character.  The x86_64 assembly code in good libraries for these
routines is very fast, at least on "modern" (recent years as of
this writing in 2019) Intel and AMD x86_64 processors with AVX
vector instructions.

However `rawmemchr` will happily run to, and beyond, the limits of the
memory segment it's searching, which could cause a SIGSEGV or other
such crash, if the character it's looking for is not found sooner.
So to use `rawmemchr` safely, the *`rawscan`* code sets up a read-only
page, just past the main buffer, with a copy of the configured
delimiterbyte in the first byte of that read-only page, ensuring
that calls to `rawmemchr` will terminate there, if not sooner,
regardless of what's in the buffer at the time.

# Advanced features

The following pause/resume, memory, multiline, and delimiterbyte
facilities extend the default behavior described above.  If you
use one of these features, then some of the specifics given above
might not apply, as will be described in the following.

### pause/resume

The *`rawscan`* calling routine can gain some control over when the
contents of the buffer are invalidated by using the optional
**pause/resume** states.

To use **pause/resume**, first invoke `rs_enable_pause`()
on the RAWSCAN stream.  That call just sets a flag on the
stream's internal state and returns immediately.

Then whenever a `rs_getline`() call hits the upper end of
the buffer and needs to invalidate the already returned portion to
make room for more data, that `rs_getline`() call will instead
return with a `RAWSCAN_RESULT.type` of `rt_paused`, without altering
the current buffer contents.

When the calling routine has finished using or copying out
whatever data it's still needs from the buffer, it can then call
the `rs_resume_from_pause`() function on that stream, which
will unpause the stream and enable subsequent `rs_getline`()
calls to once again return more lines, and overwrite some of the
mpreviously returned data that had been in the buffer.

### Accessing state of a paused stream

(to be written - routines to observe state of a paused stream)

### Special Memory Handling

(to be written - routines to preallocate or preassign the buffer, without the need for any runtime malloc or other heap allocator.)

### Limited support for multiline "records"

(to be written - routines enabling handling multiline records,
so long as the entire record still fits in the buffer.)

### Changing delimiterbyte on the fly

(to be written - switching delimiterbyte on the fly)

### Handling constrained memory configurations

(to be written - disabling full readonly page for sentinel)

### Caller controlled resizing of buffer

(to be written - shrinking or expanding the buffer while in use)

# Helper Routines

(to be written - some minor helper routines that support various
carriage return/newline conventions such as a chomp that works
well with the returns from rt_getline, and access routines for
potentially interesting fields or values of the (theoretically)
opaque RAWSCAN structure.)

# Comparative Analysis:

As part of developing other personal Unix/Linux command line tools
over the years that process data line by line, I've not been happy
with the existing alternatives for scanning input lines:

 - stdio's [gets](https://www.studymite.com/blog/strings-in-c#read_using_gets)() is dangerously insecure, allowing buffer overflow.
 - stdio's [fgets](https://www.studymite.com/blog/strings-in-c#read_using_fgets)(), [scanf](http://c-faq.com/stdio/scanfprobs.html)() and [getchar](https://www.studymite.com/blog/strings-in-c#read_using_getchar)() use slow double buffering.
 - stdio's [fgets](https://www.studymite.com/blog/strings-in-c#read_using_fgets)() cannot correctly handle lines with embedded nuls.
 - [scanf](http://c-faq.com/stdio/scanfprobs.html)() risks failed format matching and buffer overflows
 - [getchar](https://www.studymite.com/blog/strings-in-c#read_using_getchar)() loops are accurate, but with slower per byte logic
 - [getline](https://www.studymite.com/blog/strings-in-c#read_using_getline)() grows (realloc's) its buffer to handle longer lines
 - [getdelim](https://www.studymite.com/blog/strings-in-c#read_using_getdelim)() is just like getline(), with a configurable delimiter

The buffer growing reallocations in `getline`() present the risk
of a denial of service attack.  Applications using `getline`()
on untrusted input can be forced to keep growing `getline`()'s
buffer until the application dies of memory exhaustion, or until
the supporting system slows unacceptably swapping a huge program.

Rust's [LineReader](https://crates.io/crates/linereader) is the
closest equivalent to *`rawscan`* that I've found so far.  However it
will split lines that wrap around the end of the buffer, even if
the line could otherwise have fit in the buffer, had it been at a
different offset.

Instead, *`rawscan`* never splits lines that it can fit in its buffer,
and never reallocates a bigger buffer than initially allocated in
the `rs_open`() call.  If an input line that would have fit in
the buffer would spill off the end of the buffer, *`rawscan`* will
shift that partially read line lower in the buffer and continue
reading the rest of it into the buffer, before returning the whole
line in one piece.

# Comparative Performance

The following test case highlights *`rawscan`*'s strengths.

The input file held 55,626,370 (55.6 million) lines of text,
and had a size of 11,266,624,053 (11.3 Gb) bytes.  The shortest
line was 82 bytes long and the longest line was 2525 bytes long.
Each line started with a long ASCII hex number over the alphabet
[0-9a-f].  These initial numbers were largely, but not entirely,
random in order and value.

A pattern was chosen that would be easy to code for in various
ways, and that would only match a few lines, so as to focus the
performance timings on the cost of reading and distinguishing each
line, not on searching within each line for a pattern, and not on
writing out the successful matches.

The pattern matched lines beginning with the eight hex characters
"6fffbf42", using strncmp() to look for the pattern, or using
the pattern "^6fffbf42" (anchored to start of line) with grep.
This pattern matched 8 out of the 55,626,370 lines.

Here's the user CPU times, on a Ryzen 1700 with plenty of RAM and
and with the input file (all 11.2+ GBytes of it) already loaded
into RAM, for each of grep and three small programs coded to use
fgets, getline, and rawscan. Times are averages of 10 runs, using
"nice -n-10" to get higher CPU priority, which resulted in fairly
consistent times from run to run (except for "grep", as noted below.)

    ----------------------
    | Routine | CPU secs |
    | ------- | -------: |
    |   grep  |   4.38   |
    |  fgets  |   2.61   |
    | getline |   2.36   |
    | rawscan |   1.60   |
    ----------------------

The "grep" runs displayed an anomaly that I don't understand.
About 90% of the grep runs used very close to 4.38 seconds,
while the other 10% of them used very close to 7.10 seconds.
In the above table, I gave grep a [mulligan](https://www.merriam-webster.com/dictionary/mulligan)
for those runs that took over 7 seconds.)

As you can see from the above table, **rawscan shaves about 30% to
40% off the user CPU times** of the fgets and getline based code.

Rawscan does this without having fgets' problems with embedded nuls,
getline's potentially unbounded malloc's (or leaving difficult to
code reallocations to the caller, as fgets does), or the potential
for dangerous buffer overruns (as gets and some scanf formats do).

Rawscan also provides more sophisticated options for managing
memory usage, from very small pre-allocated buffers that require
no malloc runtime, to sufficiently large buffers to contain the
entire input, directly accessible in memory (without falling
apart when an even larger than expected input shows up.)

# Project Origins:

I've been coding various C routines to read input line by line
for many years, since before stdio even existed.  As an old
school C programmer, the complexities and inefficiencies of
the stdio package, compared to raw read(2) and write(2) calls,
has always annoyed me a bit.

As a coder of quite a few personal text processing tools, I've long
experimented with various safe, fast, line readers.  Those familiar
with Rust's [Result](https://doc.rust-lang.org/std/result/) style of
handling complex function call returns, or with the above mentioned
[LineReader](https://crates.io/crates/linereader) routine, will recognize their influence on this current
rawscan code.

# How to obtain *`rawscan`*

Here's how to obtain *`rawscan`* for your project.

### Prerequisites and portability:

*`Rawscan`* is developed and maintained using the GNU C compiler
on Linux systems, though a `cmake` build system is provided that
(so far only tested on my Ubuntu system) should enable building,
installing, and linking *`rawscan`* on BSD, Windows and MacOS systems,
and with the clang and MSVC compilers.

### Download and build

Download or clone from
[github](https://github.com/ThePythonicCow/rawscan), compile, and install the shared library (librawscan.so) and header file
(rawscan.h) as follows:

Once downloaded or cloned, cd into `rawscan`'s build directory and do:

      cmake ..      # run commands in the build subdirectory
      make
      sudo make install

One is then ready to use rawscan to help write one's own text
or line processing utilities.

# Developing and Contributing

(to be written - how to develop, test and contribute)

### Test harness

A test harness using randomized "fuzzy" input is also under
development and will be included in this repository "soon".

Paul Jackson  
pj@usa.net  
2 Nov 2019
