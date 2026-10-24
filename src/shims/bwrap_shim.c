#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    FILE *f = fopen("/tmp/bwrap.log", "a");
    if (f) {
        fprintf(f, "BWRAP INVOCATION: ");
        for (int j = 0; j < argc; j++) fprintf(f, "'%s' ", argv[j]);
        fprintf(f, "\n");
    }

    int i = 1;
    while (i < argc) {
        /* Check if this is an option flag */
        if (argv[i][0] == '-') {
            /* 3-token options (option + 2 args) */
            if (strcmp(argv[i], "--ro-bind") == 0 ||
                strcmp(argv[i], "--bind") == 0 ||
                strcmp(argv[i], "--dev-bind") == 0 ||
                strcmp(argv[i], "--ro-bind-try") == 0 ||
                strcmp(argv[i], "--bind-try") == 0 ||
                strcmp(argv[i], "--dev-bind-try") == 0 ||
                strcmp(argv[i], "--symlink") == 0 ||
                strcmp(argv[i], "--setenv") == 0) {
                i += 3;
            }
            /* 2-token options (option + 1 arg) */
            else if (strcmp(argv[i], "--chdir") == 0 ||
                     strcmp(argv[i], "--unsetenv") == 0 ||
                     strcmp(argv[i], "--seccomp") == 0 ||
                     strcmp(argv[i], "--tmpfs") == 0 ||
                     strcmp(argv[i], "--dir") == 0 ||
                     strcmp(argv[i], "--dev") == 0 ||
                     strcmp(argv[i], "--proc") == 0 ||
                     strcmp(argv[i], "--file") == 0 ||
                     strcmp(argv[i], "--bind-data") == 0 ||
                     strcmp(argv[i], "--ro-bind-data") == 0) {
                i += 2;
            }
            /* 1-token options (option with 0 args) */
            else {
                i += 1;
            }
        } else {
            /* First non-flag argument is the target command */
            if (f) {
                fprintf(f, "EXECUTING TARGET: '%s'\n", argv[i]);
                fclose(f);
            }
            execvp(argv[i], &argv[i]);
            perror("execvp target");
            return 1;
        }
    }

    if (f) {
        fprintf(f, "NO TARGET COMMAND FOUND\n");
        fclose(f);
    }
    return 0;
}
