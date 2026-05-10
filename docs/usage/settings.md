# Settings

Settings is a system configuration application providing a graphical interface to manage BoredOS preferences across multiple categories.

## Main Menu

The Settings application presents seven configuration categories, each with its own icon and panel:

- **Wallpaper** — Manage desktop background images and patterns
- **Network** — Configure network interfaces and connectivity
- **Desktop** — Control desktop layout and icon alignment
- **Mouse** — Adjust mouse speed and cursor appearance
- **Fonts** — Browse and select system fonts
- **Display** — Configure screen resolution
- **Keyboard** — Select keyboard layout

## Wallpaper

### Image Selection

- Browse wallpapers stored in `/Library/images/Wallpapers/`
- Supported formats: JPEG (`.jpg`)
- Thumbnails (80×50 pixels) are generated for preview
- Selected wallpaper is applied immediately to the desktop background

### Patterns

The Wallpaper panel provides built-in pattern options:

- **Lumberjack Pattern** — Checkered pattern with red, dark grey, and black colors
- **Blue Diamond Pattern** — Geometric diamond design

Patterns are rendered procedurally (128×128 pixels) and can be applied as alternatives to image wallpapers.

### Color Settings

Six color presets are available for quick selection, with RGB textbox inputs for custom color values.


## Network

### Configuration

- Set static IP address via textbox input
- Configure DNS server address
- Network status is displayed after initialization
- Settings are applied through the `NET_INIT`, `NET_SET_IP`, and `NET_SET_DNS` controls

## Desktop

### Layout Control

- **Snap to Grid** — Enable/disable automatic icon alignment to grid positions
- **Auto Align** — Automatically reorganize icons when enabled
- **Columns** — Adjust maximum number of columns for icon layout (configurable with +/- buttons)
- **Rows per Column** — Set maximum rows within each column

Desktop grid settings are stored as `desktop_max_rows_per_col` and `desktop_max_cols`.

## Mouse

### Cursor Control

- **Mouse Speed** — Adjust pointer movement sensitivity
- **Cursor Scale** — Increase or decrease cursor size using +/− buttons
  - Settings communicates with the kernel WM using `SYSTEM_GET_CURSOR_SCALE` and `SYSTEM_SET_CURSOR_SCALE` syscalls
  - Cursor changes are applied instantly and visibly in real-time

## Fonts

### System Fonts

- Browse available fonts from `/Library/Fonts/`
- Font list is dynamically loaded with scrollbar for navigation
- Each font displays an icon and name
- Fonts are listed with entry structures containing path and name information

## Display

### Resolution Selection

- Choose from dynamic resolution options based on physical screen size:
  - 50% of screen resolution
  - 75% of screen resolution
  - Full screen resolution (100%)
- Custom resolution entry via textbox (width and height)
- Apply button commits the selected resolution change


## Keyboard

### Layout Selection

- Available keyboard layouts can be selected from a dropdown
- Layout state is maintained as `keyboard_layout`
- Selection applies to system-wide keyboard input

---

[Return to Documentation Index](../README.md)
