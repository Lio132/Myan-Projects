#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <pty.h>

#define BUFFER_SIZE 4096

static volatile sig_atomic_t resize_requested = 0;
static volatile sig_atomic_t stop_requested = 0;

static void handle_resize(int signal_number)
{
    (void)signal_number;
    resize_requested = 1;
}

static void handle_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int write_all(int fd, const char *buf, ssize_t len)
{
    ssize_t offset = 0;

    while (offset < len)
    {
        ssize_t n = write(fd, buf + offset,
                          (size_t)(len - offset));

        if (n == -1)
        {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (n == 0)
            return -1;

        offset += n;
    }

    return 0;
}

static int update_terminal_size(int master)
{
    struct winsize size;

    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) == -1)
        return -1;

    if (ioctl(master, TIOCSWINSZ, &size) == -1)
        return -1;

    return 0;
}

int main(void)
{
    struct termios original, raw;
    struct sigaction action = {0};
    int master;
    int stdin_enabled = 1;
    int result = 0;
    pid_t pid;
    char buffer[BUFFER_SIZE];

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
    {
        fprintf(stderr,
                "msh benötigt ein interaktives Terminal.\n");
        return 1;
    }

    if (tcgetattr(STDIN_FILENO, &original) == -1)
    {
        perror("tcgetattr");
        return 1;
    }

    action.sa_handler = handle_resize;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGWINCH, &action, NULL) == -1)
    {
        perror("sigaction SIGWINCH");
        return 1;
    }

    action.sa_handler = handle_stop;

    if (sigaction(SIGTERM, &action, NULL) == -1 ||
        sigaction(SIGHUP, &action, NULL) == -1)
    {
        perror("sigaction");
        return 1;
    }

    pid = forkpty(&master, NULL, NULL, NULL);

    if (pid == -1)
    {
        perror("forkpty");
        return 1;
    }

    if (pid == 0)
    {
        execl("/bin/bash", "bash",
              "--noprofile", "--norc", "-i",
              (char *)NULL);

        perror("execl");
        _exit(127);
    }

    raw = original;

    if (cfmakeraw(&raw), tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    {
        perror("tcsetattr");
        kill(pid, SIGHUP);
        close(master);
        while (waitpid(pid, NULL, 0) == -1 && errno == EINTR)
            ;
        return 1;
    }

    if (update_terminal_size(master) == -1)
        resize_requested = 1;

    while (!stop_requested)
    {
        if (resize_requested)
        {
            resize_requested = 0;

            if (update_terminal_size(master) == -1 &&
                errno != EINTR)
            {
                perror("Terminalgröße");
                result = 1;
                break;
            }
        }

        fd_set readfds;
        FD_ZERO(&readfds);

        if (stdin_enabled)
            FD_SET(STDIN_FILENO, &readfds);

        FD_SET(master, &readfds);

        int maxfd = master;

        if (stdin_enabled && STDIN_FILENO > maxfd)
            maxfd = STDIN_FILENO;

        int ready = select(maxfd + 1, &readfds,
                           NULL, NULL, NULL);

        if (ready == -1)
        {
            if (errno == EINTR)
                continue;

            perror("select");
            result = 1;
            break;
        }

        if (stdin_enabled &&
            FD_ISSET(STDIN_FILENO, &readfds))
        {
            ssize_t n = read(STDIN_FILENO,
                             buffer, sizeof(buffer));

            if (n == 0)
            {
                /* EOF: einmal Ctrl+D senden und
                   weitere EOF-Leseversuche verhindern. */
                if (write_all(master, "\004", 1) == -1)
                    break;

                stdin_enabled = 0;
            }
            else if (n < 0)
            {
                if (errno == EINTR)
                    continue;

                perror("read stdin");
                result = 1;
                break;
            }
            else if (write_all(master, buffer, n) == -1)
            {
                if (errno != EIO)
                    perror("write PTY");

                break;
            }
        }

        if (FD_ISSET(master, &readfds))
        {
            ssize_t n = read(master, buffer, sizeof(buffer));

            if (n > 0)
            {
                if (write_all(STDOUT_FILENO, buffer, n) == -1)
                {
                    perror("write stdout");
                    result = 1;
                    break;
                }
            }
            else if (n == 0 ||
                     (n == -1 && errno == EIO))
            {
                break;
            }
            else if (errno != EINTR)
            {
                perror("read PTY");
                result = 1;
                break;
            }
        }
    }

    /* Terminalzustand immer wiederherstellen. */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original) == -1)
    {
        perror("Terminal wiederherstellen");
        result = 1;
    }

    close(master);

    int status;
    pid_t waited;

    do
    {
        waited = waitpid(pid, &status, 0);
    } while (waited == -1 && errno == EINTR);

    if (waited == -1)
    {
        perror("waitpid");
        result = 1;
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
    {
        result = 127;
    }
    else if (WIFSIGNALED(status))
    {
        result = 128 + WTERMSIG(status);
    }

    return result;
}
