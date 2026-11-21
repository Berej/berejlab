#include <stdio.h>
#include <string.h>

#include "common/version.h"

static void print_help(FILE *out) {
    fputs("usage: berejlab [options]\n"
          "       berejlab /command [args...]\n"
          "\n"
          "berejlab client: interactive terminal UI for the berejlabd daemon.\n"
          "\n"
          "Options:\n"
          "  -h, --help          show this help and exit\n"
          "      --version       show the version and exit\n"
          "\n"
          "Planned (not implemented yet):\n"
          "      --socket PATH   daemon socket (default /run/berejlab/berejlab.sock,\n"
          "                      or $BEREJLAB_SOCKET)\n"
          "  /command [args...]  run one command, print the reply and exit\n",
          out);
}

static int run(int argc, char **argv) {
    if (argc < 2) {
        fputs("berejlab: the interactive UI is not implemented yet (stage 5); see --help\n",
              stderr);
        return 1;
    }
    const char *arg = argv[1];
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
        print_help(stdout);
        return 0;
    }
    if (strcmp(arg, "--version") == 0) {
        printf("berejlab %s\n", BEREJLAB_VERSION);
        return 0;
    }
    if (strcmp(arg, "--socket") == 0) {
        fputs("berejlab: option --socket is not implemented yet (stage 5)\n", stderr);
        return 1;
    }
    if (arg[0] == '/') {
        fputs("berejlab: one-shot commands are not implemented yet (stage 5)\n", stderr);
        return 1;
    }
    fprintf(stderr, "berejlab: unknown option '%s'; see --help\n", arg);
    return 2;
}

int main(int argc, char **argv) {
    int rc = run(argc, argv);
    // A failed write to stdout (closed pipe, full disk) must not look like success.
    if (fflush(stdout) != 0 || ferror(stdout)) {
        perror("berejlab: stdout");
        return 1;
    }
    return rc;
}
