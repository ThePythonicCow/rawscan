# Rawscan

*`Rawscan`* is written for programmers of utilities that read
data one line at a time.  A line is any sequence of bytes terminated
by a designated delimiter byte.

Use `rawscan_open`(), `rawscan_getline`(), and `rawscan_close`()
to read input, line by line.

*`Rawscan`*'s advantages include:

- no double buffering that wastes cpu and memory
- rarely copies incoming data before returning it
- safer, with no risk of buffer overruns
- no confusing sscanf format risking bugs and overruns
- no complex and expensive per byte logic
- correctly and easily handles embedded nuls
- no chance of repeated, increasingly big, mallocs

# How to use *`rawscan`*

Here's how you can use *`rawscan`* in your code:

### Example Code

(to be written - pointers to a few example uses)

### RAWSCAN_RESULT

To see example code using the structure returned by `rawscan_getline`(),
see the above Example Code section.

The return value from `rawscan_getline`() is the RAWSCAN_RESULT
structure defined in `rawscan.h`.  This return structure contains a
typed union, handling any of the several possible results from a
`rawscan_getline`() call, such as another full line, a chunk of a
long line, an error, an end-of-file, a paused input stream, and
so forth.

When returning a line, the `RAWSCAN_RESULT.end` pointer will point
to the delimiterbyte (e.g. to the newline '\n') ending that line.
If the caller wants that delimiterbyte replaced with (for example)
a nul, such as when directly using the returned line as a filename
to be passed back into the kernel as a nul-terminated pathname
string, then the caller can overwrite that byte, directly in the
*`rawscan`* return buffer.

Lines (sequences of bytes ending in the delimiterbyte byte)
returned by `rawscan_getline`() are byte arrays in the interval
`[RAWSCAN_RESULT.begin, RAWSCAN_RESULT.end]`, inclusive.  They
reside somewhere in a heap allocated buffer that is at least one
page larger than the size specified in the `rawscan_open`() call,
in order to hold the read-only sentinel copy of the delimiterbyte,
as discussed above in the `rawmemchr` section.

The above `RAWSCAN_RESULT.begin` will point to the first byte in
any line returned by `rawscan_getline`(), and `RAWSCAN_RESULT.end`
will point to the last character (either the delimiterbyte,
or the very last byte in the input stream if that comes first.)
The "line" described by these return values from `rawscan_getline`()
will remain valid at least until the next `rawscan_getline`() or
`rawscan_close`() call.

That heap allocated *`rawscan`* buffer is freed in the `rawscan_close`()
call, invalidating any previously returned `rawscan_getline`() results.
That heap allocated buffer is never moved or expanded, once setup
in the `rawscan_open`() call, until the `rawscan_close`() call.  But
subsequent `rawscan_getline`() calls may invalidate data in that buffer
by overwriting or shifting it downward. So accessing stale results
from an earlier `rawscan_getline`() call, after additional calls
of `rawscan_getline`(), prior to the `rawscan_close`() of that stream,
won't directly cause an invalid memory access, but may return invalid
data, unless carefully sequenced using the pause/resume facility.

# How `rawscan` works internally

Except as noted in **Special Memory Handling**, below, *`rawscan`*
uses a single fixed length buffer that is allocated during the
`rawscan_open`() call.  All lines short enough to fit in that buffer
are returned in one piece, using pointers directly into that buffer,
with zero copies.  Lines too long to fit are returned in multiple
chunks.

Except as noted in the **pause/resume** discussion below, whenever
`rawscan_getline`() finds that it only has a partial line left in
the upper end of its buffer, it shifts that partial line lower down
in its buffer and continues, always trying to return full lines in
one piece.  For this reason in particular, __*note that*__ each
`rawscan_getline`() call might invalidate the pointers returned
in prior calls (unless the **pause/resume** feature is used.)

### (nearly) zero-copy

Thus `rawscan_getline`() is not "zero-copy", but "infrequent copy",
so long as it's configured in the `rawscan_open`() call to have an
internal buffer that is usually longer than the typical lines it's
returning.

This strategy works especially well when there is a given upper
length to the line length that needs to be parsed conveniently (in
one piece), and longer lines can be ignored or handled in chunks.

For applications that can ignore overly long lines, the *`rawscan`*
interface makes that quick and easy to do so.  Just ignore
RAWSCAN_RESULT's with result types of `rt_start_longline`,
`rt_within_longline` or `rt_longline_ended`.

### not safely multi-threaded (sorry)

The use of *`rawscan`* on any particular input stream should be single
threaded.  Multiple parallel threads trying to access and update
the same *`rawscan`* stream will likely corrupt the `RAWSCAN` control
structure, and might prematurely invalidate some line being returned
to another thread.

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

The following pause/resume,memory, multiline, and delimiterbyte
facilities extend the default behavior described above.  If you
use one of these features, then some of the specifics given above
might not apply, as will be described in the following.

### pause/resume

The *`rawscan`* calling routine can gain some control over when the
contents of the buffer are invalidated by using the optional
**pause/resume** states.

To use **pause/resume**, first invoke `rawscan_enable_pause`()
on the RAWSCAN stream.  Then whenever a `rawscan_getline`() would
have needed to invalidate the current contents of the buffer for
that stream, that `rawscan_getline`() call will instead return
with a `RAWSCAN_RESULT.type` of `rt_paused`, without invalidating
the current buffer contents.  When the calling routine has finished
using or copied out whatever data it's still using in the *`rawscan`*
buffer, it can then call the `rawscan_resume_from_pause`() function
on that stream, which will unpause the stream and enable subsequent
`rawscan_getline`() calls to succeed again.

### Special Memory Handling

(to be written - routines to preallocate or preassign the buffer, without the need for any runtime malloc or other heap allocator.)

### Limited support for multiline "records"

(to be written - routines enabling handling multiline records,
so long as the entire record still fits in the buffer.)

### Changing delimiterbyte on the fly

(to be written - switching delimiterbyte on the fly)

### Handling constrained memory configurations

(to be written - disabling full readonly page for sentinel)

# Comparative Analysis:

As part of developing other Unix/Linux command line tools over the
years that process data line by line, I've never been happy with
the existing alternatives for scanning input lines:

 - stdio's gets() is dangerously insecure, allowing buffer overflow.
 - stdio's fgets(), scanf() and getchar() use slow double buffering.
 - stdio's fgets() cannot correctly handle lines with embedded nuls.
 - scanf() risks failed format matching and buffer overflows
 - getchar() loops are accurate, but with slower per byte logic
 - getline() grows (realloc's) its buffer to handle longer lines
 - getdelim() is just like getline(), with a configurable delimiter

The buffer growing reallocations in `getline`() present the risk
of a denial of service attack.  Applications using `getline`()
on untrusted input can be forced to keep growing `getline`()'s
buffer until the application dies of memory exhaustion, or until
the supporting system slows unacceptably swapping a huge program.

Rust's [LineReader](https://crates.io/crates/linereader) is the
closest equivalent to rawscan that I've found so far.  However it
will split lines that wrap around the end of the buffer, even if
the line could otherwise have fit in the buffer, had it been at a
different offset.

Instead, *`rawscan`* never splits lines that it can fit in its buffer,
and never reallocates a bigger buffer than initially allocated in
the `rawscan_open`() call.  If an input line that would have fit in
the buffer would spill off the end of the buffer, rawscan will
shift that partially read line lower in the buffer and continue
reading the rest of it into the buffer, before returning the whole
line in one piece.

# Project Origins:

I've been coding various C routines to read input line by line
for many years, since before stdio even existed.  As an old
school C programmer, the complexities and inefficiencies of
the stdio package, compared to raw read(2) and write(2) calls,
has always annoyed me a bit.  As a coder of quite a few personal
text processing tools, I've long experimented with various safe,
fast, line readers.  Those familiar with Rust's "Result" style
of handling complex function call returns, or with the above
mentioned LineReader routine, will recognize their influence
on this current rawscan code.

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
