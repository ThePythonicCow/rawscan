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
# (and nothing else) to their output.  Given any input stream, such
# as from some sort of random_line_generator invocation, they will
# all produce the exact same output.  Our purpose here is to compare
# how much time and resources it takes each of them to do that.

PATH=.:$PATH
random=$RANDOM

shm=/dev/shm/rawscan_compare_various_apis_test_data.$RANDOM.$$
trap 'rm -f $shm.?; trap 0; exit 0' 0 1 2 3 15

test_data_file=$shm.1

echo Beginning: $(date)

# Aliases for test commands and scripts used below.  It's ok if
# these files don't exist; those tests will be silently skipped.
# One could also build Debug, rather than Release, versions of
# these rust test commands, but adapting this script for that case
# is left as an exercise to the reader.  The present focus in this
# test script is to optimize, compare, and provide raw data for
# analyzing the performance of each alternative.

export rust_bstr=../../rust_bstr-Release/release/rust_bstr
export rust_bufreader=../../rust_bufreader-Release/release/rust_bufreader

# Output time format is CSV, for convenient consumption by data
# analysis tools.  The time format TIMEFMT (see zshparam(1)) is
# for the zsh builtin time command (see zshmisc(1)), not for
# whatever binary time command is in some bin directory on $PATH.

# Must use zsh's builtin time command, not the classical /bin/time
# binary, because that binary emits its results using writes of 1
# byte at a time ... which results in a jumbled mess when several
# time'd commands try that in parallel.  The zsh time builtin does
# a single write of its results, so parallel such writes can be
# reliably read.

title="cmdbasename,nlines,linelen,nloops,maxrssmemkb,totalcpupercent,minorpagefaults,usercpusecs,syscpusecs,elapsedsecs"

# Need nlines and linelen in scope to be set in main outer loop,
# far below, then displayed in run_test(), just below.
typeset -gx -i 10 nlines
typeset -gx -i 10 linelen

# Silently skips tests that invoke some command we don't have,
# such as awk, rust or some version of python.  If we do have the
# specified command, run it, with args, and print its timings,
# using a custom zsh TIMEFMT format that will be understood by the
# summarize_results script, further down.

function run_test()
{
        cmdfullpath=$(whence $1)
        cmdbasename=$(basename $cmdfullpath)
        cmdwithargs="$*"

        test -f "$cmdfullpath" -a -x "$cmdfullpath" || { sleep 1; return; }

        # Final CSV fields in each line of test result output:
        #   $cmdbasename,$nlines,$linelen,$nloops,$TIMEFMT
        # where the "time" command (a zsh builtin) just below produces
        # its portion of the output in the following $TIMEFMT format:

        export TIMEFMT="%M,%P,%R,%U,%S,%E"

        # The %P zsh time spec appends a '%' to each value, and the
        # %U, %S, and %E time specs append a 's' (for seconds).
        # This is annoying to import into some data analysis tools
        # that resist seeing these as numbers rather than strings.
        # So the "sed" command below trims off these trailing '%'
        # and 's' characters.

        # What's up with the crazy fd 1, 2, and 3 redirections:
        #
        # The following:
        #
        # (1) places timings and iteration count on fd 2,
        # (2) redirects normal test output from fd 1 to fd 3,
        # (3) redirects timings and count from fd 2 to fd 1,
        # (4) constructs test CSV result line,
        # (5) writes that result line out fd 2,
        # (6) redirects fd 3 back to fd 1 on to /dev/null,
        # (7) redirects the test results from fd 2 to fd 1.

        ( ( ( time zsh -c "

            #            === BEWARE ===
            #
            # Delicate quoting lies here.  Do NOT put any
            # double quotes inside this zsh -c ... script,
            # or you will almost surely totally break it.

            # Zsh module zsh/datetime provides parameter $EPOCHSECONDS.
            # $EPOCHSECONDS is current time since epoch in seconds
            zmodload zsh/datetime

            let finishtime=\$EPOCHSECONDS+10

            typeset -i 10 nloops=0

            # Here lies the inner loop of all this test apparatus:
            #
            # We loop for 10 seconds, until we reach $finishtime,
            # repeatedly scanning the same $test_data_file, using
            # whatever command was specified by $cmdwithargs
            #
            # The second echo just below is the main CSV
            # formatted output of this test script.

            while [[ \$EPOCHSECONDS -lt \$finishtime ]]
            do
                cat $test_data_file | eval \"$cmdwithargs\"
                nloops+=1
            done

            echo -n \$nloops, 1>&2
        " ) 1>&3 ) 2>&1 |
        sed -e 's/[s%],/,/g' -e 's/s$//' |  # cut tailing 's' '%' off timings
        while read nloops_and_timings
            do
                echo $cmdbasename,$nlines,$linelen,$nloops_and_timings
            done 1>&2
        ) 3>&1
}

# Repeat tests a bunch of times, unless interrupted sooner, which is ok.

( echo "$title"; repeat 1000 {

    # We choose test cases of 8 to a 65536 lines, of lengths
    # 8 to 4096 bytes, and then repeatedly run our various test
    # cases in a loop (above "run_test()" code).

    for log2linelen in $(seq 3 12)        # line lengths 8 to 4096
    do
        for log2nlines in $(seq 3 16)     # number lines 8 to 65536
        do
            linelen=$(( 2 ** $log2linelen ))
            nlines=$(( 2 ** $log2nlines ))
            random_line_generator -n $nlines -m $linelen -M $linelen -R > $shm.1

            # Feed data into tests using "cat ... |" pipeline,
            # rather than i/o redirction '<', to keep commands such
            # as "grep" from cheating and mmap'ing their input.
            # Similarly, discard any "stdout" test output via another
            # "| cat > /dev/null" pipeline, so that grep can't
            # cheat again by using "splice(2)" to discard output
            # more quickly if it notices that its output goes to
            # "/dev/null."  Perhaps more importantly, since the
            # primary expected use for rawscan is by text processing
            # commands in pipelines, it seemed most relevant to test
            # rawscan and its various competitors in such pipelines.

            # Feed each test command from a separate dedicated
            # "cat ... |" pipeline, rather than using zsh multios
            # parallel magic to duplicate a single source to each
            # consumer. Do this so we can get useful, independent,
            # measurements of their total elapsed time.
            #
            # BEWARE: Elapsed timings from the following won't mean
            # as much if you don't have enough cores/threads to run
            # all these in parallel, including the input and output
            # cat commands and (above) the encompassing zsh commands.

            # Let's reuse the above random input 10 times, to
            # amortize the cost of generating it.

            repeat 10 {
                (
                    (
                        cat $shm.1 | run_test  $rust_bstr &
                        cat $shm.1 | run_test  rawscan_test &
                        cat $shm.1 | run_test  rawscan_static_test &
                        cat $shm.1 | run_test  getline_test &
                        cat $shm.1 | run_test  fgets_test &
                        cat $shm.1 | run_test  grep '^abc' &
                        cat $shm.1 | run_test  sed -n '/^abc/p' &
                        cat $shm.1 | run_test  $rust_bufreader &
                        cat $shm.1 | run_test  python2 python2_test &
                        cat $shm.1 | run_test  python3 python3_test &
                        cat $shm.1 | run_test  awk '/^abc/' &
                        wait
                    ) | cat > /dev/null
                ) 2>&1 |
                sort -t, -k4,4n  # sort runs on $nloops for easier viewing
            }
        done
    done
} ) > compare_various_apis.rawdata.out.$random
