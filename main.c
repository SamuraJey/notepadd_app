#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

// error handling
void die(const char* custom_msg) {
    perror(custom_msg);  // print the mesage and description of errno of failed call
    exit(1);             // return error val to terminal
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("resteting terminal config");
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
        die("geting terminal config");
    atexit(disableRawMode);

    struct termios raw = orig_termios;

    raw.c_iflag &= ~(IXON | ICRNL | INPCK | BRKINT);
    // raw.c_iflag &= ~(ISTRIP);  // set 8-th bit of input to 0. (input becomes layout insenseable)
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_lflag &= ~(IEXTEN);
    // timeouts
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("seting terminal config");
}

int main() {
    enableRawMode();

    char c;
    while (true) {
        c = '\0';
        read(STDIN_FILENO, &c, 1);
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
        if (c == 'q') break;
    };
    return 0;
}