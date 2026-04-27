#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include <character.h>
#include <keyboard.h>

#define TERM_BG 0x00000000
#define TERM_FG 0x00ffffff
#define MENUBAR_H 24
#define TERM_Y_OFFSET MENUBAR_H

#define INPUT_MAX 256
static char input_buf[INPUT_MAX];
static size_t input_len = 0;

static size_t term_col = 0;
static size_t term_row = 0;
static size_t term_scale = 2;

// Set the base revision to 6, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

// Halt and catch fire function.
static void hcf(void) {
    for (;;) {
        asm ("hlt");
    }
}

static void put_pixel(struct limine_framebuffer *fb, size_t x, size_t y, uint32_t color){
    volatile uint32_t *fb_ptr = fb->address;
    fb_ptr[y * (fb->pitch / 4) + x] = color;
}

static void draw_char(struct limine_framebuffer *fb, char c,
                      size_t x, size_t y, uint32_t color, size_t scale) {
    const uint8_t *glyph = font[(uint8_t)c];

    for (size_t row = 0; row < 8; row++) {
        for (size_t col = 0; col < 8; col++) {
            if (glyph[row] & (0x80 >> col)) {
                for (size_t sy = 0; sy < scale; sy++) {
                    for (size_t sx = 0; sx < scale; sx++) {
                        put_pixel(fb, x + col * scale + sx, y + row * scale + sy, color);
                    }
                }
            }
        }
    }
}

static void draw_string(struct limine_framebuffer *fb, const char *s,
                        size_t x, size_t y, uint32_t color, size_t scale) {
    while (*s) {
        draw_char(fb, *s, x, y, color, scale);
        x += 8 * scale;
        s++;
    }
}

static void clear_screen(struct limine_framebuffer *fb, uint32_t color) {
    for (size_t y = 0; y < fb->height; y++){
    for (size_t x = 0; x < fb->width; x++){
        put_pixel(fb, x, y, color);
    }
    }
}

static void terminal_putchar(struct limine_framebuffer *fb, char c){
    size_t w = 8 * term_scale;
    size_t h = 10 * term_scale;

    size_t cols = fb->width / w;
    size_t rows = (fb->height - MENUBAR_H) / h;

    if (c == '\n'){
        term_col = 0;
        ++term_row;
    }
    else{
        draw_char(fb, c, term_col * w, MENUBAR_H + term_row * h, TERM_FG, term_scale);
        ++term_col;
        if (term_col >= cols){
            term_col = 0;
            ++term_row;
        }
    }
    if (term_row >= rows){
        clear_screen(fb, TERM_BG);
        term_col = 0;
        term_row = 0;
    }
}

static void terminal_write(struct limine_framebuffer *fb, const char *s){
    while (*s) {
        terminal_putchar(fb, *s++);
    }
}

static void draw_rect(struct limine_framebuffer *fb, size_t x, size_t y, size_t w, size_t h, uint32_t color){
    for (size_t yy = 0; yy < h; ++yy){
        for (size_t xx = 0; xx < w; ++xx){
            put_pixel(fb, x + xx, y + yy, color);
        }
    }
}
static void draw_topbar(struct limine_framebuffer *fb) {
    draw_rect(fb, 0, 0, fb->width, MENUBAR_H, 0x00222222);

    draw_string(fb,
        "adi-os   programs   mem   time   help",
        6, 2,
        0x00ffffff,
        2);
}
static void terminal_draw_cursor(struct limine_framebuffer *fb){
    size_t char_w = 8 * term_scale;
    size_t char_h = 10 * term_scale;

    draw_rect(fb, term_col * char_w, MENUBAR_H + term_row * char_h, char_w, char_h, TERM_FG);
}

static void terminal_erase_cursor(struct limine_framebuffer *fb){
    size_t char_w = 8 * term_scale;
    size_t char_h = 10 * term_scale; 

    draw_rect(fb, term_col * char_w, MENUBAR_H + term_row * char_h, char_w, char_h, TERM_BG);
}

static int streq(const char *a, const char *b) {
    while (*a && *b){
        if (*a != *b){return 0;}
        a++; b++;
    }
    return *a == *b;
}

static void shell_execute(struct limine_framebuffer *fb, const char *cmd){
    if (streq(cmd, "help")){terminal_write(fb, "commands: help, clear, echo\n");}
    else if (streq(cmd, "clear")){clear_screen(fb, TERM_BG); draw_topbar(fb); term_col = 0; term_row = 0;}
    else if (cmd[0] == 0){}
    else{
        terminal_write(fb, "unknown command: ");
        terminal_write(fb, cmd);
        terminal_putchar(fb, '\n');
    }
}

// The following will be our kernel's entry point.
// If renaming kmain() to something else, make sure to change the
// linker script accordingly.
void kmain(void) {
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    clear_screen(framebuffer, TERM_BG);
    draw_topbar(framebuffer);    

    terminal_write(framebuffer, "cronk booted.\n");
    terminal_write(framebuffer, "framebuffer initialized\n");
    terminal_write(framebuffer, "welcome to terminal\n");
    terminal_write(framebuffer, "> ");
    terminal_draw_cursor(framebuffer);
    keyboard_init();

    while(1){
        char c = keyboard_poll_char();

        if(!c){
            asm volatile("pause");
            continue;
        }
        terminal_erase_cursor(framebuffer);
        if (c=='\n') {
            input_buf[input_len] = 0;
            
            terminal_putchar(framebuffer, '\n');
            shell_execute(framebuffer, input_buf);
            
            input_len = 0;
            terminal_write(framebuffer, "> " );
        }
        else if (c=='\b'){
            if (input_len > 0) {
                --input_len;
                input_buf[input_len] = 0;
                if (term_col > 2) {
                    --term_col;
                    size_t char_w = 8 * term_scale;
                    size_t char_h = 10 * term_scale;
                    draw_rect(framebuffer, term_col * char_w, term_row * char_h, char_w, char_h, TERM_BG);
                }
            }
        }
        else {
            if (input_len < INPUT_MAX - 1){
                input_buf[input_len++] = c;
                terminal_putchar(framebuffer, c);
            }
        }
        
        terminal_draw_cursor(framebuffer);
    }
}
