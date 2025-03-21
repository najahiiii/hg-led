#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hgledon.h"

int main(int argc, char *argv[]) {
    if (geteuid() != 0) {
        fprintf(stderr, "This script must be run as root\n");
        return 1;
    }

    GPIO_PINS pins = init_gpio();

    if (argc == 2 && strcmp(argv[1], "-help") == 0) {
        usage(pins);
        return 0;
    }

    if (argc != 3) {
        fprintf(stderr, "Error: Incorrect number of arguments\n");
        fprintf(stderr, "Use -help for more info\n");
        return 1;
    }

    exec(argv[1], argv[2], pins);
    return 0;
}
