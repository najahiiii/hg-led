#ifndef HGLEDON_H
#define HGLEDON_H

#define MAX_BUF 64

typedef struct {
    int power[2];
    int lan[2];
    int ir;
} GPIO_PINS;

GPIO_PINS get_pins(int major, int minor);
void export_gpio(int pin);
void set_gpio_direction(int pin, const char *direction);
void set_gpio_value(int pin, int value);
void lp_control(const char *act, int pin_on, int pin_off);
void ir_control(const char *ir_action, int pin_ir);
void hgl_exec(const char *command, const char *action, GPIO_PINS pins);
GPIO_PINS init_gpio();
void usage(GPIO_PINS pins, const char *kernel_version);

#endif
