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
char lock_file_path[64];
char iface_file_path[64];
char led_name[16] = "lan";

typedef enum {
    DIS_ON,
    DIS_OFF,
    ON_OFF,
    OFF_ON
} Pattern;

void set_file_paths(const char *iface) {
    snprintf(lock_file_path, sizeof(lock_file_path), "/var/run/trafmon_%s.lock", iface);
    snprintf(iface_file_path, sizeof(iface_file_path), "/var/run/trafmon_%s.iface", iface);
}

bool parse_iface(const char *filename, char *iface, size_t size) {
    const char *prefix = "trafmon_";
    const char *suffix = ".lock";

    size_t len = strlen(filename);
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);

    if (len <= prefix_len + suffix_len) return false;
    if (strncmp(filename, prefix, prefix_len) != 0) return false;
    if (strcmp(filename + len - suffix_len, suffix) != 0) return false;

    size_t iface_len = len - prefix_len - suffix_len;
    if (iface_len >= size) return false;

    memcpy(iface, filename + prefix_len, iface_len);
    iface[iface_len] = '\0';
    return true;
}

int list_instances() {
    DIR *d = opendir("/var/run");
    if (!d) {
        perror("Failed to open /var/run");
        return EXIT_FAILURE;
    }

    struct dirent *entry;
    int found = 0;

    while ((entry = readdir(d)) != NULL) {
        char iface[32];
        if (parse_iface(entry->d_name, iface, sizeof(iface))) {
            if (!found) {
                printf("Running trafmon instances:\n");
                found = 1;
            }
            printf(" - %s\n", iface);
        }
    }
    closedir(d);

    if (!found) {
        printf("No running trafmon instances found.\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

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
    FILE *file = fopen(lock_file_path, "r");
    if (!file) return 0;

    int pid;
    if (fscanf(file, "%d", &pid) != 1) {
        fclose(file);
        return 0;
    }
    fclose(file);

    return kill(pid, 0) == 0;
}

void create_lock_file() {
    FILE *file = fopen(lock_file_path, "w");
    if (file) {
        fprintf(file, "%d\n", getpid());
        fclose(file);
    }
    file = fopen(iface_file_path, "w");
    if (file) {
        fprintf(file, "%s %s\n", interface_name, led_name);
        fclose(file);
    }
}

void remove_lock_file() {
    remove(lock_file_path);
    remove(iface_file_path);
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

void select_led_for_instance() {
    int lan_in_use = 0;
    int power_in_use = 0;

    DIR *d = opendir("/var/run");
    struct dirent *entry;

    if (d) {
        while ((entry = readdir(d)) != NULL) {
            char iface[32];
            if (parse_iface(entry->d_name, iface, sizeof(iface))) {
                char led_path[64];
                snprintf(led_path, sizeof(led_path), "/var/run/trafmon_%s.iface", iface);

                FILE *f = fopen(led_path, "r");
                if (f) {
                    char iface[32], used_led[16];
                    if (fscanf(f, "%31s %15s", iface, used_led) == 2) {
                        if (strcmp(used_led, "lan") == 0) lan_in_use = 1;
                        if (strcmp(used_led, "power") == 0) power_in_use = 1;
                    }
                    fclose(f);
                }
            }
        }
        closedir(d);
    }

    if (!lan_in_use) {
        strncpy(led_name, "lan", sizeof(led_name));
    } else if (!power_in_use) {
        strncpy(led_name, "power", sizeof(led_name));
    } else {
        fprintf(stderr, "Maximum 2 trafmon instances supported (lan, power).\n");
        exit(EXIT_FAILURE);
    }
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

int stop_process(const char *iface) {
    set_file_paths(iface);

    FILE *file = fopen(lock_file_path, "r");
    if (!file) {
        printf("Traffic monitor for %s is not running.\n", iface);
        return EXIT_FAILURE;
    }

    int pid;
    if (fscanf(file, "%d", &pid) != 1) {
        fclose(file);
        printf("Failed to read PID from lock file for %s.\n", iface);
        return EXIT_FAILURE;
    }
    fclose(file);

    if (kill(pid, SIGTERM) == 0) {
        printf("Stopping traffic monitor for %s (PID: %d)...\n", iface, pid);

        for (int i = 0; i < 10; i++) {
            if (kill(pid, 0) == -1) {
                remove_lock_file();
                sleep_ms(100);
                led(led_name, "on");
                log_msg("Trafmon stopped.");
                return EXIT_SUCCESS;
            }
            sleep_ms(500);
        }

        kill(pid, SIGKILL);
        remove_lock_file();
    } else {
        printf("Failed to send SIGTERM to %d, process may not exist.\n", pid);
        remove_lock_file();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int check_status(const char *iface) {
    set_file_paths(iface);

    FILE *file = fopen(lock_file_path, "r");
    if (!file) {
        printf("Traffic monitor for %s is not running.\n", iface);
        return EXIT_FAILURE;
    }

    int pid;
    if (fscanf(file, "%d", &pid) != 1) {
        fclose(file);
        printf("Failed to read PID from lock file for %s.\n", iface);
        return EXIT_FAILURE;
    }
    fclose(file);

    file = fopen(iface_file_path, "r");
    if (!file) {
        printf("Traffic monitor is running (PID: %d), but LED/interface is unknown.\n", pid);
        return EXIT_SUCCESS;
    }

    char real_iface[32] = {0};
    char led_used[16] = {0};

    int n = fscanf(file, "%31s %15s", real_iface, led_used);
    fclose(file);

    if (n == 2) {
        if (kill(pid, 0) == 0) {
            printf("Traffic monitor is running (PID: %d), interface: %s, LED: %s\n", pid, real_iface, led_used);
            return EXIT_SUCCESS;
        }
    } else if (n == 1) {
        if (kill(pid, 0) == 0) {
            printf("Traffic monitor is running (PID: %d), interface: %s, LED: unknown\n", pid, real_iface);
            return EXIT_SUCCESS;
        }
    }

    printf("Lock file exists for %s but process not found. Cleaning up.\n", iface);
    remove_lock_file();
    return EXIT_FAILURE;
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

        int rate = clamp(MAX_VAL - log10(final_rate) * 10, MIN_BLINK_DELAY, MAX_BLINK_DELAY);
        long now = current_time_ms();
        int iface_status = check_iface(interface_name);

        #ifdef DEBUG
        snprintf(log_buf, sizeof(log_buf),
            "Traffic: RX: %ld KB/s, TX: %ld KB/s, Total: %ld KB/s, Blink delay: %d ms",
            rx_rate, tx_rate, final_rate, rate);
        log_msg(log_buf);
        #endif // DEBUG

        if (!iface_status) {
            blink_led(led_name, DIS_OFF, 100, 100, 1);
        } else if (trx_hi(final_rate)) {
            blink_led(led_name, DIS_ON, rate, rate, 1);
            last_activity_time = now;
        } else {
            if (now - last_activity_time > IDLE_TIMEOUT) {
                led(led_name, "on");
            } else {
                blink_led(led_name, OFF_ON, 100, 100, 1);
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
        printf("\n");
        printf("████████ ██████   █████  ███████ ███    ███  ██████  ███    ██ \n");
        printf("   ██    ██   ██ ██   ██ ██      ████  ████ ██    ██ ████   ██ \n");
        printf("   ██    ██████  ███████ █████   ██ ████ ██ ██    ██ ██ ██  ██ \n");
        printf("   ██    ██   ██ ██   ██ ██      ██  ██  ██ ██    ██ ██  ██ ██ \n");
        printf("   ██    ██   ██ ██   ██ ██      ██      ██  ██████  ██   ████ \n");
        printf("\n");
        printf("LED Traffic Monitor Daemon\n\n");
        printf("Usage:\n");
        printf("  %s start <interface>   - Start monitoring traffic on the specified interface.\n", prog);
        printf("  %s stop <interface>    - Stop monitoring the specified interface.\n", prog);
        printf("  %s stop                - Stop all running traffic monitor instances.\n", prog);
        printf("  %s status              - Show status of all running instances.\n", prog);
        printf("  %s list                - List all running trafmon instances.\n", prog);
        printf("  %s help                - Show this help message and exit.\n\n", prog);
        printf("The traffic monitor will blink the LED when traffic is detected.\n");
        printf("The LED will blink faster for higher traffic rates.\n\n");
        printf("Copyright (C) 2025 Najahi. All rights reserved.\n");
        return EXIT_SUCCESS;
    }

    if (argc >= 2 && strcmp(argv[1], "stop") == 0) {
        if (argc == 3) {
            strncpy(interface_name, argv[2], sizeof(interface_name) - 1);
            interface_name[sizeof(interface_name) - 1] = '\0';
            return stop_process(interface_name);
        }

        if (argc == 2) {
            printf("Stopping all running trafmon instances...\n");

            DIR *d = opendir("/var/run");
            if (!d) {
                perror("Failed to open /var/run");
                return EXIT_FAILURE;
            }

            struct dirent *entry;
            int found = 0;

            while ((entry = readdir(d)) != NULL) {
                char iface[32];
                if (parse_iface(entry->d_name, iface, sizeof(iface))) {
                    found = 1;
                    printf(" → Stopping instance on interface: %s\n", iface);
                    stop_process(iface);
                }
            }
            closedir(d);

            if (!found) {
                printf("No running trafmon instances found.\n");
                return EXIT_FAILURE;
            }

            return EXIT_SUCCESS;
        }

        fprintf(stderr, "Invalid usage. Use: %s stop [<interface>] or stop for stop all instances\n", prog);
        return EXIT_FAILURE;
    }

    if (argc >= 2 && strcmp(argv[1], "status") == 0) {
        if (argc == 3) {
            strncpy(interface_name, argv[2], sizeof(interface_name) - 1);
            interface_name[sizeof(interface_name) - 1] = '\0';
            return check_status(interface_name);
        }

        if (argc == 2) {
            int found = 0;
            DIR *d = opendir("/var/run");
            if (!d) {
                perror("Failed to open /var/run");
                return EXIT_FAILURE;
            }

            struct dirent *entry;
            while ((entry = readdir(d)) != NULL) {
                char iface[32];
                if (parse_iface(entry->d_name, iface, sizeof(iface))) {
                    strncpy(interface_name, iface, sizeof(interface_name) - 1);
                    interface_name[sizeof(interface_name) - 1] = '\0';
                    check_status(interface_name);
                    found = 1;
                }
            }
            closedir(d);

            if (!found) {
                printf("No running trafmon instances found.\n");
                return EXIT_FAILURE;
            }

            return EXIT_SUCCESS;
        }

        fprintf(stderr, "Invalid usage. Use: %s status [<interface>]\n", prog);
        return EXIT_FAILURE;
    }

    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        return list_instances();
    }

    if (argc == 3 && strcmp(argv[1], "start") == 0) {
        strncpy(interface_name, argv[2], sizeof(interface_name) - 1);
        interface_name[sizeof(interface_name) - 1] = '\0';

        set_file_paths(interface_name);

        select_led_for_instance();

        if (check_running()) {
            printf("Traffic monitor already running!\n");
            return EXIT_FAILURE;
        }

        snprintf(log_buf, sizeof(log_buf),
                "Starting traffic monitor for interface %s with led %s...\n", interface_name, led_name);
        log_msg(log_buf);

        printf("Starting traffic monitor for interface %s with led %s...\n", interface_name, led_name);

        daemonize();
        monitor_traffic();
        remove_lock_file();

        return EXIT_SUCCESS;
    }

    fprintf(stderr, "Invalid arguments. Use '%s help' for usage information.\n", prog);
}
