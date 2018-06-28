// Following tutorial https://viewsourcecode.org
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;


void die(const char *s) {
    // Prints error messages and exits the program
    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
        die("tcsetattr");
    }
}

void enableRawMode() {
    // Sets flags to enable raw mode instead of canonical
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        die("tcgetattr");
    }
    tcgetattr(STDIN_FILENO, &orig_termios);
    // Return to normal on exit
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    tcgetattr(STDIN_FILENO, &raw);
    // Flip the bits then AND to set ECHO to 0
    // Turn off canonical to get bytes
    // Turn off ctrl-v
    // Turn off the SIG signals that exit
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    // Fix ctrl-m newline carriage returns
    // Stops data suspension w/ ctr-S
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    // Turn off the carriage return for newlines, we control them
    raw.c_oflag &= ~(OPOST);
    // Set 8-bits per byte
    raw.c_cflag |= (CS8);
    // Set timeout for read
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

int main() {
    char c = '\0';
    enableRawMode();
    
    while (1) {
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) {
            die("read");
        }
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
        if (c == 'q') break;
    }
    return 0;
}
