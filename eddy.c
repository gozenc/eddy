#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define EDDY_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)

struct editorConfig {
	int cx, cy, screenrows, screencols;
	struct termios orig_termios;
};

struct editorConfig E;

/* TERMINAL */
void clearScreen(void) {
	write(STDOUT_FILENO, "\x1b[2J", 4); // Clears screen
	write(STDOUT_FILENO, "\x1b[H", 3); // Repositions cursor
}

void die(const char *s) {
	clearScreen();
	perror(s);
	exit(1);
}

void disableRawMode(void) {
	if ( tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
		die("tcsetattr");
	}
}

void enableRawMode(void) {
	if ( tcgetattr(STDIN_FILENO, &E.orig_termios) == -1 ) {
		die("tcgetattr");
	}

	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
		die("tcsetattr");
	}
}

char editorReadKey(void) {
	int nread;
	char c;
	while ( (nread = read(STDIN_FILENO, &c, 1)) != 1 ) {
		if ( nread == -1 && errno != EAGAIN ) die("read");
	}

  if ( c == '\x1b' ) {
    char seq[3];

    if ( read(STDIN_FILENO, &seq[0], 1) != 1 ) return '\x1b';
    if ( read(STDIN_FILENO, &seq[1], 1) != 1 ) return '\x1b';

    if ( seq[0] == '[' ) {
      switch (seq[1]) {
        case 'A': return 'w';
        case 'B': return 's';
        case 'C': return 'd';
        case 'D': return 'a';
      }
    }

    return '\x1b';
  } else {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {

	char buf[32];
	unsigned int i = 0;

	if ( write(STDOUT_FILENO, "\x1b[6n", 4) != 4 ) return -1;

	while ( i < sizeof(buf) -1) {
		if ( read(STDIN_FILENO, &buf[i], 1) != 1 ) break;
		if ( buf[i] == 'R' ) break;
		i++;
	}
	buf[i] = '\0';

	if ( buf[0] != '\x1b' || buf[1] != '[' ) return -1;
	if ( sscanf(&buf[2], "%d;%d", rows, cols) != 2 ) return -1;

	return 0;
}

int getWindowSize(int *rows, int *cols) {
	struct winsize ws;
	if ( ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 ) {
		if ( write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12 ) return -1;
		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/* APPEND BUFFER */
struct abuf {
	char *buffer;
	int len;
};

#define ABUF_INIT {NULL, 0}

void abufAppend( struct abuf *ab, const char *s, int len ) {
	char *new = realloc( ab->buffer, ab->len + len );

	if ( new == NULL ) return;
	memcpy( &new[ab->len], s, len );
	ab->buffer = new;
	ab->len += len;
}

void abufFree( struct abuf *ab ) {
	free(ab->buffer);
}

/* OUTPUT */
void editorDrawRows(struct abuf *ab) {
	int y;
	for (y = 0; y < E.screenrows; y++) {

		if ( y == E.screenrows / 3 ) {
			char welcome[80];
			int welcomelen = snprintf(
				welcome, sizeof(welcome),
				"EDDY Text Editor - v%s", EDDY_VERSION
			);
			if ( welcomelen > E.screencols ) {
				welcomelen = E.screencols;
			}

			int padding = (E.screencols - welcomelen) / 2;
			if ( padding ) {
				abufAppend(ab, "~", 1);
				padding--;
			}
			while (padding--) abufAppend(ab, " ", 1);

			abufAppend(ab, welcome, welcomelen);
		} else {
			abufAppend(ab, "~", 1);
		}

		abufAppend(ab, "\x1b[K", 3);
		if ( y < E.screenrows - 1 ) {
			abufAppend(ab, "\r\n", 2);
		}
	}
}

void editorRefreshScreen(void) {
	struct abuf ab = ABUF_INIT;

	abufAppend(&ab, "\x1b[?25l", 6);
	abufAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abufAppend(&ab, buf, strlen(buf));

	abufAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.buffer, ab.len);
	abufFree(&ab);
}

/* INPUT */
void editorMoveCursor(char key) {
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

void editorProcessKeyPress(void) {
	char c = editorReadKey();
	
	switch (c) {
		case CTRL_KEY('q'):
			clearScreen();
			exit(0);
			break;

    case 'w':
    case 'a':
    case 's':
    case 'd':
      editorMoveCursor(c);
      break;
	}

}

/* INIT */
void initEditor(void) {
  E.cx = 0;
  E.cy = 0;
	if ( getWindowSize(&E.screenrows, &E.screencols) == -1 ) {
		die("getWindowSize");
	}
}

int main(void){
	enableRawMode();
	initEditor();

	while (1) {
		editorRefreshScreen();
		editorProcessKeyPress();
	}

	return 0;
}
