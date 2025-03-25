#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>

#include <sys/stat.h>

#include "hgledon.h"

#define LOCK_FILE "/var/run/trafmon.lock"
#define IFACE_FILE "/var/run/trafmon.iface"

volatile int running = 1;
char interface_name[32];
char log_buf[128];

void stop_daemon(int) {
    running = 0;
}

void log_msg(const char *msg) {
    openlog("trafmon", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "%s", msg);
    closelog();
}

int get_rate(long rate) {
    if (rate > 1024) return 50;
    if (rate > 512)  return 100;
    if (rate > 256)  return 150;
    return 0;
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

void lan(const char *state) {
    static GPIO_PINS pins;
    static int initialized = 0;

    if (!initialized) {
        pins = init_gpio();
        initialized = 1;
    }

    exec("-lan", state, pins);
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
                lan("on");
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
    int max_val = 150; // Safe value
    lan("on"); // Init lan state on

    long p_rx = get_traffic(interface_name, "rx");
    long p_tx = get_traffic(interface_name, "tx");

    while (running) {
        long c_rx = get_traffic(interface_name, "rx");
        long c_tx = get_traffic(interface_name, "tx");
        long rx_rate = (c_rx - p_rx) / 1024;
        long tx_rate = (c_tx - p_tx) / 1024;
        long final_rate = rx_rate + tx_rate;
        int int_val = get_rate(final_rate);

        if (int_val > 0 && int_val <= max_val) {
            lan("dis");
            sleep_ms(int_val);
            lan("on");
        }

        p_rx = c_rx;
        p_tx = c_tx;

        if (!running) break;

        sleep_ms(150);
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
    if (argc == 2 && strcmp(argv[1], "help") == 0) {
        const char *prog = argv[0];
        printf("\n████████ ██████   █████  ███████ ███    ███  ██████  ███    ██ \n");
        printf("   ██    ██   ██ ██   ██ ██      ████  ████ ██    ██ ████   ██ \n");
        printf("   ██    ██████  ███████ █████   ██ ████ ██ ██    ██ ██ ██  ██ \n");
        printf("   ██    ██   ██ ██   ██ ██      ██  ██  ██ ██    ██ ██  ██ ██ \n");
        printf("   ██    ██   ██ ██   ██ ██      ██      ██  ██████  ██   ████ \n");
        printf("\nLED Traffic Monitor Daemon\n\n");
        printf("Usage:\n");
        printf("  %s start <interface> - Start traffic monitoring on the given interface\n", prog);
        printf("  %s stop              - Stop the running traffic monitor\n", prog);
        printf("  %s status            - Check the status of the traffic monitor\n", prog);
        printf("  %s help              - Show this help message\n", prog);
        printf("\n");
        printf("The traffic monitor will blink the LAN LED when traffic is detected.\n");
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

    printf("Invalid command. Use help for usage instructions.\n");
    return EXIT_SUCCESS;
}
