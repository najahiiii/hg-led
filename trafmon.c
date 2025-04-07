#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>
#include <math.h>

#include <sys/stat.h>

#include "hgledon.h"

#define LOCK_FILE "/var/run/trafmon.lock"
#define IFACE_FILE "/var/run/trafmon.iface"

#define TRAFFIC_THRESHOLD 150
#define MAX_VAL 150

#define DEBUG 0

volatile int running = 1;
char interface_name[32];
char log_buf[256];

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
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, iface) == 0) {
            closedir(dir);
            return 1;
        }
    }
    closedir(dir);
    return 0;
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

void monitor_traffic() {
    long p_rx = get_traffic(interface_name, "rx");
    long p_tx = get_traffic(interface_name, "tx");

    led("-lan", "on");

    long last_final_rate = -1;

    while (running) {
        long c_rx_tx[2];
        c_rx_tx[0] = get_traffic(interface_name, "rx");
        c_rx_tx[1] = get_traffic(interface_name, "tx");

        long rx_rate = (c_rx_tx[0] - p_rx) / 1024;
        long tx_rate = (c_rx_tx[1] - p_tx) / 1024;
        long final_rate = rx_rate + tx_rate;

        int s_rate = MAX_VAL - log10(final_rate + 1) * 10;

        if (DEBUG) {
            snprintf(log_buf, sizeof(log_buf),
            "Traffic detected: RX: %ld KB/s, TX: %ld KB/s, Rate: %ld KB/s, Blink rate: %d ms",
            rx_rate, tx_rate, final_rate, s_rate);
            log_msg(log_buf);
        }

        if (labs(final_rate - last_final_rate) > TRAFFIC_THRESHOLD) {
            led("-lan", "dis");
            sleep_ms(s_rate);
            led("-lan", "on");
            last_final_rate = final_rate;
        }

        p_rx = c_rx_tx[0];
        p_tx = c_rx_tx[1];

        if (!running) break;

        sleep_ms(MAX_VAL);
    }
}

void daemonize() {
    if (fork() > 0) exit(EXIT_SUCCESS);
    if (setsid() < 0) exit(EXIT_FAILURE);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    
    if (fork() > 0) exit(EXIT_SUCCESS);

    umask(0);
    chdir("/");

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

    create_lock_file();
    signal(SIGTERM, stop_daemon);

    snprintf(log_buf, sizeof(log_buf), "Starting traffic monitor for interface %s...", interface_name);
    log_msg(log_buf);

    int wait_time = 2;
    int total_wait = 0;
    const int max_wait = 30;
    const int max_total_wait = 240;

    while (!check_iface(interface_name) && total_wait < max_total_wait) {
        snprintf(log_buf, sizeof(log_buf),
                "Interface %s not found, waiting %d seconds...", interface_name, wait_time);
        log_msg(log_buf);

        sleep(wait_time);
        total_wait += wait_time;
        wait_time = (wait_time + 2 > max_wait) ? max_wait : wait_time + 2;
    }

    if (total_wait >= max_total_wait) {
        snprintf(log_buf, sizeof(log_buf),
                "Timeout waiting for %s, exiting...", interface_name);
        log_msg(log_buf);
        remove_lock_file();
        exit(EXIT_FAILURE);
    }
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
