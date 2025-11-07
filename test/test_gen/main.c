#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define REQUIRED_ARGS                                                         \
    REQUIRED_LONG_ARG(n, "num-allocs", "Number of allocations")               \
    REQUIRED_LONG_ARG(min, "min-size", "Minimum allocation size")             \
    REQUIRED_LONG_ARG(max, "max-size", "Maximum allocation size")

#define OPTIONAL_ARGS                                                         \
    OPTIONAL_STRING_ARG(out, "", "--out", "filepath", "Output file")          \
    OPTIONAL_LONG_ARG(seq, 10L, "--seq", "longest-sequence",                  \
        "Max number of sequential allocs")

#include "../easyargs.h"

int main(int argc, char** argv)
{
    srand(time(NULL));
    args_t args = make_default_args();

    if (!parse_args(argc, argv, &args)) {
        print_help(argv[0]);
        return 1;
    }

    FILE* out_file = NULL;

    if (args.out && args.out[0]) {
        out_file = fopen(args.out, "w");
    } else {
        out_file = stdout;
    }

    if (!out_file) {
        fprintf(stderr, "Failed to open output file\n");
        return 1;
    }

    long range = args.max - args.min;
    long outstanding = args.n;

    while (outstanding > 0) {
        long sequence_len = (rand() % (args.seq + 1)) + 1;
        if (sequence_len > outstanding) {
            sequence_len = outstanding;
        }

        for (long i = 0; i < sequence_len; i++) {
            long size = (rand() % (range + 1)) + args.min;
            fprintf(out_file, "a %ld %ld\n", i, size);
        }

        for (long i = 0; i < sequence_len; i++) {
            fprintf(out_file, "f %ld\n", i);
        }

        outstanding -= sequence_len;
    }

    if (out_file != stdout) {
        fclose(out_file);
    }

    return 0;
}
