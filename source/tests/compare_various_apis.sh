#!/bin/zsh
#
# Compare how many user CPU cycles it takes each of several
# different ways of reading and examining input, line by line,
# including by using the 'rawscan' rs_getline() method.

# Spoiler alert: rs_getline() is the fastest.
# ...  But rust "bstr" tied within margin of error on short lines,
# ...  and it required a special "static" compile of rs_getline
# ...  to get the winning rawscan results.

# If the "grep '^abc'" command below notices that its output is going
# directly to /dev/null, then grep "cheats" and runs an order of
# magnitude faster (fewer user CPU cycles), still reading its input,
# but sending that input directly to /dev/null using the splice(2)
# system call.  So we pipe the stdout (fd 1) of all our commands
# via a pipe to the cat command, "| cat > /dev/null", so that they
# don't notice we're ignoring their output, and so they can't "cheat".

# All the various *_test, python, grep, awk, sed, ... timed commands
# below have equivalent behavior.  They all look for input lines
# that begin with the three letters "abc", and copy those lines
# (and nothing else) to their output.  Given any input stream,
# such as from some sort of random_line_generator invocation, they
# will all produce the exact same output.  Our purpose here is to
# compare how many user CPU cycles it takes each of them to do that.

# Must use zsh's builtin time command, not the classical /bin/time
# binary, because that binary emits its results using writes of 1
# byte at a time ... which results in a jumbled mess when several
# time'd commands try that in parallel.  The zsh time builtin does
# a single write of its results, so parallel such writes can be
# reliably read.

# Multios: Enable zsh's multi-pipe into parallel ">>(...)" processes
# below.  Since the random_line_generator invocations below are
# the most expensive command in this script, we use some zsh file
# redirection and replication magic in order to feed copies of each
# single invocation of random_line_generator to all our test cases
# in parallel.  Thanks, Ryzen, for all the nice cores to run this on.

setopt multios

PATH=../build:.:$PATH
random=$RANDOM

# Aliases for longer paths used below:
rust_bstr=rust_bstr/target/release/rust_bstr
rust_bufreader=rust_bufreader/target/release/rawscan_bufreader_test

repeat 100 {            # How many times we loop repeating all our tests
    minlen=10
    repeat 9 {          # How many times we double our test line lengths
        maxlen=$(($minlen * 2))

        echo "Line lengths [ $minlen , $maxlen ]"
        (
            (
                random_line_generator -n 1000000 -m $minlen -M $maxlen -B -R |
                      >>(
                        (export TIMEFMT='fgets %U user';   time fgets_test)
                    ) >>(
                        (export TIMEFMT='getline %U user'; time getline_test)
                    ) >>(
                        (export TIMEFMT='rawscan %U user'; time rawscan_test)
                    ) >>(
                        (export TIMEFMT='rawscan_static %U user'; time rawscan_static_test)
                    ) >>(
                        (export TIMEFMT='python2 %U user'; time python2_test)
                    ) >>(
                        (export TIMEFMT='python3 %U user'; time python3_test)
                    ) >>(
                        (export TIMEFMT='grep %U user';    time grep '^abc')
                    ) >>(
                        (export TIMEFMT='awk %U user';     time awk '/^abc/')
                    ) >>(
                        (export TIMEFMT='sed %U user';     time sed -n '/^abc/p')
                    ) >>(
                        (export TIMEFMT='rust_bstr %U user'; time $rust_bstr)
                    ) >>(
                        (export TIMEFMT='rust_bufreader %U user'; time $rust_bufreader)
                    )
            ) | cat > /dev/null
        ) 2>&1 | sort

        minlen=$maxlen
    }
} | tee compare_various_apis.rawdata.out.$random |
    zsh summarize_results.sh |
    tee compare_various_apis.results.$random
