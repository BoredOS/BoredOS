/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file boredos_v.cpp BoredOS native video driver. */

#include "../stdafx.h"
#include "../openttd.h"
#include "../gfx_func.h"
#include "../blitter/factory.hpp"
#include "../core/geometry_func.hpp"
#include "../window_func.h"
#include "boredos_v.h"

#include "../safeguards.h"

#if defined(BOREDOS)
extern "C" {
#include "libc/syscall_user.h"
}
#define BOREDOS_VIDEO_LOG(message) sys_serial_write(message)
#else
#define BOREDOS_VIDEO_LOG(message) ((void)0)
#endif

/*
 * These bridge functions are supplied by the BoredOS userland port layer.
 * Keeping this file in upstream-driver shape makes the missing work explicit:
 * once the OpenTTD core links for BoredOS, this driver becomes the replacement
 * for the temporary /bin/openttd.elf launcher.
 */
extern "C" void *ottd_boredos_window_create(const char *title, int width, int height);
extern "C" void ottd_boredos_window_destroy(void *window);
extern "C" void ottd_boredos_window_present(void *window, const uint32_t *pixels, int width, int height);
extern "C" void ottd_boredos_window_present_rect(void *window, const uint32_t *pixels, int stride, int x, int y, int width, int height);
extern "C" void ottd_boredos_sleep_ms(uint32_t ms);

struct OTTDBoredOSEvent {
	int type;
	int legacy;
	int keycode;
	int mods;
	uint32_t codepoint;
	int x;
	int y;
	int buttons;
	int wheel;
};

extern "C" int ottd_boredos_window_poll(void *window, OTTDBoredOSEvent *event);

enum BoredOSGuiEvent {
	BOREDOS_GUI_EVENT_NONE = 0,
	BOREDOS_GUI_EVENT_PAINT = 1,
	BOREDOS_GUI_EVENT_CLICK = 2,
	BOREDOS_GUI_EVENT_RIGHT_CLICK = 3,
	BOREDOS_GUI_EVENT_CLOSE = 4,
	BOREDOS_GUI_EVENT_KEY = 5,
	BOREDOS_GUI_EVENT_MOUSE_DOWN = 6,
	BOREDOS_GUI_EVENT_MOUSE_UP = 7,
	BOREDOS_GUI_EVENT_MOUSE_MOVE = 8,
	BOREDOS_GUI_EVENT_MOUSE_WHEEL = 9,
	BOREDOS_GUI_EVENT_KEYUP = 10,
	BOREDOS_GUI_EVENT_RESIZE = 11,
};

enum BoredOSKeyCode {
	BOREDOS_LEGACY_ARROW_UP = 17,
	BOREDOS_LEGACY_ARROW_DOWN = 18,
	BOREDOS_LEGACY_ARROW_LEFT = 19,
	BOREDOS_LEGACY_ARROW_RIGHT = 20,
	BOREDOS_LEGACY_BACKSPACE = 8,
	BOREDOS_LEGACY_TAB = 9,
	BOREDOS_LEGACY_ENTER = 10,
	BOREDOS_LEGACY_ESCAPE = 27,
	BOREDOS_LEGACY_DELETE = 127,
	BOREDOS_KEY_ESC = 1,
	BOREDOS_KEY_BACKSPACE = 14,
	BOREDOS_KEY_TAB = 15,
	BOREDOS_KEY_ENTER = 28,
	BOREDOS_KEY_CTRL_L = 29,
	BOREDOS_KEY_ALT = 56,
	BOREDOS_KEY_SPACE = 57,
	BOREDOS_KEY_F1 = 59,
	BOREDOS_KEY_F12 = 85,
	BOREDOS_KEY_KP_ENTER = 86,
	BOREDOS_KEY_HOME = 90,
	BOREDOS_KEY_ARROW_UP = 91,
	BOREDOS_KEY_PAGE_UP = 92,
	BOREDOS_KEY_ARROW_LEFT = 93,
	BOREDOS_KEY_ARROW_RIGHT = 94,
	BOREDOS_KEY_END = 95,
	BOREDOS_KEY_ARROW_DOWN = 96,
	BOREDOS_KEY_PAGE_DOWN = 97,
	BOREDOS_KEY_INSERT = 98,
	BOREDOS_KEY_DELETE = 99,
};

static constexpr uint32_t BOREDOS_MOD_SHIFT = 0x0001u;
static constexpr uint32_t BOREDOS_MOD_CTRL = 0x0002u;
static constexpr uint32_t BOREDOS_MOD_ALT = 0x0004u;

static FVideoDriver_BoredOS iFVideoDriver_BoredOS;
static uint8_t _boredos_dirkeys = 0;

static uint32_t BoredOSTicks()
{
#if defined(BOREDOS)
	return (uint32_t)sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0);
#else
	return 0;
#endif
}

static constexpr uint32_t BOREDOS_PRESENT_INTERVAL_TICKS = 1;
static constexpr int BOREDOS_UPSTREAM_DEFAULT_WINDOW_WIDTH = 640;
static constexpr int BOREDOS_UPSTREAM_DEFAULT_WINDOW_HEIGHT = 480;
static constexpr int BOREDOS_DEFAULT_WINDOW_WIDTH = 512;
static constexpr int BOREDOS_DEFAULT_WINDOW_HEIGHT = 384;

static uint ConvertBoredOSKey(const OTTDBoredOSEvent &event)
{
	uint keycode = WKC_NONE;

	if (event.keycode >= BOREDOS_KEY_F1 && event.keycode <= BOREDOS_KEY_F12) {
		keycode = WKC_F1 + (event.keycode - BOREDOS_KEY_F1);
	} else {
		switch (event.keycode) {
			case BOREDOS_KEY_ESC: keycode = WKC_ESC; break;
			case BOREDOS_KEY_BACKSPACE: keycode = WKC_BACKSPACE; break;
			case BOREDOS_KEY_TAB: keycode = WKC_TAB; break;
			case BOREDOS_KEY_ENTER:
			case BOREDOS_KEY_KP_ENTER: keycode = WKC_RETURN; break;
			case BOREDOS_KEY_SPACE: keycode = WKC_SPACE; break;
			case BOREDOS_KEY_INSERT: keycode = WKC_INSERT; break;
			case BOREDOS_KEY_DELETE: keycode = WKC_DELETE; break;
			case BOREDOS_KEY_PAGE_UP: keycode = WKC_PAGEUP; break;
			case BOREDOS_KEY_PAGE_DOWN: keycode = WKC_PAGEDOWN; break;
			case BOREDOS_KEY_END: keycode = WKC_END; break;
			case BOREDOS_KEY_HOME: keycode = WKC_HOME; break;
			case BOREDOS_KEY_ARROW_LEFT: keycode = WKC_LEFT; break;
			case BOREDOS_KEY_ARROW_UP: keycode = WKC_UP; break;
			case BOREDOS_KEY_ARROW_RIGHT: keycode = WKC_RIGHT; break;
			case BOREDOS_KEY_ARROW_DOWN: keycode = WKC_DOWN; break;
			default:
				break;
		}
	}

	if (keycode == WKC_NONE) {
		switch (event.legacy) {
			case BOREDOS_LEGACY_ESCAPE: keycode = WKC_ESC; break;
			case BOREDOS_LEGACY_BACKSPACE: keycode = WKC_BACKSPACE; break;
			case BOREDOS_LEGACY_TAB: keycode = WKC_TAB; break;
			case BOREDOS_LEGACY_ENTER: keycode = WKC_RETURN; break;
			case BOREDOS_LEGACY_DELETE: keycode = WKC_DELETE; break;
			case BOREDOS_LEGACY_ARROW_LEFT: keycode = WKC_LEFT; break;
			case BOREDOS_LEGACY_ARROW_UP: keycode = WKC_UP; break;
			case BOREDOS_LEGACY_ARROW_RIGHT: keycode = WKC_RIGHT; break;
			case BOREDOS_LEGACY_ARROW_DOWN: keycode = WKC_DOWN; break;
			default:
				if (event.legacy >= 'a' && event.legacy <= 'z') {
					keycode = (uint)('A' + (event.legacy - 'a'));
				} else if (event.legacy >= 32 && event.legacy <= 126) {
					keycode = (uint)event.legacy;
				}
				break;
		}
	}

	if (keycode == WKC_NONE && event.keycode >= 'a' && event.keycode <= 'z') {
		keycode = (uint)('A' + (event.keycode - 'a'));
	}
	if (keycode == WKC_NONE && event.keycode >= 'A' && event.keycode <= 'Z') {
		keycode = (uint)event.keycode;
	}

	if ((event.mods & BOREDOS_MOD_SHIFT) != 0) keycode |= WKC_SHIFT;
	if ((event.mods & BOREDOS_MOD_CTRL) != 0) keycode |= WKC_CTRL;
	if ((event.mods & BOREDOS_MOD_ALT) != 0) keycode |= WKC_ALT;
	return keycode;
}

static uint8_t GetBoredOSDirectionBit(const OTTDBoredOSEvent &event)
{
	switch (event.keycode) {
		case BOREDOS_KEY_ARROW_LEFT: return 1;
		case BOREDOS_KEY_ARROW_UP: return 2;
		case BOREDOS_KEY_ARROW_RIGHT: return 4;
		case BOREDOS_KEY_ARROW_DOWN: return 8;
		default:
			break;
	}

	switch (event.legacy) {
		case BOREDOS_LEGACY_ARROW_LEFT: return 1;
		case BOREDOS_LEGACY_ARROW_UP: return 2;
		case BOREDOS_LEGACY_ARROW_RIGHT: return 4;
		case BOREDOS_LEGACY_ARROW_DOWN: return 8;
		default:
			return 0;
	}
}

static void HandleBoredOSKeyEvent(const OTTDBoredOSEvent &event)
{
	_ctrl_pressed = (event.mods & BOREDOS_MOD_CTRL) != 0;
	_shift_pressed = (event.mods & BOREDOS_MOD_SHIFT) != 0;
	HandleCtrlChanged();

	uint8_t dir_bit = GetBoredOSDirectionBit(event);
	if (dir_bit != 0) {
		if (event.type == BOREDOS_GUI_EVENT_KEYUP) {
			_boredos_dirkeys &= (uint8_t)~dir_bit;
		} else {
			_boredos_dirkeys |= dir_bit;
		}
		_dirkeys = _boredos_dirkeys;
	}

	if (event.type == BOREDOS_GUI_EVENT_KEYUP) return;

	char32_t character = event.codepoint;
	if (character == 0 && event.legacy >= 32 && event.legacy <= 126) character = (char32_t)event.legacy;
	HandleKeypress(ConvertBoredOSKey(event), character);
}

static void HandleBoredOSMouseEvent(const OTTDBoredOSEvent &event)
{
	if (event.type == BOREDOS_GUI_EVENT_MOUSE_WHEEL) {
		if (event.wheel > 0) {
			_cursor.wheel--;
		} else if (event.wheel < 0) {
			_cursor.wheel++;
		}
		_cursor.v_wheel -= (float)event.wheel * 14.0f;
		_cursor.wheel_moved = true;
		HandleMouseEvents();
		return;
	}

	(void)_cursor.UpdateCursorPosition(event.x, event.y);

	if (event.type == BOREDOS_GUI_EVENT_CLICK) {
		_left_button_down = true;
		_left_button_clicked = true;
		HandleMouseEvents();
		_left_button_down = false;
		_left_button_clicked = false;
		HandleMouseEvents();
		return;
	}

	if (event.type == BOREDOS_GUI_EVENT_RIGHT_CLICK) {
		_right_button_down = true;
		_right_button_clicked = true;
		HandleMouseEvents();
		_right_button_down = false;
		_right_button_clicked = false;
		HandleMouseEvents();
		return;
	}

	switch (event.type) {
		case BOREDOS_GUI_EVENT_MOUSE_DOWN:
			_left_button_down = (event.buttons & 1) != 0;
			_right_button_down = (event.buttons & 2) != 0;
			if (_right_button_down) _right_button_clicked = true;
			break;

		case BOREDOS_GUI_EVENT_MOUSE_UP:
			_left_button_down = false;
			_left_button_clicked = false;
			_right_button_down = false;
			_right_button_clicked = false;
			break;

		default:
			break;
	}

	HandleMouseEvents();
}

bool VideoDriver_BoredOS::ResizeFramebuffer(int width, int height)
{
	if (width <= 0 || height <= 0) return false;
	if (width == _screen.width && height == _screen.height && this->framebuffer != nullptr) return true;

	const size_t pixel_count = (size_t)width * (size_t)height;
	uint32_t *new_framebuffer = new (std::nothrow) uint32_t[pixel_count];
	if (new_framebuffer == nullptr) return false;

	std::fill_n(new_framebuffer, pixel_count, 0xFF000000u);
	delete[] this->framebuffer;
	this->framebuffer = new_framebuffer;

	_screen.width = _screen.pitch = width;
	_screen.height = height;
	_screen.dst_ptr = this->framebuffer;
	_cur_resolution.width = width;
	_cur_resolution.height = height;

	ScreenSizeChanged();
	if (BlitterFactory::GetCurrentBlitter() != nullptr) BlitterFactory::GetCurrentBlitter()->PostResize();
	if (this->window != nullptr) GameSizeChanged();
	this->MarkWholeScreenDirty();
	return true;
}

std::optional<std::string_view> VideoDriver_BoredOS::Start(const StringList &)
{
	this->UpdateAutoResolution();
	if (_cur_resolution.width == BOREDOS_UPSTREAM_DEFAULT_WINDOW_WIDTH && _cur_resolution.height == BOREDOS_UPSTREAM_DEFAULT_WINDOW_HEIGHT) {
		_cur_resolution.width = BOREDOS_DEFAULT_WINDOW_WIDTH;
		_cur_resolution.height = BOREDOS_DEFAULT_WINDOW_HEIGHT;
	}

	if (!this->ResizeFramebuffer(_cur_resolution.width, _cur_resolution.height)) return "BoredOS video framebuffer allocation failed";

	this->window = ottd_boredos_window_create("OpenTTD", _screen.width, _screen.height);
	if (this->window == nullptr) return "BoredOS window creation failed";

	ScreenSizeChanged();
	/*
	 * Avoid the animation/SSE blitters for now; their extra buffers and vector
	 * paths are a larger compatibility surface on BoredOS. The generic optimized
	 * 32bpp blitter is still scalar and significantly cheaper when zoomed out.
	 */
	BOREDOS_VIDEO_LOG("[OpenTTD] selecting BoredOS-safe 32bpp-optimized blitter\n");
	BlitterFactory::SelectBlitter("32bpp-optimized");
	BlitterFactory::GetCurrentBlitter()->PostResize();
		BOREDOS_VIDEO_LOG("[OpenTTD] BoredOS video start complete\n");
		this->MarkWholeScreenDirty();
		return std::nullopt;
}

void VideoDriver_BoredOS::Stop()
{
	if (this->window != nullptr) {
		ottd_boredos_window_destroy(this->window);
		this->window = nullptr;
	}
	delete[] this->framebuffer;
	this->framebuffer = nullptr;
	_screen.dst_ptr = nullptr;
}

void VideoDriver_BoredOS::MarkWholeScreenDirty()
{
	this->dirty_rects[0] = {0, 0, _screen.width, _screen.height};
	this->dirty_rect_count = 1;
}

void VideoDriver_BoredOS::MakeDirty(int left, int top, int width, int height)
{
	if (width <= 0 || height <= 0 || _screen.width <= 0 || _screen.height <= 0) return;

	Rect rect{
		std::max(0, left),
		std::max(0, top),
		std::min(_screen.width, left + width),
		std::min(_screen.height, top + height),
	};
	if (rect.left >= rect.right || rect.top >= rect.bottom) return;

	for (size_t i = 0; i < this->dirty_rect_count; i++) {
		Rect &existing = this->dirty_rects[i];
		bool overlaps_or_touches = rect.left <= existing.right && rect.right >= existing.left &&
				rect.top <= existing.bottom && rect.bottom >= existing.top;
		if (overlaps_or_touches) {
			existing = BoundingRect(existing, rect);
			return;
		}
	}

	if (this->dirty_rect_count < MAX_DIRTY_RECTS) {
		this->dirty_rects[this->dirty_rect_count++] = rect;
		return;
	}

	Rect combined = rect;
	for (size_t i = 0; i < this->dirty_rect_count; i++) combined = BoundingRect(combined, this->dirty_rects[i]);
	this->dirty_rects[0] = combined;
	this->dirty_rect_count = 1;
}

void VideoDriver_BoredOS::PresentDirtyRects()
{
	if (this->dirty_rect_count == 0 || this->framebuffer == nullptr) return;

	Rect combined = this->dirty_rects[0];
	uint64_t total_area = 0;
	for (size_t i = 0; i < this->dirty_rect_count; i++) {
		const Rect &rect = this->dirty_rects[i];
		if (IsEmptyRect(rect)) continue;
		combined = BoundingRect(combined, rect);
		total_area += (uint64_t)(rect.right - rect.left) * (uint64_t)(rect.bottom - rect.top);
	}

	if (!IsEmptyRect(combined)) {
		const uint64_t screen_area = (uint64_t)_screen.width * (uint64_t)_screen.height;
		const uint64_t combined_area = (uint64_t)(combined.right - combined.left) * (uint64_t)(combined.bottom - combined.top);
		const bool use_atomic_present = this->dirty_rect_count > 4 ||
				combined_area * 5 >= screen_area ||
				total_area * 6 >= screen_area;

		if (use_atomic_present) {
			ottd_boredos_window_present_rect(this->window, this->framebuffer, _screen.pitch, combined.left, combined.top, combined.right - combined.left, combined.bottom - combined.top);
		} else {
			for (size_t i = 0; i < this->dirty_rect_count; i++) {
				Rect rect = this->dirty_rects[i];
				if (IsEmptyRect(rect)) continue;
				ottd_boredos_window_present_rect(this->window, this->framebuffer, _screen.pitch, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
			}
		}
	}

	this->dirty_rect_count = 0;
}

void VideoDriver_BoredOS::MainLoop()
{
	this->is_game_threaded = false;
	int pending_resize_w = 0;
	int pending_resize_h = 0;

	while (!_exit_game) {
		sys_network_poll();
		OTTDBoredOSEvent event{};
		while (ottd_boredos_window_poll(this->window, &event) != 0) {
			switch (event.type) {
				case BOREDOS_GUI_EVENT_CLOSE:
					HandleExitGameRequest();
					break;

				case BOREDOS_GUI_EVENT_KEY:
				case BOREDOS_GUI_EVENT_KEYUP:
					HandleBoredOSKeyEvent(event);
					break;

				case BOREDOS_GUI_EVENT_CLICK:
				case BOREDOS_GUI_EVENT_RIGHT_CLICK:
				case BOREDOS_GUI_EVENT_MOUSE_DOWN:
				case BOREDOS_GUI_EVENT_MOUSE_UP:
				case BOREDOS_GUI_EVENT_MOUSE_MOVE:
				case BOREDOS_GUI_EVENT_MOUSE_WHEEL:
					HandleBoredOSMouseEvent(event);
					break;

				case BOREDOS_GUI_EVENT_PAINT:
					this->MakeDirty(0, 0, _screen.width, _screen.height);
					break;

				case BOREDOS_GUI_EVENT_RESIZE:
					pending_resize_w = event.x;
					pending_resize_h = event.y;
					break;

				default:
					break;
			}
		}

			if (pending_resize_w > 0 && pending_resize_h > 0) {
				if (this->ResizeFramebuffer(pending_resize_w, pending_resize_h)) {
					this->MarkWholeScreenDirty();
				}
				pending_resize_w = 0;
				pending_resize_h = 0;
		}

		::GameLoop();
		sys_network_poll();
		::InputLoop();
		::UpdateWindows();

			uint32_t now_ticks = BoredOSTicks();
			bool present_now = now_ticks == 0 || this->last_present_tick == 0 ||
					now_ticks - this->last_present_tick >= BOREDOS_PRESENT_INTERVAL_TICKS;
			if (present_now) {
				this->PresentDirtyRects();
				this->last_present_tick = now_ticks;
			}

			this->DrainCommandQueue();
			sys_network_poll();
			ottd_boredos_sleep_ms(1);
		}
}

bool VideoDriver_BoredOS::ChangeResolution(int w, int h)
{
	return this->ResizeFramebuffer(w, h);
}

bool VideoDriver_BoredOS::ToggleFullscreen(bool)
{
	return false;
}
