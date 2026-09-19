/*
 * uart_bridge.c - Forward ST-LINK UART input to LCD terminal (tty1) via TIOCSTI
 *
 * Provides a single unified terminal session on /dev/tty1 (LCD),
 * allowing both UART (ttyS5) and USB HID keyboard (VT path) to provide input.
 * Supports Ctrl+X (0x18) emergency recovery shell on UART.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define LCD_TTY_DEV "/dev/tty1"
#define ESCAPE_CHAR 0x18 /* Ctrl+X */

static struct termios orig_termios;
static int raw_mode_active = 0;

static void set_raw_mode(int fd)
{
    struct termios raw;
    if (tcgetattr(fd, &orig_termios) < 0) {
        perror("tcgetattr");
        return;
    }
    raw = orig_termios;
    /* Disable echo, canonical mode, and signals on UART so all keys pass through */
    raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    raw.c_oflag &= ~OPOST;
    raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= ~(CSIZE | PARENB);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &raw);
    raw_mode_active = 1;
}

static void restore_mode(int fd)
{
    if (raw_mode_active) {
        tcsetattr(fd, TCSANOW, &orig_termios);
        raw_mode_active = 0;
    }
}

static void run_emergency_shell(int uart_fd)
{
    const char *msg_enter = "\r\n[UART BRIDGE: Entering Emergency Recovery Shell. Type 'exit' to resume LCD bridge.]\r\n";
    const char *msg_exit  = "\r\n[UART BRIDGE: Resumed shared LCD terminal bridge.]\r\n";

    write(uart_fd, msg_enter, strlen(msg_enter));
    restore_mode(uart_fd);

    pid_t pid = vfork();
    if (pid == 0) {
        /* Child */
        /* Login shell: sources /etc/profile (PATH, bbx applet functions). */
        execl("/bin/sh", "-sh", NULL);
        _exit(127);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    } else {
        const char *err = "vfork failed!\r\n";
        write(uart_fd, err, strlen(err));
    }

    set_raw_mode(uart_fd);
    write(uart_fd, msg_exit, strlen(msg_exit));
}

int main(int argc, char **argv)
{
    const char *target_tty = LCD_TTY_DEV;
    int uart_fd = STDIN_FILENO;
    int tty_fd = -1;
    unsigned char c;

    if (argc > 1) {
        target_tty = argv[1];
    }

    /* Open target LCD virtual terminal with retries */
    for (int retry = 0; retry < 10; retry++) {
        tty_fd = open(target_tty, O_RDWR);
        if (tty_fd >= 0)
            break;
        sleep(1);
    }

    if (tty_fd < 0) {
        perror("open target tty");
        return 1;
    }

    set_raw_mode(uart_fd);

    while (1) {
        ssize_t n = read(uart_fd, &c, 1);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN))
                continue;
            break; /* EOF or error */
        }

        /* Check for emergency escape (Ctrl+X) */
        if (c == ESCAPE_CHAR) {
            run_emergency_shell(uart_fd);
            continue;
        }

        /* Forward keypress into target tty's line discipline */
        if (ioctl(tty_fd, TIOCSTI, &c) < 0) {
            /* If target closed or failed, retry opening */
            close(tty_fd);
            sleep(1);
            tty_fd = open(target_tty, O_RDWR);
        }
    }

    restore_mode(uart_fd);
    close(tty_fd);
    return 0;
}
