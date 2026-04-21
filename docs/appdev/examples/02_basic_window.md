<div align="center">
  <h1>Example 02: Basic Window</h1>
  <p><em>An introduction to libui and creating graphical apps.</em></p>
</div>

---

This example demonstrates how to create an empty window that stays active on the screen until the user explicitly closes it by clicking the 'X' button.

## Concepts Introduced
* Including `libui.h` and the event structure.
* Creating a `ui_window_t` handle.
* Creating an infinite event loop using `ui_get_event()`.
* Yielding CPU time to the kernel via `sys_yield()`.
* Declaring app metadata via source annotations.

---

## The Code (`src/userland/gui/basic_window.c`)

```c
// BOREDOS_APP_DESC: Basic Window — a minimal graphical window demo.
#include <stdlib.h>
#include <libui.h>
#include <syscall.h>

int main(void) {
    // 1. Ask the Window Manager to create a new window
    // Arguments are: Title, X Position, Y Position, Width, Height
    ui_window_t wid = ui_window_create("My First GUI", 100, 100, 400, 300);
    
    if (wid < 0) {
        printf("Failed to create the window!\n");
        return 1;
    }

    // 2. Define our event object
    gui_event_t event;

    // 3. Enter the main event loop
    while (1) {
        // ui_get_event is non-blocking. It returns true if an event was waiting.
        if (ui_get_event(wid, &event)) {
            
            // Check what type of event occurred
            if (event.type == GUI_EVENT_CLOSE) {
                // The user clicked the 'X' button in the titlebar!
                printf("Window closed cleanly by user.\n");
                break; // Break the infinite loop
            }
        }
        
        // 4. CRITICAL: Yield the remainder of our timeslice
        // If we don't do this, the while(1) loop will consume 100% of the CPU
        // and starve the rest of the OS!
        sys_yield();
    }

    // Returning from main will automatically destroy the window and exit the process.
    return 0;
}
```

## 🛠️ How it Works

1.  **Window Handle (`wid`)**: `ui_window_create` sends a request to the kernel. The kernel allocates the memory for the window and returns a numerical ID (the handle) that we use for all future interactions with that specific window.
2.  **The Event Loop**: Graphical programs run forever until closed. The `while (1)` loop serves this purpose.
3.  **Polling**: `ui_get_event` asks the kernel, "Hey, did the user click my window or press a key since the last time I asked?". It is non-blocking, so it immediately returns `false` if nothing happened.
4.  **CPU Yielding**: Since we are constantly polling in a tight loop, we call `sys_yield()` at the end of the loop frame. This politely tells the OS scheduler, "I'm done checking for events, go ahead and let another program run for a bit."
5.  **`BOREDOS_APP_DESC` / `BOREDOS_APP_ICONS`**: Embedded into the `.elf` by the build system as a BoredOS NOTE section. The Window Manager reads this at runtime to render the app's icon on the Desktop and in the File Explorer. See [`elf_metadata.md`](../elf_metadata.md) for full details.

## Running It

Launch the Terminal and type `basic_window`. You'll see an empty window appear that you can move around the screen!
