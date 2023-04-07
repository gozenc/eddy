#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct termios orig_termios;

/* TERMINAL */
void clearScreen(void) {
	// ESC Sequence: Clears screen
	write(STDOUT_FILENO, "\x1b[2J", 4);
	// ESC Sequence: Repositions cursor
	write(STDOUT_FILENO, "\x1b[H", 3); 
}

void die(const char *s) {
	clearScreen();
	perror(s);
	exit(1);
}

void disableRawMode(void) {
	if ( tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
		die("tcsetattr");
	}
}

void enableRawMode(void) {
	if ( tcgetattr(STDIN_FILENO, &orig_termios) == -1 ) {
		die("tcgetattr");
	}

	atexit(disableRawMode);

	struct termios raw = orig_termios;
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
	// if (iscntrl(c)) {
	// 	printf("%d\r\n", c);
	// } else {
	// 	printf("%d ('%c')\r\n", c, c);
	// }
	return c;
}

/* OUTPUT */
void editorDrawRows(void) {
	int y;
	for (y = 0; y < 24; y++) {
		write(STDOUT_FILENO, "~\r\n", 3);
	}
}

void editorRefreshScreen(void) {
	clearScreen();
	editorDrawRows();
	write(STDOUT_FILENO, "\x1b[H", 3);
}

/* INPUT */
void editorProcessKeyPress(void) {
	char c = editorReadKey();
	
	switch (c) {
		case CTRL_KEY('q'):
			clearScreen();
			exit(0);
			break;
	}

}

/* INIT */
int main(void){
	enableRawMode();

	while (1) {
		editorRefreshScreen();
		editorProcessKeyPress();
	}

	return 0;
}
