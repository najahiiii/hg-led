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
}

void remove_lock_file() {
    remove(LOCK_FILE);
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
                printf("Traffic monitor stopped.\n");
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
    long prev_rx = get_traffic(interface_name, "rx");
    long prev_tx = get_traffic(interface_name, "tx");

    // Init lan state on
    lan("on");

    while (running) {
        long rx = get_traffic(interface_name, "rx");
        long tx = get_traffic(interface_name, "tx");
        long rx_rate = (rx - prev_rx) / 1024;
        long tx_rate = (tx - prev_tx) / 1024;
        long total_rate = rx_rate + tx_rate;
        int int_val = get_rate(total_rate);

        if (int_val > 0 && int_val <= 150) {
            lan("dis");
            sleep_ms(int_val);
            lan("on");
        }

        if (!running) break;

        prev_rx = rx;
        prev_tx = tx;

        sleep_ms(150);
    }
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);
    setsid();
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    chdir("/");
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int fd = open("/dev/null", O_RDWR);
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);

    int wait_time = 2;
    int total_wait = 0;
    const int max_wait = 30;

    while (!check_iface(interface_name)) {
        snprintf(log_buf, sizeof(log_buf),
                    "Interface %s not found, waiting %d seconds...", interface_name, wait_time);
        log_msg(log_buf);

        sleep(wait_time);
        total_wait += wait_time;
        wait_time += 2;

        if (wait_time > max_wait) wait_time = max_wait;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <interface|stop>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (argc == 2 && strcmp(argv[1], "stop") == 0) {
        return stop_process();
    }

    strncpy(interface_name, argv[1], sizeof(interface_name) - 1);
    interface_name[sizeof(interface_name) - 1] = '\0';

    if (check_running()) {
        printf("Traffic monitor already running!\n");
        return EXIT_FAILURE;
    }

    printf("Starting traffic monitor for interface %s...\n", interface_name);
    daemonize();
    create_lock_file();
    signal(SIGTERM, stop_daemon);

    snprintf(log_buf, sizeof(log_buf), "Starting traffic monitor for interface %s...", interface_name);
    log_msg(log_buf);
    monitor_traffic();

    sprintf(log_buf, "Monitor for interface %s stopped.", interface_name);
    log_msg(log_buf);
    remove_lock_file();

    return EXIT_SUCCESS;
}
