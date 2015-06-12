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
#include "backends/cpu/device.h"
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

namespace
{
	const cl_bool cl_bool_true = CL_TRUE;
	const cl_bool cl_bool_false = CL_FALSE;
}

#define SET_STRING(X)	FreeOCL::copy_memory_within_limits((X), strlen(X) + 1, param_value_size, param_value, param_value_size_ret)
#define SET_VAR(X)	FreeOCL::copy_memory_within_limits(&(X), sizeof(X), param_value_size, param_value, param_value_size_ret)

extern "C"
{
	cl_int clGetDeviceInfoFCL (cl_device_id device,
							cl_device_info param_name,
							size_t param_value_size,
							void *param_value,
							size_t *param_value_size_ret)
	{
		MSG(clGetDeviceInfoFCL);
		if (device != FreeOCL::device)
			return CL_INVALID_DEVICE;
		bool bTooSmall = false;
		switch(param_name)
		{
		case CL_DEVICE_TYPE:							bTooSmall = SET_VAR(device->device_type);	break;
		case CL_DEVICE_VENDOR_ID:						bTooSmall = SET_VAR(device->vendor_id);		break;
		case CL_DEVICE_MAX_COMPUTE_UNITS:				bTooSmall = SET_VAR(device->cpu_cores);		break;
		case CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS:		bTooSmall = SET_VAR(device->max_work_item_dimensions);	break;
		case CL_DEVICE_MAX_WORK_ITEM_SIZES:				bTooSmall = SET_VAR(device->max_work_item_sizes);	break;
		case CL_DEVICE_MAX_WORK_GROUP_SIZE:				bTooSmall = SET_VAR(device->max_work_group_size);	break;
		case CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR:		bTooSmall = SET_VAR(device->preferred_vector_width_char);	break;
		case CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT:	bTooSmall = SET_VAR(device->preferred_vector_width_short);	break;
		case CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT:		bTooSmall = SET_VAR(device->preferred_vector_width_int);	break;
		case CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG:		bTooSmall = SET_VAR(device->preferred_vector_width_long);	break;
		case CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT:	bTooSmall = SET_VAR(device->preferred_vector_width_float);	break;
		case CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE:	bTooSmall = SET_VAR(device->preferred_vector_width_double);	break;
		case CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF:		bTooSmall = SET_VAR(device->preferred_vector_width_half);	break;
		case CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR:		bTooSmall = SET_VAR(device->native_vector_width_char);	break;
		case CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT:		bTooSmall = SET_VAR(device->native_vector_width_short);	break;
		case CL_DEVICE_NATIVE_VECTOR_WIDTH_INT:			bTooSmall = SET_VAR(device->native_vector_width_int);	break;
		case CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG:		bTooSmall = SET_VAR(device->native_vector_width_long);	break;
		case CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT:		bTooSmall = SET_VAR(device->native_vector_width_float);	break;
		case CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE:		bTooSmall = SET_VAR(device->native_vector_width_double);	break;
		case CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF:		bTooSmall = SET_VAR(device->native_vector_width_half);	break;
		case CL_DEVICE_MAX_CLOCK_FREQUENCY:				bTooSmall = SET_VAR(device->max_clock_frequency);	break;
		case CL_DEVICE_ADDRESS_BITS:					bTooSmall = SET_VAR(device->addressbits);	break;
		case CL_DEVICE_MAX_MEM_ALLOC_SIZE:				bTooSmall = SET_VAR(device->freememsize);	break;
		case CL_DEVICE_IMAGE_SUPPORT:					bTooSmall = SET_VAR(cl_bool_true);	break;

		case CL_DEVICE_MAX_READ_IMAGE_ARGS:				bTooSmall = SET_VAR(device->max_read_image_args);	break;
		case CL_DEVICE_MAX_WRITE_IMAGE_ARGS:			bTooSmall = SET_VAR(device->max_write_image_args);	break;
		case CL_DEVICE_IMAGE2D_MAX_WIDTH:				bTooSmall = SET_VAR(device->image2d_max_width);	break;
		case CL_DEVICE_IMAGE2D_MAX_HEIGHT:				bTooSmall = SET_VAR(device->image2d_max_height);	break;
		case CL_DEVICE_IMAGE3D_MAX_WIDTH:				bTooSmall = SET_VAR(device->image3d_max_width);	break;
		case CL_DEVICE_IMAGE3D_MAX_HEIGHT:				bTooSmall = SET_VAR(device->image3d_max_height);	break;
		case CL_DEVICE_IMAGE3D_MAX_DEPTH:				bTooSmall = SET_VAR(device->image3d_max_depth);	break;
		case CL_DEVICE_MAX_SAMPLERS:					bTooSmall = SET_VAR(device->max_samplers);	break;
		case CL_DEVICE_IMAGE_MAX_ARRAY_SIZE:			bTooSmall = SET_VAR(device->image_max_array_size);	break;
		case CL_DEVICE_IMAGE_MAX_BUFFER_SIZE:			bTooSmall = SET_VAR(device->image_max_buffer_size);	break;

		case CL_DEVICE_MAX_PARAMETER_SIZE:				bTooSmall = SET_VAR(device->max_parameter_size);	break;
		case CL_DEVICE_MEM_BASE_ADDR_ALIGN:				bTooSmall = SET_VAR(device->mem_base_addr_align);	break;
		case CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE:		bTooSmall = SET_VAR(device->mem_base_addr_align);	break;
		case CL_DEVICE_SINGLE_FP_CONFIG:				bTooSmall = SET_VAR(device->fp_config);	break;
		case CL_DEVICE_DOUBLE_FP_CONFIG:				bTooSmall = SET_VAR(device->fp_config);	break;
		case CL_DEVICE_GLOBAL_MEM_CACHE_TYPE:			bTooSmall = SET_VAR(device->mem_cache_type);	break;
		case CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE:		bTooSmall = SET_VAR(device->mem_cacheline_size);	break;
		case CL_DEVICE_GLOBAL_MEM_CACHE_SIZE:			bTooSmall = SET_VAR(device->mem_cache_size);	break;
		case CL_DEVICE_GLOBAL_MEM_SIZE:					bTooSmall = SET_VAR(device->memsize);	break;
		case CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE:		bTooSmall = SET_VAR(device->max_constant_buffer_size);	break;
		case CL_DEVICE_MAX_CONSTANT_ARGS:				bTooSmall = SET_VAR(device->max_constant_args);	break;
		case CL_DEVICE_LOCAL_MEM_TYPE:					bTooSmall = SET_VAR(device->local_mem_type);	break;
		case CL_DEVICE_LOCAL_MEM_SIZE:					bTooSmall = SET_VAR(device->local_mem_size);	break;
		case CL_DEVICE_ERROR_CORRECTION_SUPPORT:		bTooSmall = SET_VAR(cl_bool_false);	break;
		case CL_DEVICE_HOST_UNIFIED_MEMORY:				bTooSmall = SET_VAR(cl_bool_true);	break;
		case CL_DEVICE_PROFILING_TIMER_RESOLUTION:		bTooSmall = SET_VAR(device->timer_resolution);	break;
		case CL_DEVICE_ENDIAN_LITTLE:					bTooSmall = SET_VAR(device->endian_little);	break;
		case CL_DEVICE_AVAILABLE:
		case CL_DEVICE_LINKER_AVAILABLE:
		case CL_DEVICE_COMPILER_AVAILABLE:				bTooSmall = SET_VAR(cl_bool_true);	break;
		case CL_DEVICE_EXECUTION_CAPABILITIES:			bTooSmall = SET_VAR(device->exec_capabilities);	break;
		case CL_DEVICE_QUEUE_PROPERTIES:				bTooSmall = SET_VAR(device->command_queue_properties);	break;
		case CL_DEVICE_PLATFORM:						bTooSmall = SET_VAR(FreeOCL::platform);	break;
		case CL_DEVICE_NAME:							bTooSmall = SET_STRING(device->name.c_str());	break;
		case CL_DEVICE_VENDOR:							bTooSmall = SET_STRING(device->vendor.c_str());	break;
		case CL_DRIVER_VERSION:							bTooSmall = SET_STRING(device->driver_version);	break;
		case CL_DEVICE_PROFILE:							bTooSmall = SET_STRING(device->device_profile);	break;
		case CL_DEVICE_VERSION:							bTooSmall = SET_STRING(device->version.c_str());	break;
		case CL_DEVICE_OPENCL_C_VERSION:				bTooSmall = SET_STRING(device->opencl_c_version);	break;
		case CL_DEVICE_EXTENSIONS:						bTooSmall = SET_STRING(device->extensions);	break;
		case CL_DEVICE_PRINTF_BUFFER_SIZE:				bTooSmall = SET_VAR(device->printf_buffer_size);	break;
		case CL_DEVICE_PREFERRED_INTEROP_USER_SYNC:		bTooSmall = SET_VAR(cl_bool_false);	break;
		case CL_DEVICE_PARENT_DEVICE:
			{
				cl_device_id dev = NULL;
				bTooSmall = SET_VAR(dev);
			}
			break;
		case CL_DEVICE_PARTITION_MAX_SUB_DEVICES:		bTooSmall = SET_VAR(device->max_sub_devices);	break;
		case CL_DEVICE_PARTITION_PROPERTIES:
			{
				cl_device_partition_property prop = 0;
				bTooSmall = SET_VAR(prop);
			}
			break;
		case CL_DEVICE_PARTITION_AFFINITY_DOMAIN:
			{
				cl_device_affinity_domain domain = 0;
				bTooSmall = SET_VAR(domain);
			}
			break;
		case CL_DEVICE_PARTITION_TYPE:
			{
				cl_device_partition_property prop = 0;
				bTooSmall = SET_VAR(prop);
			}
			break;
		case CL_DEVICE_REFERENCE_COUNT:
			{
				cl_uint count = 1;
				bTooSmall = SET_VAR(count);
			}
			break;
		case CL_DEVICE_BUILT_IN_KERNELS:	bTooSmall = SET_STRING("");		break;

		default:
			return CL_INVALID_VALUE;
		}
		if (bTooSmall && param_value != NULL)
			return CL_INVALID_VALUE;
		return CL_SUCCESS;
	}

	cl_int clGetDeviceIDsFCL (cl_platform_id platform,
							  cl_device_type device_type,
							  cl_uint num_entries,
							  cl_device_id *devices,
							  cl_uint *num_devices)
	{
		MSG(clGetDeviceIDsFCL);
		if (platform != FreeOCL::platform)
			return CL_INVALID_PLATFORM;

		if ((num_entries == 0 && devices != NULL)
			|| (devices == NULL && num_devices == NULL))
			return CL_INVALID_VALUE;

		if (device_type & CL_DEVICE_TYPE_ALL)
			return FreeOCL::devices.size();

		if (! (device_type & (CL_DEVICE_TYPE_CPU
				      | CL_DEVICE_TYPE_DEFAULT
				      | CL_DEVICE_TYPE_GPU
				      | CL_DEVICE_TYPE_ACCELERATOR
				      | CL_DEVICE_TYPE_CUSTOM
				      | CL_DEVICE_TYPE_ALL)))
		{
			return CL_INVALID_DEVICE_TYPE;	
		}

		*num_devices = 0;

		for(std::vector<cl_device_id>::iterator it = FreeOCL::devices.begin(); it != FreeOCL::devices.end() && *num_devices < num_entries; ++it)
		{
			if (device_type & (it->device_type | CL_DEVICE_TYPE_DEFAULT))
			{
				if (devices != NULL)
					devices[*num_devices] = FreeOCL::device;
				if (num_devices != NULL)
					++(*num_devices);
			}
		}

		if (*num_devices == 0)
			return CL_DEVICE_NOT_FOUND;

		return CL_SUCCESS;
	}

	CL_API_ENTRY cl_int CL_API_CALL	clCreateSubDevicesFCL(cl_device_id in_device,
							      const cl_device_partition_property * properties,
							      cl_uint                              num_devices,
							      cl_device_id *                       out_devices,
							      cl_uint *                            num_devices_ret) CL_API_SUFFIX__VERSION_1_2
	{
		MSG(clCreateSubDevicesFCL);
		return CL_INVALID_DEVICE;
	}

	CL_API_ENTRY cl_int CL_API_CALL	clRetainDeviceFCL(cl_device_id device) CL_API_SUFFIX__VERSION_1_2
	{
		MSG(clRetainDeviceFCL);
		if (device != FreeOCL::device)
			return CL_INVALID_DEVICE;
		return CL_SUCCESS;
	}

	CL_API_ENTRY cl_int CL_API_CALL	clReleaseDeviceFCL(cl_device_id device) CL_API_SUFFIX__VERSION_1_2
	{
		MSG(clReleaseDeviceFCL);
		if (device != FreeOCL::device)
			return CL_INVALID_DEVICE;
		return CL_SUCCESS;
	}
}

namespace FreeOCL
{
	std::vector<cl_device_id> devices;
	_cl_device_id_cpu freeocl_device;
	devices.push_back(freeocl_device);
}
