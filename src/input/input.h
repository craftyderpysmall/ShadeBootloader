/* Keyboard + mouse/touchpad/tablet input, unified into one event stream. */
#ifndef SHADE_INPUT_H
#define SHADE_INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EV_KEY,
    EV_POINTER_MOVE,
    EV_POINTER_DOWN,
    EV_POINTER_UP,
} EventType;

typedef struct {
    EventType type;
    uint16_t scan;      /* SCAN_* for special keys */
    uint16_t ch;        /* unicode char, 0 if none */
    bool shift;
    int x, y;           /* pointer position (screen px) */
} Event;

void input_init(int screen_w, int screen_h, float mouse_speed);
bool input_poll(Event *ev);
/* Sleep until input arrives or timeout_ms passes. */
void input_wait(int timeout_ms);
bool input_has_pointer(void);

#endif
