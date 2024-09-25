# Rawscan

*`Rawscan`* is written for programmers of utilities that read
data one line at a time.  A line is any sequence of bytes terminated
by a designated delimiter byte.

Use the *`rawscan`* library routines `rs_open`(), `rs_getline`(), and
`rs_close`() to read input, line by line, from any any input stream
(any readable open file descriptor).  See doc/HowToUse.md for the
details of how to use *`rawscan`*.

## Side note -- Project Status

As of January 10, 2020, I have (I hope) concluded my performance
tests and refinements on *`rawscan`*.  A couple of weeks ago, I
had planned on learning NumPy and JupyterLab to analyze these
test results, but it was quicker and easier for me to use the
classic Unix/Linux utilities that I already knew for this.

You can find the performance results (which I find to be *quite*
interesting) below in the **Further Performance Results** section.
For these results, I tested rawscan, rust, grep, sed, awk, python2,
and python3 across a variety of input sizes.

Except for BurntSushi's Rust bstr crate found at [BurntSushi's
bstr](https://github.com/BurntSushi/bstr), rawscan is the fastest
over all input sizes, long or short lines, and few or many lines.
In order to beat this Rust bstr crate on large inputs of short
lines, I had to replace the ordinary C library call `strncmp()`
with  hackish inline code hardwired for the specific test pattern,
because the Rust equivalent of `strncmp()`, namely `str.starts_with`,
is quite a bit faster.  Even with that, I only "beat" Rust bstr
on this test case (many short lines) by the thinnest of margins,
likely within the margin of error.

Now that I have a good set of performance data across various commands
and input sizes, and now that *`rawscan`* is beating all comers,
across all inputs (though in a few cases by razor thin margins),
perhaps I can turn my focus to presenting that performance data
in a more accessible manner, using NumPy and JupyterLab.

## Second side note -- Long Running Goals

This rawscan project is seeking multiple goals for me:

  1. Providing a useful text line reader for some useful C/Linux commandline tools I intend to polish up and publish.
  2. A project to shape my learning (or re-leaning) of such tech as git, cmake, Python, NumPy, and JupyterLab.
  3. A set of well done C/Linux tools that I can rework in Rust, to learn and contribute to that language.

## Now to return to our regularly scheduled Rawscan Broadcast

*`Rawscan`*'s advantages include:

    - fast, safe and robust
    - rarely moves or copies data internally
    - fixed length caller specified buffer doesn't grow
    - can be used in memory constrained situations
    - caller controls buffer size at initialization
    - handles arbitrarily long input lines
    - all lines that fit in buffer returned in one chunk
    - no confusing sscanf format risking bugs and overruns
    - correctly and easily handles embedded nuls
    - beats `gets`, `fgets`, `scanf`, `getline` and `getdelim`
    - .. see "Comparative Analysis" below for details
    - fast ... did I say fast?

## How `rawscan` works internally

*`rawscan`* uses a single fixed length buffer that is allocated
during the `rs_open`() call.  Except as modified by the optional
`rs_set_min1stchunklen()`setting, all lines short enough to fit in
that buffer are returned in one piece.  Lines too long to fit are
returned in multiple chunks.  Whether returned in one piece or in
multiple chunks, all returns use pointers directly into that buffer,
with zero copies.

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
lengths of input. These routines are very heavily optimized, both by
modern compilers, and further by the Intel(tm) MultiMedia eXtension
(MMX) and Intel(tm) Advanced Vector Extensions (AVX) instructions.

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

## Advanced features - Implemented

The following facilities extend the default behavior described
above.  If you use one of these features, then some of the
specifics given above might not apply, as will be described
in the following.

### pause/resume

The *`rawscan`* calling routine can gain some control over when the
contents of the buffer are invalidated by using the optional
**pause/resume** states.

To use **pause/resume**, first invoke `rs_enable_pause`() on the
RAWSCAN stream.  That call just sets a *pause enable flag* on the
stream's internal state and returns immediately.

Then whenever a `rs_getline`() call hits the upper end of the buffer
and needs to invalidate the already returned portion to make room
for more data, that `rs_getline`() call will instead return with a
`RAWSCAN_RESULT.type` of `rt_paused`, without altering the current
buffer contents.

When the calling routine has finished using or copying out
whatever data it's still needs from the buffer, it can then call the
`rs_resume_from_pause`() function on that stream, which will unpause
the stream and enable the next `rs_getline`() call to once again
return another line, and as a necessary side effect, overwrite some
or all of the previously returned data that had been in the buffer.

Such calls to the `rs_resume_from_pause`() function do not alter
the setting of the *pause enable flag* set by `rs_enable_pause`().
Rather such calls to the `rs_resume_from_pause`() function just set
a one-time latch, enabling the next `rs_getline`() call to one-time
overwrite stale data in the buffer in order to make room to read
in more data that it can scan and potentially return to the caller.
Use the `rs_disable_pause`() call to disable the *pause enable flag*.

### `rs_set_min1stchunklen()`

By default, *`rawscan`* guarantees to return all lines that are
shorter than the buffer size as a single complete line.  This can
result in having to copy almost an entire buffer of data lower in
the buffer, in an effort to get the rest of the line to fit in
the upper end of the buffer.  Depending on the specified buffer
size and on the various line lengths typically see in the input,
this can substantially increase the amount of data copying done
internally by the *`rawscan`* routines, and reduce performance.

In the abstract, this potential performance issue is due to
conflating what could be two separate tuning parameters:

 1) The buffer size, which might be tuned for optimum i/o
    performance on the target system.
 2) The guaranteed minimum length of any full lines or
    initial chunks of longer lines.

The `rs_set_min1stchunklen(RAWSCAN *rsp, size_t len)` routine, run
on an already opened *`rawscan`* stream, sets the second parameter
of these two to any specified length `len` less than or equal to
that *`rawscan`*'s buffer size.

Once set to some value `len`, then that *`rawscan`* stream will
always return any line shorter than or equal to length `len` in
one piece, and for any longer line, will always return a first
chunk that is at least `len` bytes long.

This can be useful if your code can efficiently handle shorter lines
and initial line chunks, and you don't want to pay the potential
performance penalty of forcing the *`rawscan`* code to shift larger
initial chunks lower in the buffer in an effort to complete the
entire line in a single return.

The default `len` value is whatever `bufsz` was requested in the
`rs_open()` call, and invoking `rs_set_min1stchunklen(rsp, bufsz)`
to restore that value will exactly restore that default.

## Advanced features - Potential futures

### Accessing state of a paused stream

The internal state of a *`rawscan`* stream is hidden from the using
application.  Application code that includes `rawscan.h` and links
dynamically with `librawscan.so` cannot not see the internals of
the RAWSCAN structure at all, and application code that includes
`rawscan_static.h` can technically see the internals of the RAWSCAN
structure, but is not supposed to look.

There may be future opportunities to expose some of these internal
properties of the RAWSCAN structure to using applications, preferably
using small accessor functions for better portability across internal
changes to internals of the RAWSCAN structure.

### Special Memory Handling

The current rs_open() code allocates, using the Linux sbrk(2) and
brk(2) system calls, the memory space, in the invoking process's
data region, for the buffer, the controlling RAWSCAN structure,
and a copy of the delimiter byte in a separate read-only page just
above the buffer.

A variant of this open could accept a pointer and size to memory
that the caller wants rawscan to use, and with a couple more
minor code tweaks, this buffer could even be read-only, allowing
for example a table in ROM to be parsed, line by line.

### Limited support for multiline "records"

By giving the invoking application more control over when and by how
much partial data in the upper end of the buffer is shifted lower,
an application could arrange to efficiently obtain a multi-line
record all in one piece in the buffer, so long as it allfit.

### Changing delimiterbyte on the fly

The delimiter byte (e.g. '\n' or '\0') could be changed on the fly,
with a routine that changed the page holding the sentinel copy of
the delimiter to be writable, changed that sentinel byte, then set
that page back to read-only.

### Handling constrained memory configurations

If an application working in a very memory constrained situation
did not want to "waste" an entire memory page just to ensure that
the sentinel byte was read-only, then (barring bugs in the code) the
*`rawscan`* library code could place the sentinel byte at the upper
end of the buffer, in the same writable page as the top of the buffer.

### Caller controlled resizing of buffer

With some additional work, routines could be provided to resize the
internal buffer of a *`rawscan`* stream, perhaps including copying
over what existing buffered data would fit.

### Handle Windows style "\r\n" line endings

The simple, most common case, of Windows style "\r\n" line endings
could be trimmed with a wrapper to rs_getline(), that moved the
line.end value returned by rs_getline down by one byte, when it was
pointing at the '\n' character of a two character '\r\n' sequence.

## Comparative Analysis

As part of developing other personal Unix/Linux command line tools
over the years that process data line by line, I've not been happy
with the existing alternatives for scanning input lines:

- stdio's [gets](https://www.studymite.com/blog/strings-in-c#read_using_gets)() is dangerously insecure, allowing buffer overflow.
- stdio's [fgets](https://www.studymite.com/blog/strings-in-c#read_using_fgets)(), [scanf](http://c-faq.com/stdio/scanfprobs.html)() and [getchar](https://www.studymite.com/blog/strings-in-c#read_using_getchar)() use slow double buffering.
- stdio's [fgets](https://www.studymite.com/blog/strings-in-c#read_using_fgets)() cannot correctly handle lines with embedded nuls.
- stdio's fgets() normal usage confuses continuations of lines longer than buffer with start of new lines.
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
closest equivalent to *`rawscan`* that I've found so far.  However,
so far as I can tell, it will split lines that wrap around the end
of the buffer, even if the line could otherwise have fit in the
buffer, had it been at a different offset.

Instead, *`rawscan`* never splits lines that it can fit in
its buffer, or that are shorter than specified by the optional
`rs_set_min1stchunklen()`setting, and never reallocates a bigger
buffer than initially allocated in the `rs_open`() call.  If an
input line that would have fit in the buffer would spill off the
end of the buffer, *`rawscan`* will shift up to `min1stchunklen`
bytes of that partially read line lower in the buffer and continue
reading the rest of it into the buffer, before returning the whole
line in one piece if possible.

## Comparative Performance

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
    | ------- | -------- |
    | grep    | 4.38     |
    | fgets   | 2.61     |
    | getline | 2.36     |
    | rawscan | 1.60     |
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

## Further Performance Results

Further tests comparing staticly included rawscan code
in the "rawscan_static.h" header, with dynamically linked
librawscan.so code as declared in the "rawscan.h" header,
along with other common routines for reading lines of input,
produced the following results.

Compared to including rawscan.h and dynamically linking with
librawscan.so, including "rawscan_static.h" was faster, across
almost all test sizes, large and small, short and long lines.
For sufficiently large test cases such as 65536 lines of 4096 bytes
each, there was no measurable difference between the two; in such
cases, I presume that the raw power of `rawmemchr()` scanning for
the next delimiter dominates all other costs.

The performance gain obtained from using "rawscan_static.h",
versus the more "normal" use of "rawscan.h" with a dynmically linked
librawscan.so, depended on the average line length of the input data.

In tests with many short lines of 8 bytes (plus newline) each, the
static built rawscan ran 10% faster.  In tests with many lines of 64
bytes each, "static" was 2% faster.  In tests with many lines of 512
bytes or longer, there was not a measurable difference between tests
that included "rawscan_static.h", versus that included "rawscan.h"
and dynamically linked to librawscan.so.

This probably reflects what part of the code is most heavily used.

With short lines, the per line call cost of rs_getline() matters
most, whereas with long lines, the underling per character cost
of the rawmemchr() scanning long runs of characters matters most.
The "static" build improves the per rs_getline() call costs, but
has no impact on the underlying per character rawmemchr() scanning
costs that dominate on input with long lines.

The above performance comparisons apply for large file inputs,
such as 65536 lines used to obtain the above results.  For small
file inputs, the cost of starting up the test command dominates.

For example, on test inputs of 8 lines of 8 chars (plus newline)
each, the "static" builds had about a 3% advantage over the
dynmaically linked build, and both variants had about an 18%
advantage over their nearest competitors using rust, as well as
a 27% advantage over the "grep" command line utility.  The other
utilities sed, awk, python2, and python3 were progressively
slower, in that order, with python3 being over 19 times slower
on these small inputs of 8 lines having 8+1 characters each.

## Project Origins

I've been coding various C routines to read input line by line
for many years, since before stdio even existed.  As an old
school C programmer, the complexities and inefficiencies of
the stdio package, compared to raw read(2) and write(2) calls,
have always annoyed me a bit.

As a coder of quite a few personal text processing tools, I've long
experimented with various safe, fast, line readers.  Those familiar
with Rust's [Result](https://doc.rust-lang.org/std/result/) style of
handling complex function call returns, or with the above mentioned
[LineReader](https://crates.io/crates/linereader) routine, will
recognize their influence on this current rawscan code.

## How to obtain *`rawscan`*

Here's how to obtain *`rawscan`* for your project.

### Prerequisites and portability

*`Rawscan`* is developed and maintained using the GNU C compiler
on Linux systems, though a `cmake` build system is provided that
(so far only tested on my Ubuntu system) should enable porting
(including removing any gcc and Linux dependencies), building,
installing, and linking *`rawscan`* on BSD, Windows and MacOS
systems, and with the clang and MSVC compilers.

### Download and build

Download or clone from
[github](https://github.com/ThePythonicCow/rawscan), compile, and install the shared library (`librawscan.so`) and header files
(`rawscan.h` and `rawscan_static.h`) as follows:

Once downloaded or cloned, cd into `rawscan`'s build directory and do:

      cmake ..      # run commands in the build subdirectory
      make
      sudo make install

One is then ready to use rawscan to help write one's own text
or line processing utilities.

## Developing and Contributing

If you're interested in using or porting *`rawscan`*, or would
like to suggest an additional feature that might make *`rawscan`*
more attractive to you, let me know, at my email address below,
or via the *`rawscan`* repository on github.  Thanks!

### Test harness

The source file `source/tests/random_line_generator.c` generates
random input, of specified number of lines, of specified length(s)
of constant length or of random lengths within a specified range.

The source file `source/tests/compare_various_apis.sh` provides
a framework to run tests on several competing commands at a time,
in parallel, from a range of test in put sizes, and collects
statistics such as user and system cpu time, elapsed time, and
page fault rates from the runs.  A few other scripts in the
`source/tests` directory perform preliminary analysis of the
resulting data.

Paul Jackson  
pj@usa.net  

Project began in November of 2019
