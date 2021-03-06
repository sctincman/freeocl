/*
	FreeOCL - a free OpenCL implementation for CPU
	Copyright (C) 2011  Roland Brochard

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Lesser General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>
*/
#ifndef __FREEOCL_TIME_H__
#define __FREEOCL_TIME_H__

#include <time.h>
#include <cstddef>
/* See https://developer.apple.com/library/mac/#qa/qa1398/_index.html */
#if defined(__APPLE__) || defined(__MACOSX)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <unistd.h>
#endif

namespace FreeOCL
{
	inline size_t ns_timer()
	{
#if defined(__APPLE__) || defined(__MACOSX)
		static mach_timebase_info_data_t sTimebaseInfo;
		if (sTimebaseInfo.denom == 0) {
			mach_timebase_info(&sTimebaseInfo);
		}

		uint64_t time = mach_absolute_time();
		return time * sTimebaseInfo.numer / sTimebaseInfo.denom;
#else
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return ts.tv_nsec + ts.tv_sec * 1000000000LU;
#endif
	}

	size_t us_timer();
	size_t ms_timer();
	size_t s_timer();
}

#endif
