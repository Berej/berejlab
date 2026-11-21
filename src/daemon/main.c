#include <stdio.h>
#include <string.h>

#include "common/version.h"

static void print_help(FILE *out) {
    fputs("usage: berejlabd [options]\n"
          "\n"
          "berejlab daemon: enumerates SHA-256 hashes and saves the ones that match targets.\n"
          "\n"
          "Options:\n"
          "  -h, --help          show this help and exit\n"
          "      --version       show the version and exit\n"
          "\n"
          "Planned options (not implemented yet):\n"
          "      --foreground    stay in the foreground, log to stderr\n"
          "      --state-dir DIR state directory (default /var/lib/berejlab)\n"
          "      --socket PATH   control socket (default /run/berejlab/berejlab.sock)\n"
          "      --config FILE   configuration file\n",
          out);
}

static int is_planned_option(const char *arg) {
    static const char *const planned[] = {"--foreground", "--state-dir", "--socket", "--config"};
    for (size_t i = 0; i < sizeof planned / sizeof planned[0]; i++) {
        if (strcmp(arg, planned[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int run(int argc, char **argv) {
    if (argc < 2) {
        fputs("berejlabd: the daemon is not implemented yet (stage 4); see --help\n", stderr);
        return 1;
    }
    const char *arg = argv[1];
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
        print_help(stdout);
        return 0;
    }
    if (strcmp(arg, "--version") == 0) {
        printf("berejlabd %s\n", BEREJLAB_VERSION);
        return 0;
    }
    if (is_planned_option(arg)) {
        fprintf(stderr, "berejlabd: option %s is not implemented yet (stage 4)\n", arg);
        return 1;
    }
    fprintf(stderr, "berejlabd: unknown option '%s'; see --help\n", arg);
    return 2;
}

int main(int argc, char **argv) {
    int rc = run(argc, argv);
    // A failed write to stdout (closed pipe, full disk) must not look like success.
    if (fflush(stdout) != 0 || ferror(stdout)) {
        perror("berejlabd: stdout");
        return 1;
    }
    return rc;
}
