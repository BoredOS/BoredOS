<div align="center">
  <h1>Example 03: Framebuffer Bouncing Ball</h1>
  <p><em>Real-time physics and high-speed memory-mapped graphics rendering.</em></p>
</div>

---

This tutorial demonstrates how to build a real-time, high-frame-rate animation using raw `/dev/fb0` memory mapping (`mmap()`). We will implement basic physics (gravity, boundary collisions, velocity) to animate a colored ball bouncing off the screen edges, while ensuring safe terminal recovery using standard signal handling.

## 📝 Concepts Introduced
* Direct zero-copy display access via memory mapping (`mmap()`).
* High-speed frame clearance and pixel-drawing functions.
* 2D coordinate-to-1D index translation using display stride (line length).
* Basic physics loop: gravity ($g$), velocity ($v_x, v_y$), and elastic boundary collisions.
* Handling termination signals (`SIGINT`, `SIGTERM`) to guarantee TTY console recovery.

---

## The Code (`fb_bouncing_ball.c`)

```c
// BOREDOS_APP_DESC: Framebuffer Bouncing Ball - real-time graphics and physics simulation via /dev/fb0.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
#include <sys/mman.h>
#include <stdint.h>

// Global structures to coordinate signal handler recovery
int g_fb_fd = -1;
uint32_t *g_fb_mmap = MAP_FAILED;
uint32_t g_screen_size = 0;

// Cleanup callback to restore TTY and unmap memory
void clean_exit(int sig) {
    if (g_fb_mmap != MAP_FAILED && g_screen_size > 0) {
        munmap(g_fb_mmap, g_screen_size);
    }
    if (g_fb_fd >= 0) {
        close(g_fb_fd);
    }

    // Crucial: Restore standard text console blitting
    ioctl(0, KDSETMODE, (void*)KD_TEXT);

    printf("\n[Cleanup] Graphics closed and console restored cleanly (Signal %d).\n", sig);
    exit(0);
}

int main(void) {
    // 1. Open the framebuffer device
    g_fb_fd = open("/dev/fb0", O_RDWR);
    if (g_fb_fd < 0) {
        printf("Error: cannot open /dev/fb0. Are you running as root?\n");
        return 1;
    }

    // 2. Query display dimensions and pitch stride
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(g_fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(g_fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        printf("Error: failed to query framebuffer layout.\n");
        close(g_fb_fd);
        return 1;
    }

    g_screen_size = finfo.line_length * vinfo.yres;
    uint32_t stride_pixels = finfo.line_length / 4; // Width in pixels (including padding)

    // 3. Register signal handlers to guarantee terminal restoration
    signal(SIGINT, clean_exit);
    signal(SIGTERM, clean_exit);

    // 4. Disable kernel TTY text blitting
    if (ioctl(0, KDSETMODE, (void*)KD_GRAPHICS) < 0) {
        printf("Warning: failed to set TTY console to graphics mode.\n");
    }

    // 5. Memory-map physical display memory directly to process address space
    g_fb_mmap = (uint32_t *)mmap(NULL, g_screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, g_fb_fd, 0);
    if (g_fb_mmap == MAP_FAILED) {
        ioctl(0, KDSETMODE, (void*)KD_TEXT); // Recover console before exit
        printf("Error: failed to map framebuffer device to memory!\n");
        close(g_fb_fd);
        return 1;
    }

    // 6. Allocate a local backbuffer for double-buffering
    uint32_t *backbuffer = (uint32_t *)malloc(g_screen_size);
    if (!backbuffer) {
        ioctl(0, KDSETMODE, (void*)KD_TEXT);
        printf("Error: failed to allocate heap backbuffer!\n");
        munmap(g_fb_mmap, g_screen_size);
        close(g_fb_fd);
        return 1;
    }

    // 7. Physics and Rendering Loop Setup
    double ball_x = vinfo.xres / 2.0;
    double ball_y = vinfo.yres / 4.0;
    double vel_x = 5.0;  // Initial horizontal speed
    double vel_y = 0.0;  // Initial vertical speed
    
    const double gravity = 0.35;    // Downward acceleration constant
    const double elasticity = 0.88; // Bounce bounce conservation coefficient
    const double radius = 35.0;    // Size of the bouncing ball
    const uint32_t ball_color = 0xFFFF3333; // Bright Red (BGRA: 0xAARRGGBB)
    const uint32_t bg_color = 0xFF121212;   // Sleek Dark Grey

    printf("Starting bouncing physics loop... Press Ctrl+C inside terminal to exit.\n");

    // Run the real-time graphics loop for 600 frames (approx 10 seconds at 60 FPS)
    for (int frame = 0; frame < 600; frame++) {
        // --- PHYSICS UPDATE ---
        vel_y += gravity; // Apply gravity
        ball_x += vel_x;  // Apply horizontal velocity
        ball_y += vel_y;  // Apply vertical velocity

        // Handle Horizontal boundary collisions (Left / Right edges)
        if (ball_x - radius < 0) {
            ball_x = radius;
            vel_x = -vel_x * elasticity;
        } else if (ball_x + radius >= vinfo.xres) {
            ball_x = vinfo.xres - radius - 1;
            vel_x = -vel_x * elasticity;
        }

        // Handle Vertical boundary collisions (Top / Bottom edges)
        if (ball_y - radius < 0) {
            ball_y = radius;
            vel_y = -vel_y * elasticity;
        } else if (ball_y + radius >= vinfo.yres) {
            ball_y = vinfo.yres - radius - 1;
            vel_y = -vel_y * elasticity; // Bounce up with loss of energy
        }

        // --- DRAWING / RENDERING ---
        // A. Clear Screen (Fill entire backbuffer with Dark Grey)
        for (uint32_t y = 0; y < vinfo.yres; y++) {
            uint32_t *row = backbuffer + y * stride_pixels;
            for (uint32_t x = 0; x < vinfo.xres; x++) {
                row[x] = bg_color;
            }
        }

        // B. Render the Ball (Draw a solid rasterized circle)
        int start_y = (int)(ball_y - radius);
        int end_y = (int)(ball_y + radius);
        int start_x = (int)(ball_x - radius);
        int end_x = (int)(ball_x + radius);

        for (int y = start_y; y <= end_y; y++) {
            if (y < 0 || (uint32_t)y >= vinfo.yres) continue;
            
            uint32_t *row = backbuffer + y * stride_pixels;
            double dy = y - ball_y;

            for (int x = start_x; x <= end_x; x++) {
                if (x < 0 || (uint32_t)x >= vinfo.xres) continue;
                
                double dx = x - ball_x;
                // Circle equation: dx^2 + dy^2 <= r^2
                if ((dx * dx) + (dy * dy) <= (radius * radius)) {
                    row[x] = ball_color;
                }
            }
        }

        // C. Copy the finished frame to mapped physical screen memory (Double-Buffering flush)
        memcpy(g_fb_mmap, backbuffer, g_screen_size);

        // Throttle to 60 FPS (16.6 milliseconds)
        sleep(16);
    }

    // 8. Restore text mode and unmap resources upon clean exit
    free(backbuffer);
    clean_exit(0);
    return 0;
}
```

---

## How it Works

### 1. Zero-Copy mmap() Mechanics
`mmap()` links physical screen coordinates directly into userspace address ranges. Under the hood, this sets up page tables pointing to the graphics memory address range `finfo.smem_start`. Doing this means you do not have to copy pixel rows repeatedly using slow `write()` system calls. Modifying `g_fb_mmap[offset]` updates the display instantly.

### 2. Resolution vs. Stride Offset Calculation
When physical screen memory is configured, hardware often aligns rows to boundaries (like 128-byte multiples). The stride length (in bytes) is returned as `finfo.line_length`.
* We divide `finfo.line_length` by `4` (bytes per 32-bit pixel) to find the stride in pixels: `stride_pixels`.
* A coordinate `(x, y)` maps to:
  $$\text{pixel\_ptr} = \text{g\_fb\_mmap} + (y \times \text{stride\_pixels}) + x$$

### 3. Physics Equations
On every frame refresh, the ball updates its physics coordinates using Euler integration:
- **Gravity Acceleration**: $v_y \leftarrow v_y + g$. Gravity constantly adds downward speed.
- **Elastic Collision**: When hitting an edge, velocity is inverted and multiplied by a loss coefficient ($\text{elasticity} = 0.88$). This simulates bouncing energy loss, causing the ball to settle down over time.

---

## Running It

1. Compile the bouncing ball client natively:
   ```bash
   tcc fb_bouncing_ball.c -o /bin/fb_bouncing_ball
   ```
2. Type `fb_bouncing_ball` and press Enter.
3. The display will enter graphics mode and render a bright red bouncing ball accelerating under gravity. The terminal will be cleanly restored after 10 seconds or immediately if you press `Ctrl+C`.

---
