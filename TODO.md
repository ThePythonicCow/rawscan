 - Provide examples of rawscan usage
   Document (1) how to obtain, install, and build (2) how to run performance tests and (3) how to use in other code.
 - Write README.md "Developing and Contributing" (inviting suggestions, fixes, ...)
 - Automate CI/CD building/testing
 - Write helper routines (e.g., chomp, RAWSCAN field accessors, ...)
 - Accessing state of a paused stream (routines to observe state of a paused stream)
 - Add a Contributing.md file, with above "Developing and Contributing" from what's now in my README.md
 - Earn some github badges
 - Special Memory Handling (routines to preallocate or preassign the buffer, without the need for any runtime malloc or other heap allocator.)
 - Limited support for multiline "records" (routines enabling handling multiline records, so long as the entire record still fits in the buffer.)
 - Changing delimiterbyte on the fly (switching delimiterbyte on the fly)
 - Handling constrained memory configurations (disabling full readonly page for sentinel)
 - Caller controlled resizing of buffer
 - Caller controlled shifting of data down, for limited multiline record support
 - man page
 - code coverage
 - Test script varying buffer size, input line count, and total byte count from random line input
 - Support user supplied input routine as option to read(2) from a file descriptor.
 - Should be able to rs_getline's from a read-only array in ROM
   Refine processing and presentation of performance benchmarks
   Present performance comparisons (rawscan versus competition) both text and gui/graphs
