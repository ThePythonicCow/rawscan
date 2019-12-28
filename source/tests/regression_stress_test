#!/bin/zsh
#
# Input a random variety of lines to both a rawscan reader and to
# the "get abc" command, with the rawscan reader coded to duplicate
# the behavior of that get command.
#
# Test various inputs, varying by:
#  1) number of lines in input,
#  2) whether the last line has a trailing newline,
#  3) the length of the lines, and
#  4) the size of the rawscan buffer used.
#
# Test passes only if the rawscan reader output matches the
# "sed -n /^abc/p" output, for all tests.
#
# Focus on smaller inputs, with fewer lines (down to zero), shorter
# lines (down to zero length), and smaller rawscan buffers, as
# the tricky code, hence the bug risk, is mostly at the edge and
# transition cases.  However be sure to also test some cases with
# input line sizes larger than the chosen rawscan buffer size.

# Multios: Enable zsh's multi-pipe into parallel ">>(...)" processes
# below.  Use some zsh file multios redirection and replication magic
# in order to feed copies of each random_line_generator invocation
# into both test cases.

setopt multios

PATH=.:$PATH
random=$RANDOM
progress="not started yet"

shm=/dev/shm/rawscan_stress_test_data.$RANDOM.$$
trap 'rm -f $shm.?; trap 0; exit 0' 0 1 2 3 15

echo Beginning: $(date)

for nlines in $(seq 0 20)
do
    for minlen in $(seq 0 80)
    do
        for deltalen in $(seq 0 10)
        do
            maxlen=$((minlen + deltalen))
            progress="nlines minlen maxlen $nlines $minlen $maxlen"
            echo -n 1>&2 "$progress" '     \r'

            for finaleol in "" "-T"
            do
                random_line_generator -n $nlines -m $minlen -M $maxlen \
                    -S $finaleol > $shm.1

                 # rawscan_static_test is coded to look for "^abc" in
                 # the first chunk in a line, so we must configure rawscan
                 # to use at least a 3 char buffer, which for our log base 2
                 # sizes, means a 4 char (equal to 2**2) "-b" buffer.  So
                 # following "seq" starts at "2", not "0".
                 # We should write a fancier rawscan_static_test that can
                 # match "^abc" in multiple chunks in a 1 or 2 char buffer.

                # We use "sed -n /^abc/p", not "grep '^abc'", because grep,
                # but not sed, tacks on a gratuitous trailing newline if the
                # final input line matches, but lacks such a newline.  The
                # "sed" command matches what "rawscan_static_test" does.

                for rawscan_buf_sz_log2 in $(seq 2 6)
                do
                    bufsz=$((2**rawscan_buf_sz_log2))

                    ( ( { cat $shm.1 } \
                        > >(rawscan_static_test -b $bufsz | md5sum 1>&3 ) \
                        > >(sed -n /^abc/p | md5sum 1>&3 )
                    ) 1>/dev/null ) 3>&1 |
                    uniq -c |
                    while read cnt sum input
                    do
                        if test $cnt -ne 2
                        then
                            echo '\n'FAILED: '                       '
                            echo '  ' ./random_line_generator -n $nlines \
                              -m $minlen -M $maxlen -S $finaleol '|' \
                              ./rawscan_static_test -b $bufsz
                            exit 1
                        fi
                    done
                done
            done
        done
    done
done

echo 1>&2 Accomplished: "$progress"
echo 1>&2 Finished: $(date)