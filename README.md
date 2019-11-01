# RawscanStressTest

Copy input (file desc 0) to output (file desc 1), in order
to stress the rawscan code.

Uses rawscan routines to read input one line at a time.
Thus this is an "inefficient" emulation of the "cat"
command, in its simplest guise - copying input to output.

Takes no arguments.  Enables the "allow_rawscan_force_bufsz_env"
setting of the rawscan code, which allows the test harness
invoking rawscanstresstest to control the buffer size used by
rawscan through the exported $_RAWSCAN_FORCE_BUFSZ_ variable,
which specifies the buffer size in bytes.  Very small buffer sizes
(as little as one byte) more heavily stress the end of buffer code
and the code that handles lines too long to fit in the buffer by
returning multiple chunks.

The test input for this program is intended to be short lines of
ASCII text, produced by the GenerateMillionLineTestData program.
These lines consist of zero to fifteen ASCII characters from a
subset (the first 32 characters) of the RFC 4648 base64 set
[A-Za-z0-9+/], each line (except perhaps the last) terminated by a
newline ('\n').

The script `runstresstest` included in this RawscanStressTest
will run various tests, for varying numbers of input lines
and varying buffer sizes, verifying that what comes out of
rawscanstresstest matches what goes in.

## Motivation:
The purpose of this module is to stress test "rawscan_getline()",
a high performance "getline()" variant.

## Underlying technology:
This program is currently of use with the GNU C compiler on Linux
systems, though it should be easily portable to other systems.

## How to build and run test:
The working source consists of a single C language source file
which includes the rawscan code, and a shell script driver.

### Download and build
Download it, compile it, and run it.

Minimal simple compile command:

  ```gcc -o rawscanstresstest.c rawscanstresstest.c.c```

Fancy optimized compile command:

```gcc -fwhole-program -march=native -O3 -Wall -pedantic -Werror -o rawscanstresstest.c rawscanstresstest.c.c```

### Running the test:
