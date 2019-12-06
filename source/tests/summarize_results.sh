#!/bin/zsh

awk '
    /Line lengths/ { minlen = $4; maxlen = $6 }
    /user/ { printf "%d %d %s %.2f\n", minlen, maxlen, $1, $2 }
' | sort -k1,1n -k3 |
    python3 -c "$( <<- '@@EOF@@'
	import sys
	import statistics as stat

	results = {}
	lastone = None

	for ln in sys.stdin:
	    flds = ln.split()
	    name = (flds[2], int(flds[0]), int(flds[1]))
	    val = float(flds[3])
	    if not name in results:
	        results[name] = []
	    results[name].append(val)
	
	for nm in results:
	    if (lastone != None and nm[1] != lastone):
	        print("")
	    lastone = nm[1]
	    print("%32s:   %0.3f +/- %0.3f" % (nm, stat.mean(results[nm]), stat.stdev(results[nm])))
	@@EOF@@
    )"
