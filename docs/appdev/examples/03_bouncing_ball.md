<div align="center">
  <h1>Example 03: Bouncing Ball</h1>
  <p><em>Animating graphics and managing application state.</em></p>
</div>

---

This example builds upon the `02_basic_window` guide. It demonstrates how to constantly update the screen to simulate a bouncing square moving freely inside the window bounds.

## Concepts Introduced
* Maintaining application state across frames (Velocity/Position).
* Drawing primitives (`ui_fill_rect`, `ui_draw_string`).
* The importance of clearing the screen on a new frame.
* Explicitly forcing standard visual updates via `ui_mark_dirty()`.
* Declaring app metadata via source annotations.

---

## The Code (`src/userland/gui/bounce.c`)

```c
// BOREDOS_APP_DESC: Bouncing ball animation demo.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/applications-games.png
#include <stdlib.h>
#include <libui.h>
#include <syscall.h>

// Window Dimensions
#define W_WIDTH  400
#define W_HEIGHT 300
// Square Dimensions
#define SQ_SIZE  30

int main(void) {
    ui_window_t wid = ui_window_create("Bouncing Box Animation", 50, 50, W_WIDTH, W_HEIGHT);
    if (wid < 0) return 1;

    // Define object state variables
    int pos_x = 50;
    int pos_y = 50;
    int vel_x = 2; // Move 2 pixels per frame horizontally
    int vel_y = 2; // Move 2 pixels per frame vertically

    gui_event_t event;
    while (1) {
        // 1. Process Events
        while (ui_get_event(wid, &event)) {
            if (event.type == GUI_EVENT_CLOSE) {
                return 0; // Exit cleanly
            }
        }

        // 2. Physics & Logic Update
        pos_x += vel_x;
        pos_y += vel_y;

        // Collision logic (Bounce off edges)
        // The window has a 20px title bar, so the usable client height is W_HEIGHT - 20.
        if (pos_x <= 0 || pos_x + SQ_SIZE >= W_WIDTH) {
            vel_x = -vel_x; // Reverse horizontal direction
        }
        if (pos_y <= 0 || pos_y + SQ_SIZE >= W_HEIGHT - 20) {
            vel_y = -vel_y; // Reverse vertical direction
        }

        // 3. Rendering Update
        // Step A: Clear the entire background to Black (0xFF000000)
        ui_draw_rect(wid, 0, 0, W_WIDTH, W_HEIGHT, 0xFF000000);

        // Step B: Draw our shape in Red (0xFFFF0000) at the new position
        ui_draw_rect(wid, pos_x, pos_y, SQ_SIZE, SQ_SIZE, 0xFFFF0000);
        
        // Step C: Draw some UI text over the animation in White
        ui_draw_string(wid, 10, 10, "BoredOS Animation Demo!", 0xFFFFFFFF);

        // Step D: Instruct the compositor to flush our drawing buffer to the physical screen
        ui_mark_dirty(wid, 0, 0, W_WIDTH, W_HEIGHT);

        // 4. Yield and throttle
        sys_yield();
    }

    return 0;
}
```

## How it Works

1.  **State Management**: We store `pos_x`, `pos_y`, `vel_x`, and `vel_y`. These variables represent the "physics" of our system. Notice that they update *outside* the event-checking logic so that the animation runs even if the user isn't clicking the mouse.
2.  **Screen Clearing**: We *must* fill the screen with black (`ui_draw_rect(wid, 0, 0, W_WIDTH, W_HEIGHT, ...)`). If we don't clear the screen, the red square will leave a permanent trailing smear everywhere it goes!
3.  **The Double Buffer**: `ui_draw_rect` and `ui_draw_string` do not immediately appear on your monitor. They just color a hidden buffer within the kernel.
4.  **`ui_mark_dirty`**: This is the crucial command that tells the kernel Window Manager, "I'm done drawing my frame. Can you quickly copy my hidden buffer over to the real screen now?"
5.  **`BOREDOS_APP_DESC` / `BOREDOS_APP_ICONS`**: Embedded into the compiled `.elf` as a BoredOS NOTE section. The Desktop and File Explorer read this to show the game's icon instead of the generic binary icon. See [`elf_metadata.md`](../elf_metadata.md) for full details.

> [!WARNING]
> Because `sys_yield()`'s pause duration depends heavily on CPU load and how many other processes are running (or QEMU emulation speed), tying physics/movement strictly to loops can make the game run faster on faster computers. Advanced developers will want to calculate delta time (time elapsed since the last frame) for smooth motion.
