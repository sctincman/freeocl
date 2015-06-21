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
#include "device.h"
#include "platform.h"
#include <cstring>
#include <iostream>
#include <time.h>
#include <algorithm>
#include <CL/cl_ext.h>
#include <unistd.h>
#include <utils/threadpool.h>
#ifdef FREEOCL_OS_WINDOWS
#include <windows.h>
#endif

#define SEP " "

extern "C"
{
	CL_API_ENTRY cl_int CL_API_CALL	clCreateSubDevicesFCLCPU(cl_device_id in_device,
								 const cl_device_partition_property * properties,
								 cl_uint                              num_devices,
								 cl_device_id *                       out_devices,
								 cl_uint *                            num_devices_ret) CL_API_SUFFIX__VERSION_1_2
	{
		MSG(clCreateSubDevicesFCL);
		if (in_device != FreeOCL::device)
			return CL_INVALID_DEVICE;
		if (!properties)
			return CL_INVALID_VALUE;
		if (!out_devices && num_devices < 1)
			return CL_INVALID_VALUE;

		for(size_t i = 0 ; properties[i] != 0 ; ++i)
		{
			switch(properties[i])
			{
			case CL_DEVICE_PARTITION_EQUALLY:
				++i;
				if (properties[i] == 0)
					return CL_INVALID_VALUE;
				if (properties[i] > in_device->cpu_cores)
					return CL_DEVICE_PARTITION_FAILED;
				break;
			case CL_DEVICE_PARTITION_BY_COUNTS:
				{
					size_t j;
					for(j = 1 ; properties[i + j] != CL_DEVICE_PARTITION_BY_COUNTS_LIST_END ; ++j)
					{}
					if (j == 1)
					{
						if (num_devices_ret)
							*num_devices_ret = 0;
						return CL_SUCCESS;
					}
					if (j > 2)
						return CL_INVALID_DEVICE_PARTITION_COUNT;
					if (properties[i + 1] > in_device->cpu_cores)
						return CL_INVALID_DEVICE_PARTITION_COUNT;
					i += j;
				}
				break;
			case CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN:
				++i;
				switch(properties[i])
				{
				case CL_DEVICE_AFFINITY_DOMAIN_NUMA:
				case CL_DEVICE_AFFINITY_DOMAIN_L1_CACHE:
				case CL_DEVICE_AFFINITY_DOMAIN_L2_CACHE:
				case CL_DEVICE_AFFINITY_DOMAIN_L3_CACHE:
				case CL_DEVICE_AFFINITY_DOMAIN_L4_CACHE:
				case CL_DEVICE_AFFINITY_DOMAIN_NEXT_PARTITIONABLE:
					break;
				default:
					return CL_INVALID_VALUE;
				}

				break;
			default:
				return CL_INVALID_VALUE;
			}
		}

		if (out_devices)
			*out_devices = in_device;
		if (num_devices_ret)
			*num_devices_ret = 1;
		return CL_SUCCESS;
	}

}

_cl_device_id_cpu::_cl_device_id_cpu() :
	device_type(CL_DEVICE_TYPE_CPU),
	vendor_id(0),
	addressbits(sizeof(void*) * 8),
	version("OpenCL 1.2 FreeOCL-" FREEOCL_VERSION_STRING),
	driver_version(FREEOCL_VERSION_STRING),
	device_profile("FULL_PROFILE"),
	opencl_c_version("OpenCL C 1.2"),
	extensions("cl_khr_icd" SEP
		   "cl_khr_global_int32_base_atomics" SEP
		   "cl_khr_global_int32_extended_atomics" SEP
		   "cl_khr_local_int32_base_atomics" SEP
		   "cl_khr_local_int32_extended_atomics" SEP
		   "cl_khr_byte_addressable_store" SEP
		   "cl_khr_fp64" SEP
		   "cl_khr_int64_base_atomics" SEP
		   "cl_khr_int64_extended_atomics" SEP
		   "cl_freeocl_debug"),
	exec_capabilities(CL_EXEC_KERNEL | CL_EXEC_NATIVE_KERNEL),
	preferred_vector_width_char(16),
	preferred_vector_width_short(8),
	preferred_vector_width_int(4),
	preferred_vector_width_long(2),
	preferred_vector_width_float(4),
	preferred_vector_width_double(2),
	preferred_vector_width_half(8),
	native_vector_width_char(16),
	native_vector_width_short(8),
	native_vector_width_int(4),
	native_vector_width_long(2),
	native_vector_width_float(4),
	native_vector_width_double(2),
	native_vector_width_half(8),
	max_work_item_dimensions(3),
	max_work_group_size(1024),
	mem_cache_type(CL_READ_WRITE_CACHE),
	local_mem_type(CL_GLOBAL),
	local_mem_size(0x8000),
	max_parameter_size(8192),
	mem_base_addr_align(128),
	fp_config(CL_FP_DENORM | CL_FP_INF_NAN | CL_FP_FMA | CL_FP_ROUND_TO_NEAREST | CL_FP_ROUND_TO_ZERO | CL_FP_ROUND_TO_INF),
	max_constant_buffer_size(0x100000),
	max_constant_args(1024),
	timer_resolution(std::max<size_t>(1LU, 1000000000LU / CLOCKS_PER_SEC)),
	command_queue_properties(CL_QUEUE_PROFILING_ENABLE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE),

	max_read_image_args(128),
	max_write_image_args(8),
	image2d_max_width(8192),
	image2d_max_height(8192),
	image3d_max_width(2048),
	image3d_max_height(2048),
	image3d_max_depth(2048),
	image_max_buffer_size(0x400000),
	image_max_array_size(2048),
	max_samplers(16),
	printf_buffer_size(0x400000),
	max_sub_devices(1),
	compiler(FREEOCL_CXX_COMPILER),
	flags(FREEOCL_CXX_FLAGS),
	includes(FREEOCL_CXX_INCLUDE)
{
	using namespace FreeOCL;

	//Allow overriding of the defaults with environmental variable
	const char *env_FREEOCL_CXX_COMPILER = getenv("FREEOCL_CXX_COMPILER");
	const char *env_FREEOCL_CXX_FLAGS = getenv("FREEOCL_CXX_FLAGS");
	const char *env_FREEOCL_CXX_INCLUDE = getenv("FREEOCL_CXX_INCLUDE");
	if (env_FREEOCL_CXX_COMPILER)
		compiler = env_FREEOCL_CXX_COMPILER;
	if (env_FREEOCL_CXX_FLAGS)
		flags = env_FREEOCL_CXX_FLAGS;
	if (env_FREEOCL_CXX_INCLUDE)
		includes = std::string("-I") + env_FREEOCL_CXX_INCLUDE;

	pool = new FreeOCL::threadpool();

#if defined(FREEOCL_OS_LINUX) || defined(FREEOCL_OS_UNKNWON)
	std::string ostype = trim(run_command("/sbin/sysctl -e kernel.ostype | awk '{ print $NF }'"));
	if (ostype.empty())
		ostype = trim(run_command("/sbin/sysctl -e kern.ostype | awk '{ print $NF }'"));
	if (ostype.empty())
		ostype = trim(run_command("/sbin/sysctl -a | grep ostype | awk '{ print $NF }'"));
#elif defined(FREEOCL_OS_DARWIN)
	std::string ostype = trim(run_command("/usr/sbin/sysctl kernel.ostype 2>/dev/null | awk '{ print $NF }'"));
	if (ostype.empty())
		ostype = trim(run_command("/usr/sbin/sysctl kern.ostype | awk '{ print $NF }'"));
	if (ostype.empty())
		ostype = trim(run_command("/usr/sbin/sysctl -a | grep ostype | awk '{ print $NF }'"));
	// TODO: Use hwloc <http://www.open-mpi.org/projects/hwloc/>
	// to obtain this information
#elif defined(FREEOCL_OS_WINDOWS)
	std::string ostype = "Windows";
#endif
	if (ostype == "Linux")
	{
#ifdef _SC_NPROCESSORS_ONLN
		cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);
#else
		cpu_cores = parse_int(run_command("cat /proc/cpuinfo | grep \"cpu cores\" | head -1 | sed -e \"s/cpu cores\t: //\""));
#endif
		memsize = parse_int(run_command("cat /proc/meminfo | grep MemTotal | awk '{ print $2 }'")) * 1024U;
		freememsize = parse_int(run_command("cat /proc/meminfo | grep MemFree | awk '{ print $2 }'")) * 1024U;
		name = trim(run_command("cat /proc/cpuinfo | grep \"model name\" | head -1 | sed -e \"s/model name\t: //\""));
		if (name.empty())
			name = trim(run_command("cat /proc/cpuinfo | grep \"Processor\" | head -1 | sed -e \"s/Processor\t: //\""));
		vendor = trim(run_command("cat /proc/cpuinfo | grep vendor_id | head -1 | sed -e \"s/vendor_id\t: //\""));
		max_clock_frequency = parse_int(run_command("cat /proc/cpuinfo | grep \"cpu MHz\" | head -1 | sed -e \"s/cpu MHz\t\t: //\""));
		mem_cacheline_size = parse_int(run_command("cat /proc/cpuinfo | grep cache_alignment | head -1 | sed -e \"s/cache_alignment\t: //\""));
		mem_cache_size = parse_int(run_command("cat /proc/cpuinfo | grep \"cache size\" | head -1 | awk '{ print $4 }'")) * 1024U;
	}
	else if (ostype == "FreeBSD" || ostype == "NetBSD" || ostype == "OpenBSD")	// Regarding NetBSD and OpenBSD
	{
		// this is just a guess
		cpu_cores = parse_int(run_command("/sbin/sysctl hw.ncpu | awk '{ print $NF }'"));
		name = trim(run_command("/sbin/sysctl hw.model | awk '{ for(i=2;i<NF;++i) print $i }' | tr \"\\n\" \" \""));
		vendor = trim(run_command("/sbin/sysctl hw.model | awk '{ print $2 }'"));
		memsize = parse_int(run_command("/sbin/sysctl hw.realmem | awk '{ print $NF }'"));
		freememsize = parse_int(run_command("/sbin/sysctl -a | grep v_free_count | awk '{ print $NF }'"))
					  * parse_int(run_command("/sbin/sysctl -a | grep v_page_size | awk '{ print $NF }'"));
		max_clock_frequency = parse_int(run_command("/sbin/sysctl hw.clockrate | awk '{ print $NF }'"));
		// I don't know how to get this information ... so let's use default values
		mem_cacheline_size = 64;
		mem_cache_size = 4 * 1024 * 1024;
	}
	else if (ostype == "Darwin")	// Regarding OSX
	{
		cpu_cores = parse_int(run_command("/usr/sbin/sysctl hw.ncpu | awk '{ print $NF }'"));
		name = trim(run_command("/usr/sbin/sysctl hw.model | awk '{ for(i=2;i<NF;++i) print $i }' | tr \"\\n\" \" \""));
		vendor = trim(run_command("/usr/sbin/sysctl hw.model | awk '{ print $2 }'"));
		memsize = parse_int(run_command("/usr/sbin/sysctl hw.memsize | awk '{ print $NF }'"));
		freememsize = parse_int(run_command("/usr/sbin/sysctl vm.page_free_count | awk '{ print $NF }'"))
				* parse_int(run_command("/usr/sbin/sysctl hw.pagesize | awk '{ print $NF }'"));
		max_clock_frequency = parse_int(run_command("/usr/sbin/sysctl machdep.tsc.frequency | awk '{ print $NF }'"));
	}
#ifdef FREEOCL_OS_WINDOWS
	else if (ostype == "Windows")
	{
		cpu_cores = strtol(getenv("NUMBER_OF_PROCESSORS"), NULL, 10);
		MEMORYSTATUSEX statex;
		statex.dwLength = sizeof (statex);
		GlobalMemoryStatusEx (&statex);
		memsize = statex.ullTotalPhys;
		freememsize = statex.ullAvailPhys;

		const char *cpu_id = getenv("PROCESSOR_IDENTIFIER");
		name = cpu_id ? cpu_id : "Unknown";
		vendor = "unknown";
		max_clock_frequency = 0;
		mem_cacheline_size = 64;
		mem_cache_size = 0;
	}
#endif

	max_work_item_sizes[0] = 1024;
	max_work_item_sizes[1] = 1024;
	max_work_item_sizes[2] = 1024;
	union
	{
		const int a;
		const char b[sizeof(int)];
	} v = { 1 };
	endian_little = v.b[0] ? CL_TRUE : CL_FALSE;
}

_cl_device_id_cpu::~_cl_device_id_cpu()
{
	delete pool;
}