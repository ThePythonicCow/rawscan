 - Provide examples of rawscan usage
 - Get up to speed with StGIT, for better change history
 - Write README.md "Developing and Contributing" (inviting suggestions, fixes, ...)
 - Checkin the rest of my fuzzy stress tester
 - Automate CI/CD building/testing
 - Write helper routines (e.g., chomp, RAWSCAN field accessors, ...)
 - Accessing state of a paused stream (routines to observe state of a paused stream)
 - Rename "LICENSES" file to "LICENSE.md", so that github thinks I have a license (I provide 3 license options)
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
 - add awk to comparison timings
 - test script varying buffer size, input line count, and total byte count from random line input