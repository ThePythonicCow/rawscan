#!/bin/zsh
#

# Testing start up time for lots of small runs.  Elapsed time average
# for 10 repetitions was 12.56 secs per run for static, and 13.26
# secs per run for dynamic, making dynamic elapsed times about 5.6%
# slower.  The per run system times were about 2.6% slower for dynamic,
# and the per run user times were about 6.0% slower.

PATH=../build:.:$PATH

t=/tmp/rs.t.$$
trap 'rm $t.?; trap 0; exit' 0 1 2 3 15

random_line_generator -n 100 -m 10 -M 20 -B > $t.1

echo rawscan_static_test
(
    export TIMEFMT='rawscan_static_test %U %S %E'
    repeat 10 time zsh -c "repeat 10000 < $t.1 rawscan_static_test" | cat > /dev/null
) 2>&1 | awk '
    NR==1 {cmd = $1}
    {u += $2; s += $3; t += $4}
    END { print cmd, "user", u/NR, "sys", s/NR, "elapsed", t/NR}
'

echo rawscan_test
(
    export TIMEFMT='rawscan_test %U %S %E'
    repeat 10 time zsh -c "repeat 10000 < $t.1 rawscan_test" | cat > /dev/null
) 2>&1 | awk '
    NR==1 {cmd = $1}
    {u += $2; s += $3; t += $4}
    END { print cmd, "user", u/NR, "sys", s/NR, "elapsed", t/NR}
'
