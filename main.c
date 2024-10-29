/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/

struct editorConfig {
    int cx, cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

/**
 * die - Exits the program with an error message.
 * @s: The error message to display.
 *
 * This function clears the screen, moves the cursor to the top-left corner,
 * prints the error message using perror, and exits the program with a status
 * code of 1.
 */
void die(const char* s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

/**
 * disableRawMode - Disables raw mode for the terminal.
 *
 * This function restores the original terminal attributes stored in E.orig_termios.
 * If tcsetattr fails, it calls die() with an error message.
 */
void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

/**
 * enableRawMode - Enables raw mode for the terminal.
 *
 * This function retrieves the current terminal attributes and stores them in
 * E.orig_termios. It then modifies the terminal attributes to disable canonical
 * mode, echoing, and other features, and sets the terminal to raw mode. If any
 * of the terminal attribute functions fail, it calls die() with an error message.
 */
void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/**
 * editorReadKey - Reads a key press from the terminal.
 *
 * This function reads a single key press from the terminal and handles escape
 * sequences for arrow keys and other special keys. It returns the key code of
 * the pressed key.
 *
 * Return: The key code of the pressed key.
 */
int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                    case '3':
                        return DEL_KEY;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    }
                }
            } else {
                switch (seq[1]) {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                }
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

/**
 * getCursorPosition - Gets the current cursor position.
 * @rows: Pointer to store the row position.
 * @cols: Pointer to store the column position.
 *
 * This function queries the terminal for the current cursor position and stores
 * the result in the provided pointers. It returns 0 on success and -1 on failure.
 *
 * Return: 0 on success, -1 on failure.
 */
int getCursorPosition(int* rows, int* cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

/**
 * getWindowSize - Gets the size of the terminal window.
 * @rows: Pointer to store the number of rows.
 * @cols: Pointer to store the number of columns.
 *
 * This function retrieves the size of the terminal window and stores the result
 * in the provided pointers. It returns 0 on success and -1 on failure.
 *
 * Return: 0 on success, -1 on failure.
 */
int getWindowSize(int* rows, int* cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** append buffer ***/

/**
 * abAppend - Appends a string to the append buffer.
 * @ab: Pointer to the append buffer.
 * @s: The string to append.
 * @len: The length of the string.
 *
 * This function appends the given string to the append buffer, reallocating
 * memory as needed.
 */
struct abuf {
    char* b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf* ab, const char* s, int len) {
    char* new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

/**
 * abFree - Frees the memory used by the append buffer.
 * @ab: Pointer to the append buffer.
 *
 * This function frees the memory allocated for the append buffer.
 */
void abFree(struct abuf* ab) {
    free(ab->b);
}

/*** output ***/

/**
 * editorDrawRows - Draws rows of text on the screen.
 * @ab: Pointer to the append buffer.
 *
 * This function draws rows of text on the screen, including a welcome message
 * in the middle of the screen.
 */
void editorDrawRows(struct abuf* ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        if (y == E.screenrows / 3) {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome),
                                      "Kilo editor -- version %s", KILO_VERSION);
            if (welcomelen > E.screencols) welcomelen = E.screencols;
            int padding = (E.screencols - welcomelen) / 2;
            if (padding) {
                abAppend(ab, "~", 1);
                padding--;
            }
            while (padding--) abAppend(ab, " ", 1);
            abAppend(ab, welcome, welcomelen);
        } else {
            abAppend(ab, "~", 1);
        }

        abAppend(ab, "\x1b[K", 3);
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

/*** input ***/

/**
 * editorRefreshScreen - Refreshes the screen.
 *
 * This function clears the screen, draws the rows of text, moves the cursor to
 * the current position, and shows the cursor.
 */
void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/**
 * editorMoveCursor - Moves the cursor based on the key pressed.
 * @key: The key code of the pressed key.
 *
 * This function moves the cursor position based on the arrow key pressed.
 */
void editorMoveCursor(int key) {
    switch (key) {
    case ARROW_LEFT:
        if (E.cx != 0) {
            E.cx--;
        }
        break;
    case ARROW_RIGHT:
        if (E.cx != E.screencols - 1) {
            E.cx++;
        }
        break;
    case ARROW_UP:
        if (E.cy != 0) {
            E.cy--;
        }
        break;
    case ARROW_DOWN:
        if (E.cy != E.screenrows - 1) {
            E.cy++;
        }
        break;
    }
}

/**
 * editorProcessKeypress - Processes a key press.
 *
 * This function reads a key press and performs the corresponding action, such
 * as moving the cursor or exiting the program.
 */
void editorProcessKeypress() {
    int c = editorReadKey();

    switch (c) {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

    case PAGE_UP:
    case PAGE_DOWN: {
        int times = E.screenrows;
        while (times--)
            editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    } break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;
    }
}

/*** init ***/

/**
 * initEditor - Initializes the editor configuration.
 *
 * This function initializes the editor configuration, setting the cursor
 * position to (0, 0) and retrieving the window size.
 */
void initEditor() {
    E.cx = 0;
    E.cy = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

/**
 * main - The main entry point of the program.
 *
 * This function enables raw mode, initializes the editor, and enters the main
 * loop to refresh the screen and process key presses.
 *
 * Return: Always returns 0.
 */
int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}