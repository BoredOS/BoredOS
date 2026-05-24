/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file boredos_main.cpp Main entry for BoredOS. */

#include "../../stdafx.h"
#include "../../openttd.h"
#include "../../crashlog.h"
#include "../../core/random_func.hpp"
#include "../../string_func.h"

#include "../../safeguards.h"

extern "C" {
#include "libc/syscall_user.h"
}

extern "C" uint32_t openttd_boredos_ticks_ms(void);

int CDECL main(int argc, char *argv[])
{
	sys_serial_write("[OpenTTD] BoredOS upstream entry\n");

	std::vector<std::string_view> params;
	for (int i = 0; i < argc; ++i) {
		StrMakeValidInPlace(argv[i]);
		params.emplace_back(argv[i]);
	}

	params.emplace_back("-v");
	params.emplace_back("boredos");
	params.emplace_back("-s");
	params.emplace_back("boredos");
	params.emplace_back("-m");
	params.emplace_back("null");
	params.emplace_back("-r");
	params.emplace_back("1024x768");
	params.emplace_back("-I");
	params.emplace_back("OpenGFX");
	params.emplace_back("-S");
	params.emplace_back("OpenSFX");
	params.emplace_back("-M");
	params.emplace_back("OpenMSX");
	params.emplace_back("-x");

	CrashLog::InitialiseCrashLog();
	SetRandomSeed(openttd_boredos_ticks_ms());
	sys_serial_write("[OpenTTD] launching core with BoredOS video/audio\n");
	return openttd_main(params);
}
