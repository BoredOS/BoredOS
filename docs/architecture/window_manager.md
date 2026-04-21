<div align="center">
  <h1>Window Manager (WM)</h1>
  <p><em>The native graphical subsystem compositing and event routing.</em></p>
</div>

---

BoredOS features a fully custom, graphical Window Manager built directly into the kernel, residing in the `src/wm/` directory. It is responsible for compositing the screen, handling window logic, rendering text, and dispatching UI events.

## Framebuffer and Rendering

1.  **Limine Framebuffer**: During boot, the Limine bootloader requests a graphical framebuffer from the hardware (e.g., GOP in UEFI environments) and passes a pointer to this linear memory buffer to the kernel.
2.  **Double Buffering**: To prevent screen tearing, the WM does not draw directly to the screen. It allocates a "back buffer" in kernel memory equal to the size of the screen. All drawing operations (lines, rectangles, windows) happen on this back buffer.
3.  **Compositing**: Once per frame or upon request, the entire back buffer (or dirty regions) is copied to the actual Limine physical framebuffer memory, making the changes visible instantly.

> [!TIP]
> The performance of the window manager heavily depends on minimizing the "dirty regions" drawn in the compositing loop rather than sweeping the whole screen.

## Window System (`wm.c`)

The windowing system is built around a linked list of `Window` structures.

-   **Z-Ordering**: The list determines the draw order. Windows at the back of the list are drawn first, and the active window is drawn last (on top).
-   **Window Structures**: Each window object tracks its dimensions (`x`, `y`, `width`, `height`), title, background color, and an internal buffer if it's acting as a canvas for userland apps.
-   **Decorations**: The kernel handles drawing window borders, title bars, and close buttons automatically unless a borderless style is specified.

## Input Handling and Events

The WM acts as the central hub for input routing.

1.  **Mouse Driver**: The PS/2 mouse driver (`dev/mouse.c`) detects movement and button clicks. It raises interrupts that update global cursor coordinates.
2.  **Hit Testing**: The WM checks these coordinates against the bounding boxes of existing windows. It handles dragging logic (if the user clicks a title bar) or focus changes.
3.  **Event Queue**: If a userland application owns the window that was clicked, the WM packages the input (coordinates, button state) into an event message and drops it into the owning process's event queue. The application can retrieve these via the custom libc UI functions.

- **Event Polling**: The UI loop inside an app continuously calls `ui_poll_event()` to respond to mouse clicks and window movement dispatched by the kernel WM.

## Multi-Core Safety & Performance

With the introduction of Symmetric Multi-Processing (SMP), the Window Manager (WM) was redesigned to ensure stability and high performance across multiple cores.

1.  **Granular Window Locks**: Each `Window` object possesses its own `spinlock_t lock;`. User applications concurrently draw directly into their own window buffers without stalling the rest of the system. The global `wm_lock` is reserved strictly for altering global structures like window z-order or syncing buffers to the screen compositing layer.
2.  **Per-CPU Rendering State**: To facilitate simultaneous GUI system calls across all CPU cores, the low-level rendering context (`g_render_target` array) is isolated per-CPU using the core ID. This allows completely lockless multi-core pixel rasterization, drastically reducing rendering bottlenecks.
3.  **Deferred Compositing**: Final screen composition (`wm_paint`) is scheduled to the main kernel idle loop on the Bootstrap Processor (BSP). This enables application cores to continue processing logic seamlessly while the GUI asynchronously handles flipping the physical framebuffer.

> [!IMPORTANT]
> Because application rendering (rasterizing geometry into a window's backbuffer) is SMP-safe and lock-free across cores, GUI performance scales linearly with the number of CPUs active.

---
