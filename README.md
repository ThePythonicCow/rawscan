# Rawscan

Use rawscan_*() routines to scan input, line by line.

Rawscan is faster and safer than "Standard IO" stdio buffered input
routines or other alternatives (e.g. "getline").

"Lines" can be any records terminated by a specified delimiter
character (byte).  Only only one allocated buffer of specified size
is used, handling arbitrarily long lines by returning lines that
don't fit in that buffer in multiple chunks.

The "raw" in "rawscan" means that these routines don't use any of the
buffered stdio apparatus, but rather directly call the raw read(2)
system call.  The "scan" means that these routines handle input only,
not output.

rawscan_getline() reads large chunks into a buffer, and returns
portions of that buffer terminated by nul, newline, or whatever
other "delimiterbyte" that rawscan stream is configured for.
These portions are called "lines" below, though they can be any
sequence of bytes terminated either by the specified delimiterbyte,
or by the end of the input stream, if that last byte is not that
rawscan stream's specified delimiterbyte.

Except as noted in the pause/resume discussion below, whenever
rawscan_getline() finds that it only has a partial line left in the
upper end of its buffer, it moves that partial line lower down in
its buffer and continues, always returning full lines in one piece
so long as the line fits in the caller specified buffer.

Thus rawscan_getline() is not "zero-copy", but "infrequent copy",
so long as it's configured in the rawscan_open() call to have an
internal buffer that is usually longer than the typical line it
will be returning.

This strategy relies on having a fixed upper length to the line
length that we need to parse conveniently (in one piece), and being
willing to accept returns of any lines longer than that length in
multiple chunks, rather than as a single string.

Except when using the optional pause/resume states, ordinary usage
of rawscan on any particular input stream must be single threaded.
If two threads invoked rawscan_getline() on the same RAWSCAN stream,
then one of these calls could cause the buffered data already
returned to the other thread to be moved or overwritten, while
the other thread was still accessing it.  Multi-thread access to
a RAWSCAN stream might be safe under the management of a wrapper
that handled the synchronizing locking, and that copied out data
being returned on a rawscan_getline() call to the invoking thread's
private data area, before returning.  A sufficiently sophisticated
such wrapper manager could use rawscan's pause/resume facility
in order to allow parallel read-only access to the buffer, while
locking and single threading rawscan_getline() calls that update the
thread, and joining or blocking all other threads from read-only
access during a rawscan_getline() and rawscan_resume_from_pause()
whenever the rawscan stream paused.  Any such multi-threading of
this library is beyond the scope of this current code, but perhaps
could be implemented on top of this current code.

The rawscan calling routine can gain some control over when the
contents of the buffer are invalidated by using the optional
**pause/resume states**.  First invoke rawscan_enable_pause() on
the RAWSCAN stream.  Then whenever a rawscan_getline() or similar
call needs to invalidate the current contents of the buffer for
that stream, that rawscan_getline() call will instead return with a
RAWSCAN_RESULT.type of rt_paused, without invalidating the current
buffer contents.  When the calling routine has finished whatever
operations or copied out whatever data it's still using in the
RAWSCAN buffer, it can then call the rawscan_resume_from_pause()
function on that stream, which will unpause the stream and enable
subsequent rawscan_getline() calls to succeed again.

For applications that can ignore overly long lines, the rawscan
interface makes that quick and easy to do so.  Just ignore
RAWSCAN_RESULT's with result types of rt_start_longline,
rt_within_longline or rt_longline_ended.

One key technique that can be used to obtain excellent performance
(several times fewer user CPU cycles than stdio buffer based
solutions) is the use of strchr, strchrnul, or rawmemchr to scan
considerable lengths of input. These routines are very heavily
optimized, both by gcc, and further by the Intel(tm) MultiMedia
eXtension (MMX) and Intel(tm) Advanced Vector Extensions (AVX)
instructions. Whenever a problem can be reduced to scanning large
spans of buffer looking for a single particular character, these
routines can race through the data at maximum speed, operating on
data perhaps 128, 256 or 512 bits at a time, depending on compiler
and CPU technology.  This is all much faster than doing character
at a time:
     `while ((c = getchar()) != EOF) switch (c) { ... }`
loops in hand-coded C from a STDIO input buffer, with multiple tests
for state and the value of each character 'c' in each loop iteration.

The fastest scanner in glibc of strchr, strchrnul, or rawmemchr is
rawmemchr, as it -only- stops scanning when it sees the requested
character.  The x86_64 assembly code in glibc for these routines is
very fast, at least on "modern" (recent years as of this writing in
2019) Intel and AMD x86_64 processors with AVX vector instructions.
Rawmemchr will happily run to, and beyond, the limits of the memory
segment it's searching, causing a SIGSEGV or other such crash,
if the character it's looking for is not found sooner.  So this
code sets up a read-only page, just past the main buffer, with the
configured delimiterbyte in the first byte of that read-only page,
ensuring that calls to rawmemchr() will terminate there, if not
sooner, regardless of what's in the buffer at the time.

The return value from rawscan_getline() is the RAWSCAN_RESULT
structure defined in rawscan.h.

When returning a line, the RAWSCAN_RESULT.end pointer will point to
the newline '\n' character (or whatever delimiterbyte was established
in the rawscan_open() call) that ends the line being returned.
If the caller wants that newline replaced with (for example) a nul,
such as when using the returned line as a potential filename to be
passed back into the kernel as a nul-terminated pathname string,
then the caller can overwrite that byte, directly in the returned
buffer.

Lines (sequences of bytes ending in the delimiterbyte byte)
returned by rawscan_getline() are byte arrays defined by
[RAWSCAN_RESULT->begin, RAWSCAN_RESULT->end], inclusive.  They
reside somewhere in a heap allocated buffer that is at least one
page larger than the size specified in the rawscan_open() call,
in order to hold a read-only sentinel copy of the delimiter
above the active portion of the buffer.

The above RAWSCAN_RESULT->begin will point to the first byte in any
line returned by rawscan_getline(), and RAWSCAN_RESULT->end will
point to the last character (typically the configured delimiter byte,
or perhaps the very last byte in the input stream if that was not
the delimiter byte.)  The "line" described by these return values
from rawscan_getline() will remain valid at least until the next
rawscan_getline() or rawscan_close() call.

That heap allocated rawscan buffer is freed in the rawscan_close()
call, invalidating any previously returned rawscan_getline() results.
That heap allocated buffer is never moved or expanded, once setup
in the rawscan_open() call, until the rawscan_close() call.  But
subsequent rawscan_getline() calls may invalidate data in that buffer
by overwriting or shifting it downward. So accessing stale results
from an earlier rawscan_getline() call, after additional calls
of rawscan_getline(), prior to the rawscan_close() of that stream,
won't directly cause an invalid memory access, but may return invalid
data, unless carefully sequenced using the pause/resume facility.

## Motivation and Comparative Analysis of Alternatives:
As part of developing other Unix/Linux command line tools over the
years that process data line by line, I've never been happy with
the existing alternatives for scanning input lines:
 -- stdio's gets() is dangerously insecure, allowing buffer overflow
 -- stdio's fgets(), scanf(), getchar(), ... double buffer input
 -- stdio's fgets() cannot correctly handle lines with embedded nuls.
 -- getline() grows (realloc's) its buffer to handle longer lines
 -- scanf() risks failed format matching and buffer overflows
 -- getchar() loops are accurate, but even slower
 -- getdelim() is just like getline(), with a configurable delimiter

The buffer growing reallocations in getline() present the risk of
a denial of service attack on an application using getline() to
read insecure data.  Getline() could be made to keep growing its
buffer until the invoking program died of memory exhaustion, or
until the supporting system slowed unacceptably swapping a huge
program.

Rust's [LineReader](https://crates.io/crates/linereader) is the
closest equivalent to rawscan that I've found so far.  However it
will split lines that wrap around the end of the buffer, even if
the line could otherwise have fit in the buffer, had it been at a
different offset.

Instead, rawscan never splits lines that it can fit in its buffer,
and never reallocates a bigger buffer than initially allocated in
the rawscan_open() call.  If an input line that would have fit in
the buffer would spill off the end of the buffer, rawscan will
shift that partially read line lower in the buffer and continue
reading the rest of it into the buffer, before returning the whole
line in one piece.

## History:
I've been coding various routines to read input line by line
for many years, since before stdio even existed.  As an old
school C programmer, the complexities and inefficiencies of
the stdio package, compared to raw read(2) and write(2) calls,
has always annoyed me a bit.  As a coder of quite a few personal
text processing tools, I've long experimented with various safe,
fast, line readers.  Those familiar with Rust's "Result" style
of handling complex function call returns, or with the above
mentioned LineReader routine, will recognize their influence
on this current rawscan code.

## Underlying technology:
This program is developed and maintained using the GNU C compiler
on Linux systems, though a cmake build system is provided that
(so far untested) should enable building it on BSD, Windows and
MacOS systems as well.

## Download and build
Download it, compile it, and install its shared library
(librawscan.so) and header file (rawscan.h) as follows:

Once downloaded or cloned, cd into its build directory and do:

  cmake ..      # run commands in the build subdirectory
  make
  sudo make install

One is then ready to use it in one's own source code.

## Test harness
A test harness using randomized "fuzzy" input is also under
development and will be included in this repository "soon".

Paul Jackson
pj@usa.net
2 Nov 2019
