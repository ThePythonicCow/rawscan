
# How to use *`rawscan`*

Here's how you can use *`rawscan`* in your code:

### Example Code

(to be written - pointers to a few example uses)

### RAWSCAN_RESULT

To see example code using the structure returned by `rs_getline`(),
see the above Example Code section.

The return value from `rs_getline`() is the RAWSCAN_RESULT
structure defined in `rawscan.h`.  This return structure contains a
typed union, handling any of the several possible results from a
`rs_getline`() call, such as another full line, a chunk of a
long line, an error, an end-of-file, a paused input stream, and
so forth.

Here is the C definition of the *`RAWSCAN_RESULT`* structure:

```
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
```
The *`rs_result_type`* that determines which of the union field(s)
are valid on any given return is the following enum choice:
```
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
```
This is not a conventional way to handle library calls, such as
*`rs_getline`*(), that may return a variety of results, of different
kinds.  More conventional 'C' code will use some combination of
overloading the return value, say as either a pointer or a NULL or
as a negative or non-negative integer, along with passing pointer
arguments that address locations where the library may write
additional return results.

These more conventional methods can be error prone.

The above RAWSCAN_RESULT supports more explicit, albeit less
conventional, return handling that uses a C "switch" on the
RAWSCAN_RESULT.rt type field.  This should reduce coding mistakes
when using this library.

Note in particular that a copy of the RAWSCAN_RESULT structure
is returned, not a pointer to such a structure.  Returning a
copy of a three word structure rather than a pointer to such
is slightly more expensive in the immediate return, but saves
having to handle the costs and increased risk for bugs that
returning a pointer to a result structure would entail.

Lines (sequences of bytes ending in the delimiterbyte byte)
returned by `rs_getline`() are byte arrays in the interval
`[RAWSCAN_RESULT.begin, RAWSCAN_RESULT.end]`, inclusive.  They
reside somewhere in a heap allocated buffer that is at least one
page larger than the size specified in the `rs_open`() call,
in order to hold the read-only sentinel copy of the delimiterbyte,
as discussed above in the `rawmemchr` section.

The above `RAWSCAN_RESULT.begin` will point to the first byte in
any line returned by `rs_getline`(), and `RAWSCAN_RESULT.end`
will point to the last character (either the delimiterbyte,
or the very last byte in the input stream if that comes first.)
The "line" described by these return values from `rs_getline`()
will remain valid at least until the next `rs_getline`() or
`rs_close`() call.

When returning a line, the `RAWSCAN_RESULT.end` pointer will point
to the delimiterbyte (e.g. to the newline '\n') ending that line,
or to the last byte in the file, if that's not a delimiterbyte.
If the caller wants that delimiterbyte replaced with (for example)
a nul, such as when directly using the returned line as a filename
to be passed back into the kernel as a nul-terminated pathname
string, _and_ if that byte is a delimiterbyte (the normal case),
then the caller can overwrite that byte, directly in the *`rawscan`*
return buffer, if that facilitates the intended use of that line.

If the byte pointed to by the `RAWSCAN_RESULT.end` pointer is _not_
that stream's delimiterbyte, then that means that the stream ended
on some other byte, such as a file without a trailing newline.
In that case, as a special case, `rs_getline`() guarantees that the
_next_ byte after `RAWSCAN_RESULT.end` is still within the writable
buffer, so that the caller can append a suitable terminator line,
such as a newline ('\n') or nul byte ('\0'), if that's useful.

That heap allocated *`rawscan`* buffer is freed in the `rs_close`()
call, invalidating any previously returned `rs_getline`() results.
That heap allocated buffer is never moved or expanded, once setup
in the `rs_open`() call, until the `rs_close`() call.  But
subsequent `rs_getline`() calls may invalidate data in that buffer
by overwriting or shifting it downward. So accessing stale results
from an earlier `rs_getline`() call, after additional calls
of `rs_getline`(), prior to the `rs_close`() of that stream,
won't directly cause an invalid memory access, but may return invalid
data, unless carefully sequenced using the pause/resume facility.
