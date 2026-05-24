/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file boredos_s.cpp BoredOS PC speaker sound shim. */

#include "../stdafx.h"
#include "boredos_s.h"
#include "../mixer.h"

extern "C" {
#include "libc/syscall_user.h"
}

#include "../safeguards.h"

static FSoundDriver_BoredOS iFSoundDriver_BoredOS;

std::optional<std::string_view> SoundDriver_BoredOS::Start(const StringList &)
{
	MxInitialize(11025);
	sys_serial_write("[OpenTTD] BoredOS PC speaker sound driver selected\n");
	return std::nullopt;
}
