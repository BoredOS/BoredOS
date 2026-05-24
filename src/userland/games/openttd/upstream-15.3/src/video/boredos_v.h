/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file boredos_v.h BoredOS native video driver. */

#ifndef VIDEO_BOREDOS_H
#define VIDEO_BOREDOS_H

#include "video_driver.hpp"
#include "../core/geometry_type.hpp"

/**
 * Native BoredOS video driver.
 *
 * This is the real upstream-driver hook for the BoredOS port. It is separate
 * from SDL/Win32/OpenGL and presents OpenTTD's software framebuffer through
 * BoredOS libui. The current BoredOS runtime still needs the broader upstream
 * OS layer before this driver can replace the temporary port launcher ELF.
 */
class VideoDriver_BoredOS : public VideoDriver {
private:
	void *window = nullptr;
	uint32_t *framebuffer = nullptr;
	static constexpr size_t MAX_DIRTY_RECTS = 64;
	Rect dirty_rects[MAX_DIRTY_RECTS]{};
	size_t dirty_rect_count = 0;
	uint32_t last_present_tick = 0;

	bool ResizeFramebuffer(int width, int height);
	void MarkWholeScreenDirty();
	void PresentDirtyRects();

public:
	std::optional<std::string_view> Start(const StringList &param) override;

	void Stop() override;

	void MakeDirty(int left, int top, int width, int height) override;

	void MainLoop() override;

	bool ChangeResolution(int w, int h) override;

	bool ToggleFullscreen(bool fullscreen) override;

	std::string_view GetName() const override { return "boredos"; }
	bool HasGUI() const override { return true; }
	bool HasEfficient8Bpp() const override { return false; }
};

/** Factory for the BoredOS native video driver. */
class FVideoDriver_BoredOS : public DriverFactoryBase {
public:
	FVideoDriver_BoredOS() : DriverFactoryBase(Driver::DT_VIDEO, 7, "boredos", "BoredOS Native Video Driver") {}
	std::unique_ptr<Driver> CreateInstance() const override { return std::make_unique<VideoDriver_BoredOS>(); }
};

#endif /* VIDEO_BOREDOS_H */
