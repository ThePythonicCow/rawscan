#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char buf[64 * 1024];

int main()
{
    while (fgets(buf, sizeof(buf), stdin) == buf) {
	if (strncmp(buf, "abc", 3) == 0) {
	    fputs(buf, stdout);
	}
    }

    exit(0);
}
