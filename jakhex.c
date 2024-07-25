/*
Copyright 2024 Vlad Mesco

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#define _POSIX_SOURCE

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <errno.h>

#include <curses.h>

static float i32tofloat32(int i)
{
    void* pv = &i;
    float* pf = (float*)pv;
    return *pf;
}

static double i64tofloat64(long i)
{
    void* pv = &i;
    double* pf = (double*)pv;
    return *pf;
}

extern void*
memsearch(void*, size_t, void*, size_t);
extern void*
rmemsearch(void*, size_t, void*, size_t);

// buffer state
unsigned char* mem = NULL;
size_t memsize = 0;
size_t memcapacity = 0;
size_t memoffset = 0;
char* fname = NULL;
size_t windowOffset = 0;
int lownibble = 0;

size_t markers[26];
unsigned char* clipboard = NULL;
size_t clipboardsize = 0;
unsigned char* searchString = NULL;
size_t nSearchString = 0;

char* mystrdup(const char* s)
{
    char* rval = malloc(strlen(s) + 1);
    if(!rval) abort();
    strcpy(rval, s);
    return rval;
}

// exit function
static void finish(int sig);
static void showhelp(const char* argv0);

// drawing functions
static void redraw(void);
static void adjust_screen(void);
static void update_status(void);
static void update_details(void);
static void printbinle(int l, int c);
static void printbinbe(int l, int c);
static void printbin(int l, int c, unsigned long);
static void show_detailed_cursor(void);

// movement functions
enum DIR {
    RIGHT, LEFT, DOWN, UP
};
static void mymove(int dir);
static void pagemove(int dir);
static void goto_command(void);
static void advance_offset(long sign);
static void set_marker(void);
static void list_markers(void);
static void goto_marker(void);
static void save_search_string(void* s, size_t len);
static void continue_find_cb(
        unsigned char* from, size_t nfrom,
        void* (*cb_search)(void*, size_t, void*, size_t));
static void find_cb(
        unsigned char* from, size_t nfrom,
        void* (*cb_search)(void*, size_t, void*, size_t));
static void find_forward(int prompt);
static void find_backward(int prompt);

// region functions
static void blank_region(void);
static void kill_region(void);
static void yank_region(void);
static void write_region(void);
static void paste_clipboard(size_t before);
static void overwrite_clipboard(void);

// dialog functions
static int question(const char* q);
static char* read_string(const char* prompt);
static char read_key(const char* prompt, const char* allowed);
static char* read_filename(void);
static void myhelp(void);

// input functions
static void punch(int c);
static void overwrite_mem(void* mem, size_t nbytes);
static void punch_interpreted(void);
static void truncate_file(size_t at);
static void insert_n_nulls(size_t before, size_t n);
static void insert_nulls(size_t before);
// file functions
static void insert_file(size_t before);
static void new_file(void);
static void test_file(void);
static void save_file(void);
static void open_file1(void);
static void open_file(void);

// main loop handlers
// handle_normal handles "normal" hex input and command mode
static void handle_normal(int c);
// handle_text handles keyboard input in text punch in mode
static void handle_text(int c);
void (*handle_keys)(int) = handle_normal;

static char HEX[] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    0
};

static const char* HELP[] = {
"Key bindings:\n",
"0-9a-f      edit bytes\n",
"arrows      move on screen\n",
"hjkl        move on screen\n",
"pgup/pgdwn  screenful\n",
"+           jump forward n bytes\n",
"-           jump backward n bytes\n",
"^F/^B       screenful\n",
")/(         screenful\n",
"g           goto address; negative means from end\n",
"/           find\n",
"?           rfind\n",
"n           continue searching forward\n",
"N           continue searching backward\n",
"<           insert nulls\n",
">           append nulls\n",
"w, F2, ^S   write file\n",
"r           insert file\n",
"R           append file\n",
"o, F3, ^O   edit/open file\n",
"q           exit with prompt\n",
"Q           exit without prompt\n",
"m           mark\n",
"M           list marks\n",
"G, ', `     goto mark\n",
//"^H, DEL     delete/cut region\n",
"x           delete/cut region\n",
"W           write region\n",
"@           blank region\n",
"y           copy region\n",
"p           insert clipboard\n",
"P           append clipboard\n",
"*           overwrite with clipboard\n",
"^G          show alternative cursor position or cancel\n",
"ESC ^C      cancel prompt of complicated command\n",
"$, ^K       truncate file\n",
"^L          refresh\n",
//"TAB ^I *    hide byte interpretation\n",
"!, F1       help\n",
"\n",
":, .        punch in (brings up menu)\n",
"            shortcuts:\n",
"F4, i       punch in text\n",
//"F5          punch in f32le (e.g. 1.3f)\n",
//"F6          punch in f32be (e.g. 0x1.91eb86p+1)\n",
//"F7          punch in f64le\n",
//"F8          punch in f64be\n",
//"F9          punch in i32le (e.g. 0x42)\n",
//"F10         punch in i32be (e.g. 123u)\n",
//"F11         punch in i64le (e.g. -47)\n",
//"F12         punch in i64be (e.g. 0x1FFFF)\n",
"\n",
"find syntax:\n",
//"' \\t'       whitespace ignored\n",
"0f fe 42    specific bytes\n",
"ttext       ascii text\n",
"\n",
"text input mode (shows an A bottom left):\n", "F1         help\n",
"printable  punches in said characters\n",
"^@ ^I ^J   punches in said characters\n",
"^M         punches in said characters\n",
"^H ^?      move left\n",
"arrows     move around\n",
"HOME END   move around\n",
"PGUP PGDWN move around\n",
"F4 ^C ^D   back to hex mode\n",
"ESC ^G     back to hex mode\n",
"F2 ^S      save file\n",
"F3 ^O      open/edit file\n",
"\n",
};

/* print help and exit */
void showhelp(const char* argv0)
{
    printf("jakhex %s by Vlad Mesco\n", VERSION);
    printf("\n");
    printf("Usage: %s file [+offset]\n", argv0);
    printf("\n");
    printf("    -h      show this message\n");
    printf("    +offset initial cursor position.\n");
    printf("            negative means offset from the end\n");
    printf("\n");
    for(int i = 0; i < sizeof(HELP)/sizeof(HELP[0]); ++i) {
        printf("%s", HELP[i]);
    }
    exit(1);
}

/* main */
int main(int argc, char* argv[])
{
    /* check command line arguments first; if we need to show help,
       then ncurses needs to be off. */
    ssize_t offset = 0;
    if(argc > 1) {
        if(strcmp(argv[1], "-h") == 0) {
            showhelp(argv[0]);
        }
        if(argc > 2) {
            offset = atol(argv[2]);
        }
        fname = mystrdup(argv[1]);
    }

    /* if there's no tty, complain and exit;
       if you want to automate binary surgery, do so from C or Python */
    if(!isatty(fileno(stdout)) || !isatty(fileno(stdin))) {
        fprintf(stderr, "Output is not a tty, cowardly exiting!\n");
        exit(2);
    }
    // TODO utf-8
    //      I'm not particularily interested in supporting unicode right now.
    //      The only issue has to do with file names. For editing, you're
    //      probably more interested in bits and bytes than interpreting
    //      unicode strings.
    //setlocale(LC_ALL, "");

    signal(SIGQUIT, finish);
    signal(SIGTERM, finish);

    // init curses
    initscr();
    keypad(stdscr, TRUE);
    nonl(); // no NL->CR/NL on output?
    noecho();
    //cbreak();
    raw();
    // only enable with switch; maybe have keys to disable mouse...
    // the reason is, I want to be able to use the yank buffer (X or otherwise)
    // to yank stuff; it's a lot more useful than clicking on a byte.
    //mousemask(ALL_MOUSE_EVENTS, NULL); // figure this out later

    // if this is set, we were instructed to load a file
    if(fname) {
        // preassign some memory to the buffer in case file read fails
        new_file();
        // try to open the file
        open_file1();
        // jump to the address specified
        if(offset == 0) memoffset = offset;
        else if(offset > 0 && offset < memsize) memoffset = offset;
        else {
            offset = -offset;
            if(offset < memsize) memoffset = memsize - offset;
        }
        adjust_screen();
    } else {
        // else, load a playground. Good for testing AND teaching!
        test_file();
    }

    redraw();

    // main loop
    for(;;) {
        int line = memoffset / 32;
        int wline = line - windowOffset;
        int byte = memoffset % 32;
        int col = 9 + (byte/4) + byte*2 + lownibble;
        int c = mvgetch(wline, col); // TODO move back to where we "were"

        update_status(); // reset status line after any key press

        // handle whatever key was pressed
        handle_keys(c);
    }

    return 0;
}

// clear and fully redraw screen. Useful if you jumped a screenful
// or if you're coming back from a subscreen.
void redraw(void)
{
    clear();

    // print file
    for(size_t i = 0; i < LINES - 10 - 1; ++i) {
        int loffset = (windowOffset + i) * 32ul;
        // address of first byte of line
        attron(A_STANDOUT);
        mvaddch(i, 0, HEX[(loffset >>28) & 0xF]);
        mvaddch(i, 1, HEX[(loffset >>24) & 0xF]);
        mvaddch(i, 2, HEX[(loffset >>20) & 0xF]);
        mvaddch(i, 3, HEX[(loffset >>16) & 0xF]);
        mvaddch(i, 4, HEX[(loffset >>12) & 0xF]);
        mvaddch(i, 5, HEX[(loffset >> 8) & 0xF]);
        mvaddch(i, 6, HEX[(loffset >> 4) & 0xF]);
        mvaddch(i, 7, HEX[(loffset >> 0) & 0xF]);
        attroff(A_STANDOUT);
        // 32 bytes
        for(int b = 0; b < 32; ++b) {
            int col = 9 // addr column
                    + (b/4) // number of full words
                    + b*2;
            size_t off = (windowOffset + i) * 32ul + (unsigned long)b;
            if(off >= memsize) break;
            mvaddch(i, col, HEX[mem[off] >> 4]);
            mvaddch(i, col+1, HEX[mem[off] & 0xF]);
        }
    }

    // separate details panel
    attron(A_STANDOUT);
    mvhline(LINES - 10 - 1, 0, '-', COLS);
    attroff(A_STANDOUT);

    // print details (e.g. int, float, string interpretations of bytes)
    update_details();
    // update status line to show cursor position
    update_status();
}

/* exits program. Tears down curses beforehand */
void finish(int code)
{
    endwin();
    // TODO save swap file if we can and we didn't force quit?
    exit(0);
}

/* allocate an initial memory buffer */
void new_file(void)
{
    mem = realloc(mem, 64ul * 1024ul);
    if(!mem) abort();
    memcapacity = 64ul * 1024ul;
    memsize = 0;
    memset(mem, 0, memcapacity);
    memoffset = 0;
    windowOffset = 0;
}

/* create a new buffer and load playground data */
void test_file(void)
{
    new_file();

    // DEBUG
    memsize = 64 * 40;
    for(int i = 0; i < memsize; ++i) {
        mem[i] = i;
    }

    if(fname == NULL) fname = mystrdup("file.bin");

    const char* hellotext = "F1/! for help. Here's a sandbox.";
    memcpy(mem, hellotext, strlen(hellotext));

    float floats[] = { 1.f, 2.f, -3.f, 112233445566.f, -1122334455667788.f, 3.14159f, -3.14159f, 0.f };
    memcpy(mem + strlen(hellotext), floats, sizeof(floats));
    double dbls[] = { 1.0, 2.0, -3.0, 112233445566.0, -1122334455667788.0, 3.1415926535897931, -3.1415926535897931, 0.f };
    memcpy(mem + strlen(hellotext) + sizeof(floats), dbls, sizeof(dbls));

    unsigned long specials[] = {
        0xFFFFFFFFFFFFFFFFul,
        0x7FFFFFFFFFFFFFFFul,
        0x8000000000000000ul,
        0x0000000000000000ul,
    };
    memcpy(mem + strlen(hellotext) + sizeof(floats) + sizeof(dbls), specials, sizeof(specials));
}

/* cursor movement motion. dir is enum DIR */
void mymove(int dir)
{
    switch(dir) {
    case RIGHT:
        if(memsize == 0 || (memoffset == memsize - 1 && lownibble)) return;
        memoffset += lownibble;
        lownibble = !lownibble;
        break;
    case LEFT:
        if(memoffset == 0 && !lownibble) return;
        memoffset -= !lownibble;
        lownibble = !lownibble;
        break;
    case DOWN:
        if(memoffset + 32 >= memsize) return;
        memoffset += 32;
        break;
    case UP:
        if(memoffset < 32) return;
        memoffset -= 32;
        break;
    }

    // we may have jumped off screen
    adjust_screen();
    update_status();
    update_details();
}

/* move one screenful at a time */
void pagemove(int dir)
{
    int page = (LINES - 10 - 1) * 32;
    int nlines = (LINES - 10 - 1);
    switch(dir) {
    case DOWN:
        if(memsize < page || memoffset >= memsize - page) {
            memoffset = memsize - (memsize > 0);
            adjust_screen();
            update_details();
            update_status();
        } else {
            memoffset += page;
            windowOffset += nlines;
            if(windowOffset * 32 > memoffset) {
                windowOffset = memsize / 32;
            }
            redraw();
        }
        break;
    case UP:
        if(memoffset > page) {
            memoffset -= page;
            if(windowOffset > nlines) {
                windowOffset -= nlines;
            } else {
                windowOffset = 0;
            }
            redraw();
        } else {
            memoffset = 0;
            windowOffset = 0;
            redraw();
        }
        break;
    }
}

/* call after moving the cursor, in case we've moved off screen */
void adjust_screen(void)
{
    size_t l = memoffset / 32;
    size_t a = windowOffset;
    size_t b = windowOffset + LINES - 10 - 1;

    if(l < a || l >= b) {
        if(l > ((LINES - 10 - 1) / 2)) {
            windowOffset = l - ((LINES - 10 - 1) / 2);
            if(windowOffset >= memsize / 32) windowOffset = (memsize - 1) / 32;
        } else {
            windowOffset = 0;
        }
        redraw();
    }
}

/* update status line to show detailed cursor position */
void show_detailed_cursor(void)
{
    mvhline(LINES - 1, 0, ' ', COLS);
    attron(A_STANDOUT);
    mvaddch(LINES - 1, 0, '.');
    attroff(A_STANDOUT);
    mvprintw(LINES - 1, 1, " %20lu/%lu bytes  %s nibble",
            memoffset, memsize,
            lownibble ? "low" : "high");

    double dblMemsize = memsize + !memsize;
    int percent = (int)((memoffset + 0.5 * lownibble) * 100.0 / dblMemsize);
    mvprintw(LINES - 1, COLS - 5, "%3d%%", percent);
}

/* update status line to show cursor position */
void update_status(void)
{
    // filename
    if(handle_keys == handle_normal) {
        mvaddch(LINES - 1, 0, '>');
    } else {
        attron(A_STANDOUT);
        mvaddch(LINES - 1, 0, 'A');
        attroff(A_STANDOUT);
    }
    mvaddch(LINES - 1, 1, ' ');
    size_t lll = strlen(fname);
    for(int i = 0; i < COLS - 22 - 2; ++i) {
        // TODO utf-8/unicode in general
        // TODO right align and trim left
        char c = (i < lll) ? fname[i] : ' ';
        mvaddch(LINES - 1, 2 + i, c);
    }

    // file position
    if(memsize > 0xFFFFFFFFul) {
        mvprintw(LINES - 1, COLS - 5 - 16 - 1 - 16, "%016lX/%016lX",
                memoffset, memsize);
    } else {
        mvprintw(LINES - 1, COLS - 5 - 8 - 1 - 8, "%08lX/%08lX",
                memoffset, memsize);
    }

    double dblMemsize = memsize + !memsize;
    int percent = (int)((memoffset + 0.5 * lownibble) * 100.0 / dblMemsize);
    mvprintw(LINES - 1, COLS - 5, "%3d%%", percent);
}

/* update details pane, showing byte interpretations as int, float, string etc */
void update_details(void)
{
    // as bytes
    mvprintw(LINES - 9 - 1, 0, "c: %c", isprint(mem[memoffset]) ? mem[memoffset] : ' ');
    mvprintw(LINES - 9 - 1, 8, "u8: %-3u", mem[memoffset]);
    mvprintw(LINES - 9 - 1, 16, "s8: %-4d", (int)((signed char)mem[memoffset]));
    // as shorts
    if(memoffset <= memsize - 2) {
        unsigned i16le = mem[memoffset] | (mem[memoffset + 1] << 8);
        mvprintw(LINES - 9 - 1, 25, "u16le: %-5u", i16le);
        mvprintw(LINES - 9 - 1, 25 + 13, "s16le: %-6d", (int)((signed short)i16le));
        unsigned i16be = (mem[memoffset] << 8) | mem[memoffset + 1];
        mvprintw(LINES - 9 - 1, 25 + 13 + 14, "u16be: %-5u", i16be);
        mvprintw(LINES - 9 - 1, 25 + 13 + 14 + 13, "s16be: %-6d", (int)((signed short)i16be));
    } else {
        mvhline(LINES - 9 - 1, 25, ' ', COLS - 25);
    }

    // as 32bits
    if(memoffset <= memsize - 4) {
        unsigned i32le = mem[memoffset] | (mem[memoffset + 1] << 8)
                  | (mem[memoffset + 2] << 16) | (mem[memoffset + 3] << 24)
                  ;
        unsigned i32be = mem[memoffset + 3] | (mem[memoffset + 2] << 8)
                  | (mem[memoffset + 1] << 16) | (mem[memoffset + 0] << 24)
                  ;
        mvprintw(LINES - 8 - 1, 0, "u32le: %-10u", i32le);
        mvprintw(LINES - 8 - 1, 18, "s32le: %-11d", (signed)i32le);
        mvprintw(LINES - 8 - 1, 18 + 19, "u32be: %-10u", i32be);
        mvprintw(LINES - 8 - 1, 18 + 19 + 18, "s32be: %-11d", (signed)i32be);

        float f32le = i32tofloat32(i32le);
        float f32be = i32tofloat32(i32be);
        mvprintw(LINES - 3 - 1, 0, "f32le: %-15.7g", f32le);
        mvprintw(LINES - 3 - 1, 40, "f32be: %-15.7g", f32be);
    } else {
        mvhline(LINES - 8 - 1, 0, ' ', COLS);
        mvhline(LINES - 3 - 1, 0, ' ', COLS);
    }
    // as 64bits
    if(memoffset <= memsize - 8) {
        unsigned long i64le = mem[memoffset + 7]; i64le <<= 8;
        i64le |= mem[memoffset + 6]; i64le <<= 8;
        i64le |= mem[memoffset + 5]; i64le <<= 8;
        i64le |= mem[memoffset + 4]; i64le <<= 8;
        i64le |= mem[memoffset + 3]; i64le <<= 8;
        i64le |= mem[memoffset + 2]; i64le <<= 8;
        i64le |= mem[memoffset + 1]; i64le <<= 8;
        i64le |= mem[memoffset + 0];
        unsigned long i64be = mem[memoffset + 0]; i64be <<= 8;
        i64be |= mem[memoffset + 1]; i64be <<= 8;
        i64be |= mem[memoffset + 2]; i64be <<= 8;
        i64be |= mem[memoffset + 3]; i64be <<= 8;
        i64be |= mem[memoffset + 4]; i64be <<= 8;
        i64be |= mem[memoffset + 5]; i64be <<= 8;
        i64be |= mem[memoffset + 6]; i64be <<= 8;
        i64be |= mem[memoffset + 7];
        mvprintw(LINES - 7 - 1, 0, "u64le: %-20lu", i64le);
        mvprintw(LINES - 7 - 1, 28, "s64le: %-21ld", (signed long)i64le);
        mvprintw(LINES - 7 - 1, 57, "h64le: %016lx", i64le);
        mvprintw(LINES - 6 - 1, 0, "u64be: %-20lu", i64be);
        mvprintw(LINES - 6 - 1, 28, "s64be: %-21ld", (signed long)i64be);

        mvprintw(LINES - 6 - 1, 57, "h64be: %016lx", i64be);

        double f64le = i64tofloat64(i64le);
        double f64be = i64tofloat64(i64be);
        mvprintw(LINES - 2 - 1, 0, "f64le: %-30.15lg", f64le);
        mvprintw(LINES - 2 - 1, 40, "f64be: %-30.15lg", f64be);
    } else {
        mvhline(LINES - 7 - 1, 0, ' ', COLS);
        mvhline(LINES - 6 - 1, 0, ' ', COLS);
        mvhline(LINES - 2 - 1, 0, ' ', COLS);
    }

    mvprintw(LINES - 5 - 1, 0, "b64le: ");
    printbinle(LINES - 5 - 1, 8);
    mvprintw(LINES - 4 - 1, 0, "b64be: ");
    printbinbe(LINES - 4 - 1, 8);

    char toPrint[65];
    toPrint[64] = '\0';
    for(int i = 0; i < 64; ++i) {
        toPrint[i] = 
            (memoffset + i < memsize)
            ? isprint(mem[memoffset + i])
              ? mem[memoffset + i]
              : '.' 
            : ' ';
    }
    mvprintw(LINES - 1 - 1, 0, "s: [%s]", toPrint);
}

/* punch in one hex character */
void punch(int ic)
{
    if(memsize == 0) {
        mvhline(LINES - 1, 0, ' ', COLS);
        mvprintw(LINES - 1, 0, "There are no bytes in the file, perhaps insert some with `>' ?");
        return;
    }
    char c = tolower((char)(ic & 0xFF));
    char* p = strchr(HEX, c);
    unsigned char nib = p - HEX;

    unsigned char b = mem[memoffset];

    mem[memoffset] = 
        (lownibble * (nib | (b & 0xF0)))
        |
        ((!lownibble) * ((nib << 4) | (b & 0x0F)))
        ;


    // update screen...
    int l = memoffset / 32 - windowOffset;
    int sb = memoffset % 32;
    int sc = 9 + sb / 4 + sb * 2 + lownibble;
    mvaddch(l, sc, c);
    mymove(RIGHT);
}

/* overwrite memory starting at memoffset */
void overwrite_mem(void* rawbytes, size_t nbytes)
{
    unsigned char* bytes = (unsigned char*)rawbytes;

    if(memoffset + nbytes > memsize) {
        mvhline(LINES - 1, 0, ' ', COLS);
        mvprintw(LINES - 1, 0, "Won't fit!");
        getch();
        return;
    }

    memcpy(mem + memoffset, bytes, nbytes);
}

/* ask the user for formatted data, then punch in said data starting at
   memoffset */
void punch_interpreted(void)
{
    const int S = LINES - 9 - 1;
    for(int i = LINES - 9 - 1; i < LINES - 1; ++i) {
        mvhline(i, 0, ' ', COLS);
    }

    mvprintw(S, 0, "What do you want to punch in?");
    mvprintw(S + 1, 0, "1) u16le 2) u16be 3) u32le 4) u64be");
    mvprintw(S + 2, 0, "5) u64le 6) u64be 7) f32le 8) f32be");
    mvprintw(S + 3, 0, "q) f64le w) f64be e) single character");
    mvprintw(S + 4, 0, "r) enter ASCII input mode");

    char choice = read_key("choice: ", "12345678qwer");

    switch(choice) {
        case '\0':
            break;
        case 'r':
            handle_keys = handle_text;
            break;
        case 'e':
            {
            char c = read_key("ascii character: ",
                    "abcdefghijklmnopqrstuvwxyz"
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                    "`0123456789-=[]|;',./"
                    "~!@#$%^&*()_+{}\\:\"<>?");
            if(c != 0)
                overwrite_mem(&c, 1);
            }
            break;
        default:
            {
                char* s = read_string("enter scanf-friendly number: ");
                if(!s) break;
                
                switch(choice) {
                    case '1':
                        {
                            unsigned short nn;
                            sscanf(s, "%hi", &nn);
                            overwrite_mem(&nn, 2);
                        }
                        break;
                    case '2':
                        {
                            unsigned short nn;
                            sscanf(s, "%hi", &nn);
                            nn = ((nn >> 8) & 0xFFu) | ((nn << 8) & 0xFF00u);
                            overwrite_mem(&nn, 2);
                        }
                        break;
                    case '3':
                        {
                            unsigned int nn;
                            sscanf(s, "%i", &nn);
                            overwrite_mem(&nn, 4);
                        }
                        break;
                    case '4':
                        {
                            unsigned int nn;
                            sscanf(s, "%i", &nn);
                            unsigned char* pn = (unsigned char*)(&nn);
                            unsigned char p[4] = {
                                pn[3], pn[2], pn[1], pn[0]
                            };
                            overwrite_mem(p, 4);
                        }
                        break;
                    case '5':
                        {
                            unsigned long nn;
                            sscanf(s, "%li", &nn);
                            overwrite_mem(&nn, 8);
                        }
                        break;
                    case '6':
                        {
                            unsigned long nn;
                            sscanf(s, "%li", &nn);
                            unsigned char* pn = (unsigned char*)(&nn);
                            unsigned char p[8] = {
                                pn[7], pn[6], pn[5], pn[4],
                                pn[3], pn[2], pn[1], pn[0]
                            };
                            overwrite_mem(p, 8);
                        }
                        break;
                    case '7':
                        {
                            double dn;
                            float nn;
                            // read float as double, otherwise it's impossible
                            // to type in 3.14159
                            sscanf(s, "%lf", &dn);
                            nn = (float)dn;
                            overwrite_mem(&nn, 4);
                        }
                        break;
                    case '8':
                        {
                            double dn;
                            float nn;
                            sscanf(s, "%lf", &dn);
                            nn = (float)dn;
                            unsigned char* pn = (unsigned char*)(&nn);
                            unsigned char p[4] = {
                                pn[3], pn[2], pn[1], pn[0]
                            };
                            overwrite_mem(p, 4);
                        }
                        break;
                    case 'q':
                        {
                            double nn;
                            sscanf(s, "%lf", &nn);
                            overwrite_mem(&nn, 8);
                        }
                        break;
                    case 'w':
                        {
                            double nn;
                            sscanf(s, "%lf", &nn);
                            unsigned char* pn = (unsigned char*)(&nn);
                            unsigned char p[8] = {
                                pn[7], pn[6], pn[5], pn[4],
                                pn[3], pn[2], pn[1], pn[0]
                            };
                            overwrite_mem(p, 8);
                        }
                        break;
                }
                free(s);
            }
            break;
    }

    redraw();
}

/* punch in ASCII characters in text input mode */
void punch_ascii(int c)
{
    int l = memoffset / 32 - windowOffset;
    int sb = memoffset % 32;
    int sc = 9 + sb / 4 + sb * 2 + lownibble;
    mem[memoffset] = c & 0xFF;
    mvaddch(l, sc+0, HEX[(c >> 4) & 0xF]);
    mvaddch(l, sc+1, HEX[(c >> 0) & 0xF]);
    mymove(RIGHT);
    mymove(RIGHT);
}

/* utility to ask the user a yes/no question */
int question(const char* q)
{
    mvhline(LINES - 1, 0, ' ', COLS);
    mvprintw(LINES - 1, 0, "%s (Y/N)", q);

    while(1) {
        int c = getch();
        if(c == 'y' || c == 'Y') return 1;
        if(c == 'N' || c == 'n') return 0;
        if(c == 3) return 0; // int
        if(c == 7) return 0; // bell
        if(c == 27) return 0; // esc
    }
    return 0;
}

/* show help screen, paginated */
void myhelp(void)
{
	clear();
    mvprintw(0, 0, "jakhex %s", VERSION);
    mvprintw(1, COLS - 13, "By Vlad Mesco");
    int n = 2;
	for(int i = 0; i < sizeof(HELP)/sizeof(HELP[0]); ++i, ++n) {
        if(n >= LINES - 1) {
            mvprintw(n, 0, "Press any key to continue...");
            getch();
            n = 0;
            clear();
        }
		mvprintw(n, 0, "%s", HELP[i]);
	}
    mvprintw(n, 0, "Press any key to continue...");
    getch();
    redraw();
}

/* print a number as binary */
void printbin(int l, int c, unsigned long N)
{
    for(int i = 0; i < 8; ++i) {
        for(int j = 0; j < 8; ++j) {
            mvaddch(l, c + j, ((N & 0x8000000000000000ul) != 0) + '0');
            N <<= 1;
        }
        c += 9;
    }
}

/* print a little endian number as binary, MSB to the left */
void printbinle(int l, int c)
{
    unsigned long N = 0;
    for(int i = 0; i < 8; ++i) {
        if(memoffset + i >= memsize) break;
        N |= (unsigned long)mem[memoffset + i] << (i * 8ul);
    }
    printbin(l, c, N);
}

/* print a big endian number as binary, MSB to the left */
void printbinbe(int l, int c)
{
    unsigned long N = 0;
    for(int i = 0; i < 8; ++i) {
        if(memoffset + i >= memsize) break;
        N |= (unsigned long)mem[memoffset + i] << ((7ul - i) * 8ul);
    }
    printbin(l, c, N);
}

/* handle keyboard presses in normal hex/command mode */
void handle_normal(int c)
{
    // single character codes...
    switch(c) {
        case 'Q': finish(SIGTERM); break;
        case 'q':
                  if(question("Exit without saving?")) {
                      finish(SIGTERM);
                  } else {
                      update_status();
                  }
                  break;
        case 12: redraw(); break;// ^L
        case 26: kill(getpid(), SIGTSTP); break; // ^Z
        case ' ':
        case KEY_RIGHT:
        case 'l': mymove(RIGHT); break;
        case KEY_LEFT:
        case KEY_BACKSPACE:
        case 127: // delete
        case 8: // ^H
        case 'h': mymove(LEFT); break;
        case KEY_DOWN:
        case 'j': mymove(DOWN); break;
        case KEY_UP:
        case 'k': mymove(UP); break;
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7':
        case '8': case '9': case 'a': case 'b':
        case 'c': case 'd': case 'e': case 'f':
                  punch(c); break;
        case ':':
        case '.':
                  punch_interpreted();
                  break;
        case '>':
                  insert_nulls(memoffset + 1);
                  break;
        case '<':
                  insert_nulls(memoffset);
                  break;
        case 'r':
                  insert_file(memoffset);
                  break;
        case 'R':
                  insert_file(memoffset + 1);
                  break;
        case 11: // ^K
                  truncate_file(memoffset);
                  break;
        case 'g':
                  goto_command();
                  break;
        case '+':
                  advance_offset(+1);
                  break;
        case '-':
                  advance_offset(-1);
                  break;
        case KEY_F(1): myhelp(); break;
        case 6:
        case ')':
        case KEY_NPAGE: pagemove(DOWN); break;
        case '(':
        case 2:
        case KEY_PPAGE: pagemove(UP); break;
        case KEY_HOME:
                        memoffset = 0;
                        windowOffset = 0;
                        lownibble = 0;
                        redraw();
                        break;
        case KEY_END:
                        memoffset = memsize - 1;
                        lownibble = 1;
                        adjust_screen();
                        update_details();
                        update_status();
                        break;
        case KEY_MOUSE:
                        {
                            MEVENT event;
                            if(OK == getmouse(&event)) {
                                // handle mouse
                            }
                        }
                        break;
        case 'w':
        case 19:
        case KEY_F(2):
                        save_file();
                        break;
        case 'o':
        case 15:
        case KEY_F(3):
                        open_file();
                        break;
        case 'i':
        case KEY_F(4):
                        // disable lownibble as we're punching in bytes
                        lownibble = 0;
                        // switch keyboard handling routine
                        handle_keys = handle_text;
                        update_status();
                        break;
        case '/':
                        find_forward(1);
                        break;
        case 'n':
                        find_forward(0);
                        break;
        case '?':
                        find_backward(1);
                        break;
        case 'N':
                        find_backward(0);
                        break;
        case 'm':
                        set_marker();
                        break;
        case 'M':
                        list_markers();
                        break;
        case '\'':
        case '`':
        case 'G':
                        goto_marker();
                        break;
        case '@':
                        blank_region();
                        break;
        case 'x':
                        kill_region();
                        break;
        case 'y':
                        yank_region();
                        break;
        case 'p':
                        paste_clipboard(memoffset);
                        break;
        case 'P':
                        paste_clipboard(memoffset + 1);
                        break;
        case '*':
                        overwrite_clipboard();
                        break;
        case 'W':
                        write_region();
                        break;
        case 7: // ^G
                        show_detailed_cursor();
                        break;

    }
}

/* handle keyboard input in text entry mode */
void handle_text(int c)
{
    lownibble = 0; // we operate on full bytes
    // handle printable characters
    if(isprint(c)) {
        punch_ascii(c);
        lownibble = 0;
        return;
    }

    // handle other keys
    switch(c)
    {
        case 0: // ^@
        case ' ':
        case 9:
        case 10: // ^J
        case 13: // ^M
            punch_ascii(c);
            break;
        case KEY_ENTER:
            punch_ascii(10);
            break;

        case KEY_F(4):
        case 3: // ^C
        case 4: // ^D
        case 27: // ^[
        case 7: // ^G
            handle_keys = handle_normal;
            update_status();
            break;

        case KEY_F(1):
            myhelp();
            break;
        case KEY_BACKSPACE:
        case 127: // delete
        case 8: // ^H
        case KEY_LEFT:
            mymove(LEFT);
            mymove(LEFT);
            break;
        case KEY_RIGHT:
            mymove(RIGHT);
            mymove(RIGHT);
            break;
        case KEY_UP:
            mymove(UP);
            break;
        case KEY_DOWN:
            mymove(DOWN);
            break;
        case 6:
        case KEY_NPAGE:
            pagemove(DOWN);
            break;
        case 2:
        case KEY_PPAGE:
            pagemove(UP);
            break;
        case KEY_HOME:
            memoffset = 0;
            windowOffset = 0;
            lownibble = 0;
            redraw();
            break;
        case KEY_END:
            memoffset = memsize - 1;
            lownibble = 0;
            adjust_screen();
            update_details();
            update_status();
            break;
        case 19:
        case KEY_F(2):
             save_file();
             break;
        case 15:
        case KEY_F(3):
             open_file();
             break;
        default:
            mvhline(LINES - 1, 0, ' ', COLS);
            mvprintw(LINES - 1, 0, "Key not available in text punchin mode");
            break;
    }
    lownibble = 0;
}

/* insert nbytes NULL bytes before `before'; this grows the buffer by nbytes */
void insert_n_nulls(size_t before, size_t nbytes)
{
    if(nbytes == 0) return;
    if(memcapacity < memsize + nbytes) {
        size_t byThisMuch = nbytes;
        if(byThisMuch < 64ul * 1024ul) byThisMuch = 1024ul;
        mem = realloc(mem, memcapacity + byThisMuch);
        if(!mem) abort();
        memcapacity += byThisMuch;
    }
    memmove(mem + before + nbytes, mem + before, memsize - before);
    memset(mem + before, 0, nbytes);
    memsize += nbytes;
}

/* insert command; prompt the user how many bytes to insert */
void insert_nulls(size_t before)
{
    if(before > memsize) before = memsize;
    mvhline(LINES - 1, 0, ' ', COLS);
    mvprintw(LINES - 1, 0, "How many bytes? ");
    char buf[64];
    int nbuf = 0;
    int col = 17;
    int out = 0;
    while(!out) {
        int c = mvgetch(LINES - 1, col + nbuf);
        switch(c) {
            case '0': case '1': case '2': case '4': case '5':
            case '6': case '7': case '8': case '9': case '3':
                if(nbuf > 63) break;
                buf[nbuf++] = c & 0xFF;
                break;
            case KEY_BACKSPACE:
            case 127: // delete
            case 8: // ^H
            case KEY_LEFT:
                nbuf--;
                if(nbuf < 0) nbuf = 0;
                break;
            case KEY_ENTER:
            case 13:
            case 10:
                out = 1;
                break;
            case 27:
            case 7:
            case 3:
                nbuf = 0;
                out = 1;
                break;
        }

        buf[nbuf] = '\0';
        mvhline(LINES - 1, col, ' ', COLS - col);
        mvprintw(LINES - 1, col, "%s", buf);
    }

    if(nbuf == 0) {
        update_status();
        return;
    }

    long nbytes = atol(buf);

    insert_n_nulls(before, nbytes);

    redraw();
}

/* truncate file at position `at' */
void truncate_file(size_t at)
{
    memsize = at;
    memoffset = (at > 0) ? at - 1 : 0;
    lownibble = 0;
    redraw();
}

/* prompt the user to press one of the `allowed' keys;
   ^C, ^G and ESC cancel out, returning NULL.
   `allowed' keys return themselves.
   Everything else keeps you in a loop */
char read_key(const char* prompt, const char* allowed)
{
    mvhline(LINES - 1, 0, ' ', COLS);
    mvprintw(LINES - 1, 0, "%s", prompt);
    int col = strlen(prompt);
    while(true) {
        int c = mvgetch(LINES - 1, col);
        switch(c) {
            case 3:
            case 7:
            case 27:
                return 0;
            default:
                if(c >= 0 && c < 128 &&
                        strchr(allowed, (char)(c & 0xFF)) != NULL)
                {
                    return c;
                }
                break;
        }
    }
}

/* prompt the user to input a string, terminated by LF or CR.
   the ending CR and/or LF are discarded.
   returns NULL if the prompt was cancelled with ^C, ^G, ESC */
char* read_string(const char* prompt)
{
    mvhline(LINES - 1, 0, ' ', COLS);
    mvprintw(LINES - 1, 0, "%s", prompt);
    char buf[4096];
    int nbuf = 0;
    buf[0] = '\0';
    mvprintw(LINES - 1, 0, "%s", buf);
    int col = strlen(prompt);
    int out = 0;
    while(!out) {
        int c = mvgetch(LINES - 1, col + nbuf);
        switch(c) {
            case 3:
            case 7:
            case 27:
                nbuf = 0;
                out = 1;
                break;
            case KEY_ENTER:
            case 10:
            case 13:
                out = 1;
                break;
            case KEY_BACKSPACE:
            case 127: // delete
            case 8: // ^H
            case KEY_LEFT:
                nbuf--;
                if(nbuf < 0) nbuf = 0;
                break;
            default:
                if(c >= 0 && c < 256 && nbuf < 4095) {
                    buf[nbuf++] = c;
                }
                break;
        }
        buf[nbuf] = '\0';

        mvhline(LINES - 1, col, ' ', COLS - col);
        mvprintw(LINES - 1, col, "%s", buf);
    }

    if(nbuf == 0) return NULL;
    return mystrdup(buf);
}

/* prompt the user for a file name. Updates `fname'. Prepopulates the
   field with `fname'. */
char* read_filename(void)
{
    mvhline(LINES - 1, 0, ' ', COLS);
    char buf[4096];
    int nbuf = 0;
    buf[0] = '\0';
    strcpy(buf, fname);
    nbuf = strlen(buf);
    mvprintw(LINES - 1, 0, "%s", buf);
    int col = 0;
    int out = 0;
    while(!out) {
        int c = mvgetch(LINES - 1, col + nbuf);
        switch(c) {
            case 3:
            case 7:
            case 27:
                nbuf = 0;
                out = 1;
                break;
            case KEY_ENTER:
            case 10:
            case 13:
                out = 1;
                break;
            case KEY_BACKSPACE:
            case 127: // delete
            case 8: // ^H
            case KEY_LEFT:
                nbuf--;
                if(nbuf < 0) nbuf = 0;
                break;
            default:
                if(c >= 0 && c < 256 && nbuf < 4095) {
                    buf[nbuf++] = c;
                }
                break;
        }
        buf[nbuf] = '\0';

        mvhline(LINES - 1, col, ' ', COLS - col);
        mvprintw(LINES - 1, col, "%s", buf);
    }

    if(nbuf == 0) return NULL;
    return mystrdup(buf);
}

/* Save the full buffer to a file. Calls `read_filename()' */
void save_file(void)
{
    char* buf = read_filename();
    update_status();
    if(!buf) return;

    free(fname);
    fname = mystrdup(buf);

    FILE* f = fopen(buf, "wb");
    if(!f) {
        mvhline(LINES - 1, 0, ' ', COLS);
        mvprintw(LINES - 1, 0, "Failed to open %s for writing: %s", buf, strerror(errno));
        return;
    }

    size_t written = fwrite(mem, 1, memsize, f);
    fclose(f);
    if(written == memsize) {
        mvhline(LINES - 1, 0, ' ', COLS);
        mvprintw(LINES - 1, 0, "Wrote %zd bytes", written);
    } else {
        if(ferror(f)) {
            mvhline(LINES - 1, 0, ' ', COLS);
            mvprintw(LINES - 1, 0, "Failed to write %s: %s", buf, strerror(errno));
            return;
        } else {
            mvhline(LINES - 1, 0, ' ', COLS);
            mvprintw(LINES - 1, 0, "Wrote %zd out of %zd bytes", written, memsize);
            return;
        }
    }
}

/* opens `fname' and loads data into the buffer */
void open_file1(void)
{
    FILE* f = fopen(fname, "rb");
    if(!f) {
        mvhline(LINES - 1, 0, ' ', COLS);
        mvprintw(LINES - 1, 0, "Failed to open %s for reading: %s", fname, strerror(errno));
        return;
    }

    fseek(f, 0L, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0L, SEEK_SET);

    unsigned char* newmem = malloc(sz);
    if(!newmem) abort();

    size_t haveread = fread(newmem, 1, sz, f);

    fclose(f);

    mvhline(LINES - 1, 0, ' ', COLS);
    mvprintw(LINES - 1, 0, "Read %zd bytes", haveread);

    free(mem);
    mem = newmem;
    memsize = haveread;
    memcapacity = sz;
    memoffset = 0;
    windowOffset = 0;
    redraw();

    mvhline(LINES - 1, 0, ' ', COLS);
    mvprintw(LINES - 1, 0, "Read %zd bytes", haveread);
}

/* Loads a file into the buffer, discarding existing data.
   Calls `read_filename()' */
void open_file(void)
{
    char* buf = read_filename();
    update_status();
    if(!buf) return;

    free(fname);
    fname = buf;

    return open_file1();
}

/* Loads a file into the buffer at the specified curosr position.
   This inserts N bytes where N is the length of the file. The buffer
   grows by that much.  Calls `read_filename()' */
void insert_file(size_t before)
{
    if(before > memsize) before = memsize;
    char* buf = read_filename();
    update_status();
    if(!buf) return;

    free(fname);
    fname = mystrdup(buf);

    FILE* f = fopen(buf, "rb");
    if(!f) {
        mvhline(LINES - 1, 0, ' ', COLS);
        mvprintw(LINES - 1, 0, "Failed to open %s for reading: %s", buf, strerror(errno));
        free(buf);
        return;
    }

    fseek(f, 0L, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0L, SEEK_SET);

    insert_n_nulls(before, sz);

    size_t haveread = fread(mem + before, 1, sz, f);

    fclose(f);
    free(buf);

    redraw();

    mvhline(LINES - 1, 0, ' ', COLS);
    mvprintw(LINES - 1, 0, "Read %zd bytes", haveread);
}

/* prompts the user for an address and jumps to it */
void goto_command(void)
{
    char* s = read_string("Address: ");
    update_status();
    if(!s) return;
    ssize_t addr;
    sscanf(s, "%zi", &addr);
    free(s);
    if(addr >= 0 && addr < memsize) {
        memoffset = addr;
        adjust_screen();
        update_details();
        update_status();
    } else if(addr < 0 && (ssize_t)memsize + addr >= 0) {
        size_t a = (size_t)(-addr);
        memoffset = memsize - a;
        adjust_screen();
        update_details();
        update_status();
    }
}

/* prompts the user for an offset and jumps to memoffset + sign * amount */
void advance_offset(long sign)
{
    if(memsize == 0) return;
    char* s = read_string("Offset: ");
    update_status();
    if(!s) return;
    long addr;
    sscanf(s, "%li", &addr);
    free(s);
    long adjusted = ((long)memoffset) + sign * addr;
    while(adjusted < 0l) {
        adjusted += memsize;
    }
    while(adjusted >= memsize) {
        adjusted -= memsize;
    }
    memoffset = adjusted;
    adjust_screen();
    update_details();
    update_status();
}

void continue_find_cb(
        unsigned char* from, size_t nfrom,
        void* (*cb_search)(void*, size_t, void*, size_t))
{
    if(!nSearchString || !searchString) return;

    unsigned char* p = cb_search(from, nfrom,
                                 searchString, nSearchString);
    if(p) {
        memoffset = p - mem;
        adjust_screen();
        update_details();
        update_status();
    } else {
        mvhline(LINES - 1, 0, ' ', COLS);
        mvprintw(LINES - 1, 0, "Not found");
    }
}

void save_search_string(void* s, size_t len)
{
    unsigned char* tosave = s;
    nSearchString = len;
    free(searchString);
    searchString = malloc(nSearchString);
    if(!searchString) {
        // ignore the malloc error, it's fine to not save it
        nSearchString = 0;
        return;
    }
    memcpy(searchString, tosave, len);
}

/* implementation of find forwards/backwards. Uses memsearch/rmemsearch.
   updates memoffset if anything is found. This does not loop around.  */
void find_cb(
        unsigned char* from, size_t nfrom,
        void* (*cb_search)(void*, size_t, void*, size_t))
{
    if(memsize == 0) return;
    char* s = read_string("? ");

    if(!s || !*s) return;

    if(s[0] == 't') {
        save_search_string(s+1, strlen(s) - 1);
        continue_find_cb(from, nfrom, cb_search);
    } else {
        unsigned char* needle = malloc(1024);
        size_t cneedle = 1024, sneedle = 0;
        char* p = s, *end = s + strlen(s);
        do {
            while(*p == ' ' || *p == '\t') ++p;
            // should be able to grab two chars
            if(*p != '\0' && p >= end - 1) {
                goto invalid_format;
            }
            char c1 = *p, c2 = *(p + 1);
            p += 2;
            char *pc1 = strchr(HEX, tolower(c1));
            char *pc2 = strchr(HEX, tolower(c2));
            if(!pc1 || !pc2)
            {
                goto invalid_format;
            }
            unsigned char uc1 = pc1 - HEX;
            unsigned char uc2 = pc2 - HEX;

            if(sneedle >= cneedle) {
                cneedle *= 2;
                needle = realloc(needle, cneedle);
                if(!needle) {
                    abort();
                }
            }
            needle[sneedle++] = (uc1 << 4) | uc2;
        } while(p < end);

        save_search_string(needle, sneedle);
        continue_find_cb(from, nfrom, cb_search);
    }

    return;

invalid_format:
    mvhline(LINES - 1, 0, ' ', COLS);
    mvprintw(LINES - 1, 0, "Invalid format");
    free(s);
}

/* prompts the user for a string and searches for the next occurrence.
   This does not wrap around.
   If prompt == 1, asks the user for a search string. */
void find_forward(int prompt)
{
    if(memsize == 0 || memoffset == memsize - 1) return;
    unsigned char* from = mem + memoffset + 1;
    size_t nfrom = memsize - memoffset - 1;
    if(prompt)
        return find_cb(from, nfrom, memsearch);
    else
        return continue_find_cb(from, nfrom, memsearch);
}

/* prompts the user for a string and searches for the previous occurrence.
   This does not wrap around.
   If prompt == 1, asks the user for a search string. */
void find_backward(int prompt)
{
    if(memsize == 0 || memoffset == 0) return;
    unsigned char* from = mem;
    size_t nfrom = memoffset;
    if(prompt)
        return find_cb(from, nfrom, rmemsearch);
    else
        return continue_find_cb(from, nfrom, rmemsearch);
}

/* save an address in one of the 26 registers */
void set_marker(void)
{
    if(memsize == 0) return;

    char c = read_key("Which marker? ", "abcdefghijklmnopqrstuvwxyz");

    if(c == 0) return;

    int i = c - 'a';

    markers[i] = memoffset;

    update_status();
}

/* list addressess of all 26 registers */
void list_markers(void)
{
    clear();
    int n = 0;
    for(int i = 0; i < 26; ++i) {
        if(n >= LINES - 1) {
            mvprintw(LINES - 1, 0, "Press any key to continue....");
            getch();
            n = 0;
        }
        mvprintw(n, 0, "%c: %016lx %ld", i + 'a', markers[i], markers[i]);
        ++n;
    }

    mvprintw(LINES - 1, 0, "Press any key to continue....");
    getch();
    redraw();
}

/* jump to the address stored in a marker */
void goto_marker(void)
{
    if(memsize == 0) return;

    char c = read_key("Which marker? ", "abcdefghijklmnopqrstuvwxyz");

    if(c == 0) return;

    int i = c - 'a';

    size_t addr = markers[i];

    if(addr < memsize) {
        memoffset = addr;
        adjust_screen();
        update_details();
        update_status();
    }
}

/* ask the user for a pair of markers. Returns the first and second
   addresses such that *pa1 <= *pa2 */
int read_pair_of_markers(size_t* pa1, size_t* pa2)
{
    char c1 = read_key("First marker: ", "abcdefghijklmnopqrstuvwxyz");
    if(!c1) return 0;
    char c2 = read_key("Second marker: ", "abcdefghijklmnopqrstuvwxyz");
    if(!c2) return 0;

    int m1 = c1 - 'a';
    int m2 = c2 - 'a';

    size_t a1 = markers[m1];
    size_t a2 = markers[m2];
    if(a1 >= memsize) a1 = memsize;
    if(a2 >= memsize) a2 = memsize;

    if(a2 < a1) {
        size_t t = a1;
        a1 = a2;
        a2 = t;
    }

    *pa1 = a1;
    *pa2 = a2;

    return 1;
}

/* ask the user for a pair of markers and replaces bytes in that region with NULLs */
void blank_region(void)
{
    if(memsize == 0) return;
    size_t a1, a2;
    if(!read_pair_of_markers(&a1, &a2)) return;

    memset(mem + a1, 0, a2 - a1 + 1);

    redraw();
}

/* ask the user for a pair of markers, and removes those bytes. The buffer
   shrinks by a2-a1+1 bytes */
void kill_region(void)
{
    if(memsize == 0) return;
    size_t a1, a2;
    if(!read_pair_of_markers(&a1, &a2)) return;

    memmove(mem + a1, mem + a2 + 1, memsize - a2 - 1);
    memsize -= a2 - a1 + 1;

    memoffset = a1;
    if(memoffset > 0) --memoffset;

    adjust_screen();
    redraw();
}

/* ask the user for a pair of markers and copies that to a hidden buffer */
void yank_region(void)
{
    if(memsize == 0) return;
    size_t a1, a2;
    if(!read_pair_of_markers(&a1, &a2)) return;

    free(clipboard);
    clipboard = malloc(a2 - a1 + 1);
    clipboardsize = a2 - a1 + 1;
    memcpy(clipboard, mem + a1, a2 - a1 + 1);

    mvprintw(LINES - 1, 0, "%zd bytes copied to clipboard!", a2 - a1 + 1);
}

/* ask the user for a pair of markers and a file name, 
   and saves the memory from a1 to a2 to a file. */
void write_region(void)
{
    if(memsize == 0) return;
    size_t a1, a2;
    if(!read_pair_of_markers(&a1, &a2)) return;

    char* buf = read_filename();
    update_status();
    if(!buf) return;

    free(fname);
    fname = mystrdup(buf);

    FILE* f = fopen(buf, "wb");
    if(!f) {
        mvhline(LINES - 1, 0, ' ', COLS);
        mvprintw(LINES - 1, 0, "Failed to open %s for writing: %s", buf, strerror(errno));
        return;
    }

    size_t written = fwrite(mem + a1, 1, a2 - a1 + 1, f);
    fclose(f);
    if(written == a2 - a1 + 1) {
        mvhline(LINES - 1, 0, ' ', COLS);
        mvprintw(LINES - 1, 0, "Wrote %zd bytes", written);
    } else {
        if(ferror(f)) {
            mvhline(LINES - 1, 0, ' ', COLS);
            mvprintw(LINES - 1, 0, "Failed to write %s: %s", buf, strerror(errno));
            return;
        } else {
            mvhline(LINES - 1, 0, ' ', COLS);
            mvprintw(LINES - 1, 0, "Wrote %zd out of %zd bytes", written, a2 - a1 + 1);
            return;
        }
    }
}

/* insert the contents of the hidden buffer at the specified location */
void paste_clipboard(size_t before)
{
    if(before > memsize) before = memsize;

    insert_n_nulls(before, clipboardsize);

    memcpy(mem + before, clipboard, clipboardsize);

    redraw();

    mvhline(LINES - 1, 0, ' ', COLS);
    mvprintw(LINES - 1, 0, "Inserted %zd bytes", clipboardsize);
}

/* overwrites memory starting at memoffset with the contents of the hidden buffer */
void overwrite_clipboard(void)
{
    if(memoffset + clipboardsize > memsize) {
        mvhline(LINES - 1, 0, ' ', COLS);
        mvprintw(LINES - 1, 0, "Not enough room to paste %zd bytes", clipboardsize);
        return;
    }

    memcpy(mem + memoffset, clipboard, clipboardsize);

    redraw();

    mvhline(LINES - 1, 0, ' ', COLS);
    mvprintw(LINES - 1, 0, "Punched over %zd bytes", clipboardsize);
}
