#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

struct editorConfig {
    struct termios orig_termios;
};
struct editorConfig E;

void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON);

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
  enableRawMode();

  char c;
  while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
  return 0;
}