#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main()
{
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;

    while ((nread = getline(&line, &len, stdin)) != -1) {
	if (strncmp(line, "abc", 3) == 0) {
	    fwrite(line, nread, 1, stdout);
	}
    }

    free(line);
    exit(0);
}
