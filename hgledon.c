#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "hgledon.h"

#define MAX_BUF 64

GPIO_PINS get_pins(int major, int minor) {
    GPIO_PINS pins;
    if (major >= 6) {
        pins = (GPIO_PINS){{547, 548}, {521, 517}, 580};
    } else if (major == 5 && minor == 15) {
        pins = (GPIO_PINS){{425, 426}, {510, 506}, 507};
    } else {
        fprintf(stderr, "Unsupported kernel version\n");
        exit(1);
    }
    return pins;
}

void export_gpio(int pin) {
    char path[MAX_BUF];
    snprintf(path, MAX_BUF, "/sys/class/gpio/gpio%d/value", pin);
    if (access(path, F_OK) == 0) return;

    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) {
        perror("Failed to export GPIO");
        exit(1);
    }
    dprintf(fd, "%d", pin);
    close(fd);
}

void set_gpio_direction(int pin, const char *direction) {
    char path[MAX_BUF];
    snprintf(path, MAX_BUF, "/sys/class/gpio/gpio%d/direction", pin);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("Failed to set GPIO direction");
        return;
    }
    dprintf(fd, "%s", direction);
    close(fd);
}

void set_gpio_value(int pin, int value) {
    char path[MAX_BUF];
    snprintf(path, MAX_BUF, "/sys/class/gpio/gpio%d/value", pin);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("Failed to set GPIO value");
        return;
    }
    dprintf(fd, "%d", value);
    close(fd);
}

void lp_control(const char *act, int pin_on, int pin_off) {
    if (!(strcmp(act, "on") == 0 || strcmp(act, "off") == 0 ||
          strcmp(act, "warn") == 0 || strcmp(act, "dis") == 0)) {
        fprintf(stderr, "Invalid action: %s\n", act);
        exit(1);
    }

    export_gpio(pin_on);
    export_gpio(pin_off);
    set_gpio_direction(pin_on, "out");
    set_gpio_direction(pin_off, "out");

    int values[4][2] = {
        {1, 0}, // on
        {0, 1}, // off
        {1, 1}, // warn
        {0, 0}  // dis
    };

    int index;
    if (strcmp(act, "on") == 0) index = 0;
    else if (strcmp(act, "off") == 0) index = 1;
    else if (strcmp(act, "warn") == 0) index = 2;
    else index = 3; // "dis"

    set_gpio_value(pin_on, values[index][0]);
    set_gpio_value(pin_off, values[index][1]);
}

void ir_control(const char *ir_action, int pin_ir) {
    if (!(strcmp(ir_action, "on") == 0 || strcmp(ir_action, "dis") == 0 ||
          strcmp(ir_action, "reset") == 0)) {
        fprintf(stderr, "Invalid IR action: %s\n", ir_action);
        exit(1);
    }

    export_gpio(pin_ir);
    set_gpio_direction(pin_ir, "out");

    set_gpio_value(pin_ir, strcmp(ir_action, "dis") != 0);

    if (strcmp(ir_action, "reset") == 0) {
        usleep(100000);
        set_gpio_value(pin_ir, 0);
    }
}

void hgl_exec(const char *command, const char *action, GPIO_PINS pins) {
    if (strcmp(command, "power") == 0) {
        lp_control(action, pins.power[0], pins.power[1]);
    } else if (strcmp(command, "lan") == 0) {
        lp_control(action, pins.lan[0], pins.lan[1]);
    } else if (strcmp(command, "ir") == 0) {
        ir_control(action, pins.ir);
    } else {
        fprintf(stderr, "Error: Invalid command '%s'\n", command);
        exit(1);
    }
}

void usage(GPIO_PINS pins, const char *kernel_version) {
    printf("Kernel Version: %s\n", kernel_version);
    printf("Using GPIO Pins:\n  Power: %d, %d\n  LAN: %d, %d\n  IR: %d\n\n",
           pins.power[0], pins.power[1], pins.lan[0], pins.lan[1], pins.ir);
    printf("Usage:\n  hgledon power [on, off, warn, dis]\n");
    printf("  hgledon lan [on, off, warn, dis]\n");
    printf("  hgledon ir [on, dis, reset]\n");
    printf("  hgledon help (to show this message)\n");
}

GPIO_PINS init_gpio(char *kernel_version) {
    int major = 0, minor = 0;

    FILE *f = fopen("/proc/sys/kernel/osrelease", "r");
    if (!f) {
        perror("Failed to open /proc/sys/kernel/osrelease");
        exit(1);
    }

    if (!fgets(kernel_version, MAX_BUF, f)) {
        fclose(f);
        exit(1);
    }
    fclose(f);

    if (sscanf(kernel_version, "%d.%d", &major, &minor) != 2) {
        fprintf(stderr, "Error parsing kernel version: %s\n", kernel_version);
        exit(1);
    }

    return get_pins(major, minor);
}

#ifdef HGLEDON_MAIN
int main(int argc, char *argv[]) {
    char kernel_version[MAX_BUF];

    if (geteuid() != 0) {
        fprintf(stderr, "This script must be run as root\n");
        return 1;
    }

    GPIO_PINS pins = init_gpio(kernel_version);

    if (argc == 2 && strcmp(argv[1], "help") == 0) {
        usage(pins, kernel_version);
        return 0;
    }

    if (argc != 3) {
        fprintf(stderr, "Error: Incorrect number of arguments\n");
        fprintf(stderr, "Use help for more info\n");
        return 1;
    }

    hgl_exec(argv[1], argv[2], pins);
    return 0;
}
#endif // HGLEDON_MAIN
