// Following tutorial https://viewsourcecode.org
// Using vt100 escape sequences
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// Gets the bytes for the control key combo
#define CTRL_KEY(k) ((k) & 0x1f)
#define NTEXT_VERSION "0.0.1"


struct editorConfig {
    int cx, cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};
struct editorConfig E;

/*** append buffer ***/
// Creating a buffer to append to so we write once
struct abuf {
    char *buffer;
    int len;
};
#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
    // Append characters to the buffer
    char *newBuffer = realloc(ab->buffer, ab->len + len);
    if (newBuffer == NULL) {
        return;
    }
    memcpy(&newBuffer[ab->len], s, len);
    ab->buffer = newBuffer;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    // Free the string buffer
    free(ab->buffer);
}

void die(const char *s) {
    // Prints error messages and exits the program
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("tcsetattr");
    }
}

void enableRawMode() {
    // Sets flags to enable raw mode instead of canonical
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
        die("tcgetattr");
    }
    tcgetattr(STDIN_FILENO, &E.orig_termios);
    // Return to normal on exit
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
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

char editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

int getCursorPosition(int *rows, int *cols) {
    // Get cursor POS so we can setup term attributes
    char buf[32];
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) {
            break;
        }
        if (buf[i] == 'R') {
            break;
        }
        i++;
    }
    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[') {
        return -1;
    }
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
        return -1;
    }
    return 0;
}

int getWindowSize(int *rows, int *cols) {
    // Gets the window size of the terminal
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // Move cursor to bottom right to try to get total rows/cols
        // fallback if doesn't work
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return -1;
        }
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void editorMoveCursor(char key) {
    // Move cursor with WASD
    switch (key) {
        case 'a':
            E.cx--;
            break;
        case 'd':
            E.cx++;
            break;
        case 'w':
            E.cy--;
            break;
        case 's':
            E.cy++;
            break;
    }
}

void editorProcessKeypress() {
    char c = editorReadKey();
    switch (c) {
        case CTRL_KEY('q'):
            // If ctrl-q, exit
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case 'w':
        case 's':
        case 'a':
        case 'd':
            // Cursor movement
            editorMoveCursor(c);
            break;
    }
}

void editorDrawRows(struct abuf *ab) {
    // Draws ~ at beginning of rows
    int y;
    for (y = 0; y < E.screenrows; y++) {
        // Display a welcom message 1/3 way down
        if (y == E.screenrows / 3) {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome),
                "nText Editor -- Version %s", NTEXT_VERSION);
            if (welcomelen > E.screencols) {
                welcomelen = E.screencols;
            }
            // Center the message
            int padding = (E.screencols - welcomelen) / 2;
            if (padding) {
                abAppend(ab, "~", 1);
                padding--;
            }
            while (padding--) {
                abAppend(ab, " ", 1);
            }
            abAppend(ab, welcome, welcomelen);
        } else {
            abAppend(ab, "~", 1);
        }
        // Clears the line to right of cursor
        abAppend(ab, "\x1b[K", 3);
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;

    // Hide cursor
    abAppend(&ab, "\x1b[?25l", 6);
    // Move cursor to top left
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    // Move cursor
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    // Show cursor
    abAppend(&ab, "\x1b[?25h", 6);
    write(STDOUT_FILENO, ab.buffer, ab.len);
    abFree(&ab);
}

void initEditor() {
    E.cx = 0;
    E.cy = 0;
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}


int main() {
    enableRawMode();
    initEditor();
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
