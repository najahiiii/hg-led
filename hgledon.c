#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hgledon.h"

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

    FILE *f = fopen("/sys/class/gpio/export", "w");
    if (!f) exit(1);
    fprintf(f, "%d", pin);
    fclose(f);
}

void set_gpio_direction(int pin, const char *direction) {
    char path[MAX_BUF];
    snprintf(path, MAX_BUF, "/sys/class/gpio/gpio%d/direction", pin);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s", direction);
        fclose(f);
    }
}

void set_gpio_value(int pin, int value) {
    char path[MAX_BUF];
    snprintf(path, MAX_BUF, "/sys/class/gpio/gpio%d/value", pin);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%d", value);
        fclose(f);
    }
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

    if (strcmp(act, "on") == 0) {
        set_gpio_value(pin_on, 1);
        set_gpio_value(pin_off, 0);
    } else if (strcmp(act, "off") == 0) {
        set_gpio_value(pin_on, 0);
        set_gpio_value(pin_off, 1);
    } else if (strcmp(act, "warn") == 0) {
        set_gpio_value(pin_on, 1);
        set_gpio_value(pin_off, 1);
    } else {
        set_gpio_value(pin_on, 0);
        set_gpio_value(pin_off, 0);
    }
}

void ir_control(const char *ir_action, int pin_ir) {
    if (!(strcmp(ir_action, "on") == 0 || strcmp(ir_action, "dis") == 0 ||
          strcmp(ir_action, "reset") == 0)) {
        fprintf(stderr, "Invalid IR action: %s\n", ir_action);
        exit(1);
    }

    export_gpio(pin_ir);
    set_gpio_direction(pin_ir, "out");

    if (strcmp(ir_action, "on") == 0) {
        set_gpio_value(pin_ir, 1);
    } else if (strcmp(ir_action, "dis") == 0) {
        set_gpio_value(pin_ir, 0);
    } else {
        set_gpio_value(pin_ir, 1);
        usleep(100000);
        set_gpio_value(pin_ir, 0);
    }
}


void exec(const char *command, const char *action, GPIO_PINS pins) {
    if (strcmp(command, "-power") == 0) {
        lp_control(action, pins.power[0], pins.power[1]);
    } else if (strcmp(command, "-lan") == 0) {
        lp_control(action, pins.lan[0], pins.lan[1]);
    } else if (strcmp(command, "-ir") == 0) {
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
    printf("Usage:\n  hgledon -power [on, off, warn, dis]\n");
    printf("  hgledon -lan [on, off, warn, dis]\n");
    printf("  hgledon -ir [on, dis, reset]\n");
    printf("  hgledon -help (to show this message)\n");
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
