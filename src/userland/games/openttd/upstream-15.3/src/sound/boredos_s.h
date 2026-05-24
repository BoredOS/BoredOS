/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file boredos_s.h BoredOS PC speaker sound shim. */

#ifndef SOUND_BOREDOS_H
#define SOUND_BOREDOS_H

#include "sound_driver.hpp"

class SoundDriver_BoredOS : public SoundDriver {
public:
	std::optional<std::string_view> Start(const StringList &) override;
	void Stop() override {}
	std::string_view GetName() const override { return "boredos"; }
	bool HasOutput() const override { return true; }
};

class FSoundDriver_BoredOS : public DriverFactoryBase {
public:
	FSoundDriver_BoredOS() : DriverFactoryBase(Driver::DT_SOUND, 5, "boredos", "BoredOS PC Speaker Sound Driver") {}
	std::unique_ptr<Driver> CreateInstance() const override { return std::make_unique<SoundDriver_BoredOS>(); }
};

#endif /* SOUND_BOREDOS_H */
