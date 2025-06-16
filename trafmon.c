#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>
#include <math.h>

#include <sys/stat.h>
#include <sys/time.h>

#include "hgledon.h"

#define LOCK_FILE "/var/run/trafmon.lock"
#define IFACE_FILE "/var/run/trafmon.iface"

#define IDLE_TIMEOUT 1000
#define TRAFFIC_THRESHOLD 10
#define MIN_BLINK_DELAY 50
#define MAX_BLINK_DELAY 150
#define MAX_VAL 100
#define MAX_BUF 64
#define KB 1024

volatile int running = 1;
char interface_name[32];
char log_buf[256];

typedef enum {
    DIS_ON,
    DIS_OFF,
    ON_OFF,
    OFF_ON
} Pattern;

void stop_daemon(int) {
    running = 0;
}

void log_msg(const char *msg) {
    openlog("trafmon", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "%s", msg);
    closelog();
}

int check_iface(const char *iface) {
    DIR *dir = opendir("/sys/class/net");
    if (!dir) return 0;

    struct dirent *entry;
    int found = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, iface) == 0) {
            found = 1;
            break;
        }
    }
    closedir(dir);

    if (!found) return 0;

    char path[128];
    snprintf(path, sizeof(path), "/sys/class/net/%s/carrier", iface);

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    int carrier = 0;
    fscanf(f, "%d", &carrier);
    fclose(f);

    return carrier == 1;
}

void led(const char *led_type, const char *state) {
    static GPIO_PINS pins;
    static char kernel_version[MAX_BUF];
    static int initialized = 0;

    if (!initialized) {
        pins = init_gpio(kernel_version);
        initialized = 1;
    }

    hgl_exec(led_type, state, pins);
}

void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

int check_running() {
    FILE *file = fopen(LOCK_FILE, "r");
    if (file) {
        fclose(file);
        return 1;
    }
    return 0;
}

void create_lock_file() {
    FILE *file = fopen(LOCK_FILE, "w");
    if (file) {
        fprintf(file, "%d\n", getpid());
        fclose(file);
    }
    file = fopen(IFACE_FILE, "w");
    if (file) {
        fprintf(file, "%s\n", interface_name);
        fclose(file);
    }
}

void remove_lock_file() {
    remove(LOCK_FILE);
    remove(IFACE_FILE);
}

void redirect_stdio_to_null() {
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
}

void setup_signals() {
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGTERM, stop_daemon);
}

void wait_for_interface(const char *iface) {
    int wait_time = 10;
    int total_wait = 0;
    const int max_wait = 30;

    while (!check_iface(iface)) {
        snprintf(log_buf, sizeof(log_buf),
                "Interface %s not found, waiting %d seconds...", iface, wait_time);
        log_msg(log_buf);

        sleep(wait_time);
        total_wait += wait_time;
        wait_time = (wait_time + 10 > max_wait) ? max_wait : wait_time + 10;
    }

    if (total_wait > 0) {
        snprintf(log_buf, sizeof(log_buf),
                "Interface %s found after waiting %d seconds.", iface, total_wait);
        log_msg(log_buf);
    } else {
        snprintf(log_buf, sizeof(log_buf),
                "Interface %s found.", iface);
        log_msg(log_buf);
    }
}

int stop_process() {
    FILE *file = fopen(LOCK_FILE, "r");
    if (!file) {
        printf("Traffic monitor is not running.\n");
        return EXIT_FAILURE;
    }

    int pid;
    if (fscanf(file, "%d", &pid) != 1) {
        fclose(file);
        printf("Failed to read PID from lock file.\n");
        return EXIT_FAILURE;
    }
    fclose(file);

    if (kill(pid, SIGTERM) == 0) {
        printf("Stopping traffic monitor (PID: %d)...\n", pid);

        for (int i = 0; i < 10; i++) {
            if (kill(pid, 0) == -1) {
                remove(LOCK_FILE);
                led("-lan", "on");
                sleep_ms(100);
                led("-power", "on");
                log_msg("Trafmon stopped.");
                return EXIT_SUCCESS;
            }
            sleep_ms(500);
        }

        kill(pid, SIGKILL);
        remove(LOCK_FILE);
    } else {
        printf("Failed to send SIGTERM, process may not exist.\n");
        remove(LOCK_FILE);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int check_status() {
    FILE *file = fopen(LOCK_FILE, "r");
    if (!file) {
        printf("Traffic monitor is not running.\n");
        return EXIT_FAILURE;
    }

    int pid;
    if (fscanf(file, "%d", &pid) != 1) {
        fclose(file);
        printf("Failed to read PID from lock file.\n");
        return EXIT_FAILURE;
    }
    fclose(file);

    file = fopen(IFACE_FILE, "r");
    if (!file) {
        printf("Traffic monitor is running (PID: %d), but interface is unknown.\n", pid);
        return EXIT_SUCCESS;
    }

    char iface[32];
    if (fscanf(file, "%31s", iface) != 1) {
        fclose(file);
        printf("Traffic monitor is running (PID: %d), but failed to read interface.\n", pid);
        return EXIT_SUCCESS;
    }
    fclose(file);

    if (kill(pid, 0) == 0) {
        printf("Traffic monitor is running (PID: %d) monitoring interface: %s.\n", pid, iface);
        return EXIT_SUCCESS;
    } else {
        printf("Traffic monitor lock file exists but process not found. Cleaning up.\n");
        remove_lock_file();
        return EXIT_FAILURE;
    }
}

long get_traffic(const char *iface, const char *direction) {
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/%s_bytes", iface, direction);

    FILE *file = fopen(path, "r");
    if (!file) return 0;

    long bytes;
    fscanf(file, "%ld", &bytes);
    fclose(file);
    return bytes;
}

void blink_led(const char *led_name, Pattern pattern, int first_ms, int second_ms, int repeat) {
    for (int i = 0; i < repeat; i++) {
        switch (pattern) {
            case DIS_ON:
                led(led_name, "dis");
                sleep_ms(first_ms);
                led(led_name, "on");
                sleep_ms(second_ms);
                break;
            case DIS_OFF:
                led(led_name, "dis");
                sleep_ms(first_ms);
                led(led_name, "off");
                sleep_ms(second_ms);
                break;
            case ON_OFF:
                led(led_name, "on");
                sleep_ms(first_ms);
                led(led_name, "off");
                sleep_ms(second_ms);
                break;
            case OFF_ON:
                led(led_name, "off");
                sleep_ms(first_ms);
                led(led_name, "on");
                sleep_ms(second_ms);
                break;
            default:
                break;
        }
    }
}

int clamp(int val, int min, int max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

bool trx_hi(long rate) {
    return rate > TRAFFIC_THRESHOLD;
}

long current_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

void monitor_traffic() {
    long prev_rx = get_traffic(interface_name, "rx");
    long prev_tx = get_traffic(interface_name, "tx");

    long last_activity_time = current_time_ms();

    while (running) {
        long curr_rx = get_traffic(interface_name, "rx");
        long curr_tx = get_traffic(interface_name, "tx");

        long rx_rate = (curr_rx - prev_rx) / KB;
        long tx_rate = (curr_tx - prev_tx) / KB;
        long final_rate = rx_rate + tx_rate;

        int rate = clamp(MAX_VAL - log10(final_rate) * 15, MIN_BLINK_DELAY, MAX_BLINK_DELAY);
        long now = current_time_ms();
        int iface_status = check_iface(interface_name);

        #ifdef DEBUG
        snprintf(log_buf, sizeof(log_buf),
            "Traffic: RX: %ld KB/s, TX: %ld KB/s, Total: %ld KB/s, Blink delay: %d ms",
            rx_rate, tx_rate, final_rate, rate);
        log_msg(log_buf);
        #endif // DEBUG

        if (!iface_status) {
            blink_led("-lan", DIS_OFF, 100, 100, 1);
        } else if (trx_hi(final_rate)) {
            blink_led("-lan", DIS_ON, rate, rate, 1);
            last_activity_time = now;
        } else {
            if (now - last_activity_time > IDLE_TIMEOUT) {
                led("-lan", "on");
            } else {
                blink_led("-lan", OFF_ON, 100, 100, 1);
            }
        }

        prev_rx = curr_rx;
        prev_tx = curr_tx;

        if (!running) break;

        sleep_ms(MAX_VAL);
    }
}

void daemonize() {
    if (fork() > 0) exit(EXIT_SUCCESS);
    if (setsid() < 0) exit(EXIT_FAILURE);

    setup_signals();

    if (fork() > 0) exit(EXIT_SUCCESS);

    umask(0);
    chdir("/");

    redirect_stdio_to_null();
    create_lock_file();

    log_msg("Daemon started.");

    wait_for_interface(interface_name);

    snprintf(log_buf, sizeof(log_buf),
            "Starting traffic monitor for interface %s...", interface_name);
    log_msg(log_buf);
}

int main(int argc, char *argv[]) {
    const char *prog = argv[0];
    if (argc == 2 && strcmp(argv[1], "help") == 0) {
        printf("\n████████ ██████   █████  ███████ ███    ███  ██████  ███    ██ \n");
        printf("   ██    ██   ██ ██   ██ ██      ████  ████ ██    ██ ████   ██ \n");
        printf("   ██    ██████  ███████ █████   ██ ████ ██ ██    ██ ██ ██  ██ \n");
        printf("   ██    ██   ██ ██   ██ ██      ██  ██  ██ ██    ██ ██  ██ ██ \n");
        printf("   ██    ██   ██ ██   ██ ██      ██      ██  ██████  ██   ████ \n");
        printf("\nLED Traffic Monitor Daemon\n\n");
        printf("Usage:\n");
        printf("  %s start <interface> - Start monitoring traffic on the specified interface.\n", prog);
        printf("  %s stop              - Stop the traffic monitor.\n", prog);
        printf("  %s status            - Check if the traffic monitor is running.\n", prog);
        printf("  %s help              - Show this help message and exit.\n", prog);
        printf("\n");
        printf("The traffic monitor will blink the LED when traffic is detected.\n");
        printf("The LED will blink faster for higher traffic rates.\n");
        printf("\nCopyright (C) 2025 Najahi. All rights reserved.\n");
        return EXIT_SUCCESS;
    }

    if (argc == 2 && strcmp(argv[1], "stop") == 0) {
        return stop_process();
    }

    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        return check_status();
    }

    if (argc == 3 && strcmp(argv[1], "start") == 0) {
        strncpy(interface_name, argv[2], sizeof(interface_name) - 1);
        interface_name[sizeof(interface_name) - 1] = '\0';

        if (check_running()) {
            printf("Traffic monitor already running!\n");
            return EXIT_FAILURE;
        }

        printf("Starting traffic monitor for interface %s...\n", interface_name);

        daemonize();
        monitor_traffic();
        remove_lock_file();

        return EXIT_SUCCESS;
    }

    fprintf(stderr, "Invalid arguments. Use '%s help' for usage information.\n", prog);
}
