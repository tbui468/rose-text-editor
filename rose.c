//Step 54: review this (and maybe a few steps before) to remember what I did
//don't worry too much about understanding everything, just try to finish the 180 or so steps
//Step 55: A Line Viewer

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
#define ROSE_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
    SCREEN_TOP, 
    SCREEN_CENTER,
    SCREEN_BOTTOM,
    SCREEN_LEFT,
//    SCREEN_MIDDLE,
    SCREEN_RIGHT,
    DEL_KEY
};


/*** data ***/
struct editorConfig {
    int mode;
    int cx, cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

enum editorMode {
    COMMAND = 0,
    INSERT,
    COMMAND_LINE
};

struct editorConfig E;


/*** terminal ***/

void die(const char* s) {
    write(STDOUT_FILENO, "\x1b[48;2;0;0;0m", 13);
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode() {
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}

void enableRawMode() {
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");

    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey() {
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if(nread == -1 && errno != EAGAIN) die("read");
    }

    if(c == '\x1b') {
        char seq[3];
        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if(seq[0] == '[') {
            if(seq[1] >= 0 && seq[1] <= 9) {
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if(seq[2] == '~') {
                    switch(seq[1]) {
                        case '1': return HOME_KEY; //\x1b[1~
                        case '2': return HOME_KEY; //\x1b[2~
                        case '3': return HOME_KEY; 
                        case '4': return HOME_KEY; 
                        case '5': return HOME_KEY; 
                        case '6': return HOME_KEY; 
                        case '7': return HOME_KEY; 
                        case '8': return HOME_KEY; 
                    }
                }
            }else{
                switch(seq[1]) {
                    case 'A': return ARROW_UP;  //\x1b[A
                    case 'B': return ARROW_DOWN; //\x1b[B
                    case 'C': return ARROW_RIGHT;  
                    case 'D': return ARROW_LEFT;
                    case 'E': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        }else if(seq[0] == 'O') {
            switch(seq[1]) {
                case 'H': return HOME_KEY; //\x1b[OH
                case 'F': return END_KEY; //\x1b[OF
            }
        }
        return '\x1b';
    }else{
        return c;
    }
}

int getCursorPosition(int* rows, int* cols) {
    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    char buf[32];
    unsigned int i = 0;
    while(i < sizeof(buf) - 1) {
        if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if(buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if(buf[0] != '\x1b' || buf[1] != '[') return -1;
    if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int* rows, int* cols) {
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    }else{
        *cols = ws.ws_col;
        *rows = ws.ws_row; 
        return 0;
    }
}

/*** append buffer ***/
struct abuf {
    char* b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf* ab, const char* s, int len) {
    char* new = realloc(ab->b, ab->len + len);

    if(!new) return;

    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/*** output ***/
void editorDrawRows(struct abuf* ab) {
    int y;
    int rows = E.screenrows - 3; 
    abAppend(ab, "\x1b[48;2;253;246;227m", 19); //set background color (use 38;2 to set foreground color)
    abAppend(ab, "\x1b[38;2;131;148;150m", 19); //set background color (use 38;2 to set foreground color)

    for(y = 0; y < rows; y++) {
        if(y == rows / 3) {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome), 
                    "Rose editor -- version %s", ROSE_VERSION);
            if(welcomelen > E.screencols) welcomelen = E.screencols;

            abAppend(ab, "~", 1);
            int padding = (E.screencols - welcomelen) * .5;
            while(padding--) abAppend(ab, " ", 1); 

            abAppend(ab, welcome, welcomelen);
        }else{
            abAppend(ab, "~", 1);
        }
        abAppend(ab, "\x1b[K", 3);
        if(y < rows) abAppend(ab, "\r\n", 2);
    }

    //status bar
    abAppend(ab, "\x1b[48;2;131;148;150m", 19);
    abAppend(ab, "\x1b[2K", 4);

    //last two rows after status bar
    abAppend(ab, "\x1b[48;2;253;246;227m", 19);
    abAppend(ab, "\r\n", 2);
    abAppend(ab, "\x1b[K", 3);

    abAppend(ab, "\r", 1);

    abAppend(ab, "\r\n", 2);
    abAppend(ab, "\x1b[K", 3);
}

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

/*** input ***/

void editorMoveCursor(int key) {
    switch(key) {
        case ARROW_LEFT:
            if(E.cx != 0) E.cx--; 
            break;
        case ARROW_DOWN: 
            if(E.cy != E.screenrows - 1) E.cy++; 
            break;
        case ARROW_UP: 
            if(E.cy != 0) E.cy--; 
            break;
        case ARROW_RIGHT: 
            if(E.cx != E.screencols - 1) E.cx++; 
            break;
        case SCREEN_TOP:
            E.cy = 0;
            break;
        case SCREEN_CENTER:
            E.cy = (E.screenrows - 1) / 2;
            break;
        case SCREEN_BOTTOM:
            E.cy = E.screenrows - 1;
            break;
        case SCREEN_LEFT:
            E.cx = 0;
            break;
        case SCREEN_RIGHT:
            E.cx = E.screencols - 1;
            break;
    }
}

void editorProcessKeypress() {
    int c = editorReadKey();
    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[48;2;0;0;0m", 13);
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_UP:
        case ARROW_DOWN:
        case SCREEN_TOP:
        case SCREEN_CENTER:
        case SCREEN_BOTTOM:
        case SCREEN_LEFT:
        case SCREEN_RIGHT:
            editorMoveCursor(c);
            break;

    }
}

/*** init ***/

void initEditor() {
    E.cx = 0;
    E.cy = 0;

    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    printf("%d", E.screenrows);

    while(1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
