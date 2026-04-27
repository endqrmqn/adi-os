#include "keyboard.h"

#define KEYBOARD_DATA_PORT      0x60
#define KEYBOARD_STATUS_PORT    0x64

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static char scancode_to_ascii[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0',

    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p',

    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l',

    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',

    [0x39] = ' ',
    [0x1C] = '\n',
    [0x0E] = '\b',
};

void keyboard_init(void){
}

char keyboard_poll_char(void) {
    uint8_t status = inb(KEYBOARD_STATUS_PORT);

    if ((status & 1) == 0){
        return 0;    
    }

    uint8_t sc = inb(KEYBOARD_DATA_PORT);
    
    //keyboard press ignore
    if (sc & 0x80) {
        return 0;
    }
    
    if (sc >= 128) {
        return 0;
    }

    return scancode_to_ascii[sc];
}
