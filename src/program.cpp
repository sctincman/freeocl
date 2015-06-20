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
#include "program.h"
#include "context.h"
#include <cstring>
#include <dlfcn.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "codebuilder.h"
#include <sstream>
#include <fstream>
#include <set>

#define SET_STRING(X)	FreeOCL::copy_memory_within_limits(X, strlen(X) + 1, param_value_size, param_value, param_value_size_ret)
#define SET_VAR(X)	FreeOCL::copy_memory_within_limits(&(X), sizeof(X),\
														param_value_size,\
														param_value,\
														param_value_size_ret)
#define SET_RET(X)	if (errcode_ret)	*errcode_ret = (X)

extern "C"
{
	cl_program clCreateProgramWithSourceFCL (cl_context context,
										  cl_uint count,
										  const char **strings,
										  const size_t *lengths,
										  cl_int *errcode_ret)
	{
		MSG(clCreateProgramWithSourceFCL);
		if (count == 0 || strings == NULL)
		{
			SET_RET(CL_INVALID_VALUE);
			return 0;
		}
		std::string source_code;
		for(size_t i = 0 ; i < count ; ++i)
		{
			if (strings[i] == NULL)
			{
				SET_RET(CL_INVALID_VALUE);
				return 0;
			}
			const size_t len = (lengths == NULL || lengths[i] == 0) ? strlen(strings[i]) : lengths[i];
			source_code.append(strings[i], len);
		}

		FreeOCL::unlocker unlock;
		if (!FreeOCL::is_valid(context))
		{
			SET_RET(CL_INVALID_CONTEXT);
			return 0;
		}
		unlock.handle(context);

		cl_program program = new _cl_program(context);
		program->source_code.swap(source_code);
		SET_RET(CL_SUCCESS);

		return program;
	}

	cl_program clCreateProgramWithBinaryFCL (cl_context context,
										  cl_uint num_devices,
										  const cl_device_id *device_list,
										  const size_t *lengths,
										  const unsigned char **binaries,
										  cl_int *binary_status,
										  cl_int *errcode_ret)
	{
		MSG(clCreateProgramWithBinaryFCL);
		if (num_devices == 0 || device_list == NULL
				|| lengths == NULL || binaries == NULL)
		{
			SET_RET(CL_INVALID_VALUE);
			return 0;
		}
		if (binary_status)
		{
			bool b_error = false;
			for(size_t i = 0 ; i < num_devices ; ++i)
			{
				if (lengths[i] == 0 || binaries[i] == NULL)
				{
					binary_status[i] = CL_INVALID_VALUE;
					b_error = true;
					continue;
				}
				binary_status[i] = CL_SUCCESS;
			}
			if (b_error)
			{
				SET_RET(CL_INVALID_VALUE);
				return 0;
			}
		}

		FreeOCL::unlocker unlock;
		if (!FreeOCL::is_valid(context))
		{
			SET_RET(CL_INVALID_CONTEXT);
			return 0;
		}
		unlock.handle(context);

		std::set<size_t> device_targets;

		for(size_t i = 0 ; i < num_devices ; ++i)
		{
			if(std::find(context->devices.begin(), context->devices.end(), device_list[i]) != context->devices.end())
			{
				device_targets.insert(i);
			}
			else
			{
				SET_RET(CL_INVALID_DEVICE);
				return 0;
			}
		}

		cl_program program = new _cl_program(context);

		for(std::set<size_t>::iterator it = device_targets.begin(); it != device_targets.end(); ++it)
		{
			_cl_program_binary *binary = program->binaries[device_list[*it]];
			binary->device = device_list[*it];
			binary->build_status = CL_BUILD_SUCCESS;
			binary->handle = NULL;

			const unsigned char *ptr = binaries[*it];
			size_t offset = 0;
			binary->binary_type = *(const cl_program_binary_type*)ptr;	offset += sizeof(cl_program_binary_type);
			const size_t size_of_binary_data = *(const size_t*)(ptr + offset);	offset += sizeof(size_t);
			// Creates a unique temporary file to write the binary data
			size_t n = 0;
			int fd_out = -1;
			std::string filename_out;
			while(fd_out == -1)
			{
				++n;
				if (n > 0x10000)
				{
					delete program;
					SET_RET(CL_OUT_OF_RESOURCES);
					return 0;
				}
				char buf[1024];
				filename_out = tmpnam(buf);
				switch(binary->binary_type)
				{
				case CL_PROGRAM_BINARY_TYPE_COMPILED_OBJECT:
					filename_out += ".o";
					break;
				case CL_PROGRAM_BINARY_TYPE_LIBRARY:
					filename_out += ".a";
					break;
				case CL_PROGRAM_BINARY_TYPE_EXECUTABLE:
				default:
					filename_out += ".so";
				}

				fd_out = open(filename_out.c_str(), O_EXCL | O_CREAT | O_WRONLY, S_IWUSR | S_IRUSR | S_IXUSR);
			}
			const size_t written_bytes = write(fd_out, ptr + offset, size_of_binary_data);	offset += size_of_binary_data;
			close(fd_out);
			binary->binary_file = filename_out;

			const size_t number_of_kernels = *(const size_t*)(ptr + offset);	offset += sizeof(size_t);
			for(size_t i = 0 ; i < number_of_kernels ; ++i)
			{
				const size_t kernel_name_size = *(const size_t*)(ptr + offset);	offset += sizeof(size_t);
				program->kernel_names.insert(std::string((const char*)(ptr + offset), kernel_name_size));	offset += kernel_name_size;
			}

			const size_t build_options_size = *(const size_t*)(ptr + offset);	offset += sizeof(size_t);
			binary->build_options = std::string((const char*)(ptr + offset), build_options_size);

			if (binary->binary_type == CL_PROGRAM_BINARY_TYPE_EXECUTABLE)
			{
				binary->handle = dlopen(binary->binary_file.c_str(), RTLD_NOW | RTLD_LOCAL);
				if (!binary->handle)
				{
					remove(binary->binary_file.c_str());
					delete program;
					SET_RET(CL_INVALID_BINARY);
					return 0;
				}
			}

		}

		SET_RET(CL_SUCCESS);
		return program;
	}

	cl_int clRetainProgramFCL (cl_program program)
	{
		MSG(clRetainProgramFCL);
		if (!FreeOCL::is_valid(program))
			return CL_INVALID_PROGRAM;
		program->retain();
		program->unlock();
		return CL_SUCCESS;
	}

	cl_int clReleaseProgramFCL (cl_program program)
	{
		MSG(clReleaseProgramFCL);
		if (!FreeOCL::is_valid(program))
			return CL_INVALID_PROGRAM;
		program->release();
		if (program->get_ref_count() == 0)
		{
			program->invalidate();
			program->unlock();
			delete program;
		}
		else
			program->unlock();
		return CL_SUCCESS;
	}

	cl_int clBuildProgramFCL (cl_program program,
						   cl_uint num_devices,
						   const cl_device_id *device_list,
						   const char *options,
						   void (CL_CALLBACK *pfn_notify)(cl_program program,
														  void *user_data),
						   void *user_data)
	{
		MSG(clBuildProgramFCL);
		if (device_list == NULL && num_devices > 0)
			return CL_INVALID_VALUE;
		if (device_list != NULL && num_devices == 0)
			return CL_INVALID_VALUE;
		if (pfn_notify == NULL && user_data != NULL)
			return CL_INVALID_VALUE;

		if (!FreeOCL::is_valid(program))
			return CL_INVALID_PROGRAM;

		std::vector<cl_device_id> devices;

		if (device_list == NULL)
		{
			for(std::vector<cl_device_id> it = program->context->devices.begin(); it != program->context->devices.end(); ++it)
			{
				_cl_program_binary *binary = program->binaries[it];
				binary->device = it;
				devices.push_back(it);
			}
		}
		else
		{
			for(size_t i = 0; i < num_devices; ++i)
			{
				if((std::find(program->context->devices.begin(), program->context->devices.end(), device_list[i]) != program->context->devices.end()))
				{
					_cl_program_binary *binary = program->binaries[device_list[i]];
					binary->device = device_list[i];
					devices.push_back(device_list[i])
				}
				else
				{
					program->unlock();
					return CL_INVALID_DEVICE;
				}
			}
		}

		program->retain();

		for(std::vector<cl_device_id> it = devices.begin(); it != devices.end(); ++it)
		{
			_cl_program_binary *binary = program->binaries[it];
			if (binary->build_status == CL_BUILD_IN_PROGRESS || program->kernels_attached > 0)
			{
				program->unlock();
				return CL_INVALID_OPERATION;
			}

			if (binary->binary_type != CL_PROGRAM_BINARY_TYPE_NONE
			    && program->source_code.empty()
			    && binary->build_status != CL_BUILD_SUCCESS)
			{
				program->unlock();
				return CL_INVALID_BINARY;
			}

			binary->build_status = CL_BUILD_IN_PROGRESS;
			const std::string &source_code = program->source_code;

			if (binary->handle)
				dlclose(binary->handle);
			if (!binary->binary_file.empty())
				remove(binary->binary_file.c_str());
			binary->handle = NULL;
			binary->binary_file.clear();
			program->unlock();

			std::stringstream build_log;
			bool b_valid_options = true;
			FreeOCL::set<std::string> kernel_names;
			const std::string binary_file = FreeOCL::build_program(binary->device,
									       options ? options : std::string(),
									       source_code,
									       build_log,
									       kernel_names,
									       b_valid_options,
									       false,
									       FreeOCL::map<std::string, std::string>(),
									       &(binary->temporary_file));

			if (!b_valid_options)
			{
				binary->build_status = CL_BUILD_ERROR;
				return CL_INVALID_BUILD_OPTIONS;
			}

			if (!FreeOCL::is_valid(program))
			{
				if (!binary_file.empty())
					remove(binary_file.c_str());
				return CL_INVALID_PROGRAM;
			}

			binary->binary_file = binary_file;
			binary->build_log = build_log.str();

			if (binary->binary_file.empty())
			{
				// In case of error do not delete temporary code file
				binary->temporary_file.clear();
				binary->build_status = CL_BUILD_ERROR;
				program->unlock();
				clReleaseProgramFCL(program);
				return CL_BUILD_PROGRAM_FAILURE;
			}

			binary->handle = dlopen(binary_file.c_str(), RTLD_NOW | RTLD_LOCAL);
			if (!binary->handle)
			{
				// In case of error do not delete temporary code file
				binary->temporary_file.clear();
				remove(binary->binary_file.c_str());
				binary->binary_file.clear();
				binary->build_status = CL_BUILD_ERROR;
				program->unlock();
				clReleaseProgramFCL(program);
				return CL_BUILD_PROGRAM_FAILURE;
			}

			program->kernel_names = kernel_names;

			binary->build_options = options ? options : "";
			binary->build_status = CL_BUILD_SUCCESS;
			binary->binary_type = CL_PROGRAM_BINARY_TYPE_EXECUTABLE;
		}
		program->unlock();
		if (pfn_notify)
			pfn_notify(program, user_data);
		clReleaseProgramFCL(program);
		return CL_SUCCESS;
	}

	cl_int clGetProgramInfoFCL (cl_program program,
							 cl_program_info param_name,
							 size_t param_value_size,
							 void *param_value,
							 size_t *param_value_size_ret)
	{
		MSG(clGetProgramInfoFCL);
		FreeOCL::unlocker unlock;
		if (!FreeOCL::is_valid(program))
			return CL_INVALID_PROGRAM;
		unlock.handle(program);

		bool bTooSmall = false;
		switch(param_name)
		{
		case CL_PROGRAM_REFERENCE_COUNT:	bTooSmall = SET_VAR(program->get_ref_count());	break;
		case CL_PROGRAM_CONTEXT:			bTooSmall = SET_VAR(program->context);	break;
		case CL_PROGRAM_NUM_DEVICES:
			{
				const cl_uint nb = program->binaries.size();
				bTooSmall = SET_VAR(nb);
			}
			break;
		case CL_PROGRAM_DEVICES:
			std::vector<cl_device_id> devices;
			for(std::map<cl_device_id, _cl_program_binary> it = program->binaries.begin()
				    ; it != program->binaries.end()
				    ; ++it)
				devices.push_back(it->first);
			bTooSmall = FreeOCL::copy_memory_within_limits(&(devices.front()),
								       devices.size() * sizeof(cl_device_id),
								       param_value_size,
								       param_value,
								       param_value_size_ret);
			break;
		case CL_PROGRAM_SOURCE:
			bTooSmall = FreeOCL::copy_memory_within_limits(program->source_code.c_str(),
														   program->source_code.size() + 1,
														   param_value_size,
														   param_value,
														   param_value_size_ret);
			break;
		case CL_PROGRAM_BINARY_SIZES:
			{
				std::vector<size_t> sizes;

				size_t kernel_names_size = 0;
				for(FreeOCL::set<std::string>::const_iterator it = program->kernel_names.begin()
					    ; it != program->kernel_names.end()
					    ; ++it)
					kernel_names_size += sizeof(size_t) + it->size();

				for(std::map<cl_device_id, _cl_program_binary> it = program->binaries.begin()
					    ; it != program->binaries.end()
					    ; ++it)
				{
					struct stat file_stat;
					file_stat.st_size = 0;
					stat(it->binary_file.c_str(), &file_stat);
					const size_t binary_file_size = file_stat.st_size;
					const size_t binary_size = sizeof(it->binary_type)
						+ sizeof(size_t) + binary_file_size
						+ sizeof(size_t) + kernel_names_size
						+ sizeof(size_t) + it->build_options.size();
					sizes.push_back(binary_size);
				}

				bTooSmall = FreeOCL::copy_memory_within_limits(&(sizes.front()),
									       sizes.size() * sizeof(size_t),
									       param_value_size,
									       param_value,
									       param_value_size_ret);
			}
			break;
		case CL_PROGRAM_BINARIES:
			if (param_value == NULL)
				break;
			size_t i = 0;
			std::map<cl_device_id, _cl_program_binary> it;
			for(i = 0, it = program->binaries.begin()
				    ; it != program->binaries.end()
				    ; ++it, ++i)
			{
				char *ptr = ((char**)param_value)[i];
				// Skip device
				if (!ptr)
					continue;
				size_t offset = 0;
				// Write binary type
				*(cl_program_binary_type*)ptr = it->binary_type;	offset += sizeof(cl_program_binary_type);

				// Write binary data
				struct stat file_stat;
				file_stat.st_size = 0;
				stat(it->binary_file.c_str(), &file_stat);
				const size_t binary_file_size = file_stat.st_size;
				*(size_t*)(ptr + offset) = binary_file_size;	offset += sizeof(size_t);

				std::fstream binary_file(it->binary_file.c_str(), std::ios_base::in | std::ios_base::binary);
				binary_file.read(ptr + offset, binary_file_size);	offset += binary_file_size;
				binary_file.close();

				// Write kernel names
				*(size_t*)(ptr + offset) = program->kernel_names.size();	offset += sizeof(size_t);
				for(FreeOCL::set<std::string>::const_iterator it = program->kernel_names.begin()
					; it != program->kernel_names.end()
					; ++it)
				{
					*(size_t*)(ptr + offset) = it->size();	offset += sizeof(size_t);
					memcpy(ptr + offset, it->data(), it->size());	offset += it->size();
				}

				// Write build options
				*(size_t*)(ptr + offset) = it->build_options.size();	offset += sizeof(size_t);
				memcpy(ptr + offset, it->build_options.data(), it->build_options.size());	offset += it->build_options.size();
			}
			break;
		case CL_PROGRAM_NUM_KERNELS:
			for(std::map<cl_device_id, _cl_program_binary> it = program->binaries.begin()
					    ; it != program->binaries.end()
					    ; ++it)
			{
				if (it->build_status != CL_BUILD_SUCCESS)
					return CL_INVALID_PROGRAM_EXECUTABLE;
			}
			else
			{
				const size_t tmp = program->kernel_names.size();
				bTooSmall = SET_VAR(tmp);
			}
			break;
		case CL_PROGRAM_KERNEL_NAMES:
			if (!program->handle)
				return CL_INVALID_PROGRAM_EXECUTABLE;
			else
			{
				std::string names;
				for(FreeOCL::set<std::string>::const_iterator it = program->kernel_names.begin() ; it != program->kernel_names.end() ; ++it)
				{
					if (!names.empty())
						names += ';';
					names += *it;
				}
				bTooSmall = SET_STRING(names.c_str());
			}
			break;
		default:
			return CL_INVALID_VALUE;
		}

		if (bTooSmall && param_value != NULL)
			return CL_INVALID_VALUE;

		return CL_SUCCESS;
	}

	cl_int clGetProgramBuildInfoFCL (cl_program program,
								  cl_device_id device,
								  cl_program_build_info param_name,
								  size_t param_value_size,
								  void *param_value,
								  size_t *param_value_size_ret)
	{
		MSG(clGetProgramBuildInfoFCL);
		FreeOCL::unlocker unlock;
		if (!FreeOCL::is_valid(program))
			return CL_INVALID_PROGRAM;
		unlock.handle(program);

		if (!FreeOCL::is_valid(device))
			return CL_INVALID_DEVICE;

		if(std::find(program->binaries.begin(), program->binaries.end(), device) == program->binaries.end())
			return CL_INVALID_DEVICE;

		_cl_program_binary = program->binaries[device];

		bool bTooSmall = false;
		switch(param_name)
		{
		case CL_PROGRAM_BUILD_STATUS:		bTooSmall = SET_VAR(binary->build_status);	break;
		case CL_PROGRAM_BUILD_OPTIONS:
			bTooSmall = FreeOCL::copy_memory_within_limits(binary->build_options.c_str(),
								       binary->build_options.size() + 1,
								       param_value_size,
								       param_value,
								       param_value_size_ret);
			break;
		case CL_PROGRAM_BUILD_LOG:
			bTooSmall = FreeOCL::copy_memory_within_limits(binary->build_log.c_str(),
								       binary->build_log.size() + 1,
								       param_value_size,
								       param_value,
								       param_value_size_ret);
			break;
		case CL_PROGRAM_BINARY_TYPE:		bTooSmall = SET_VAR(binary->binary_type);	break;
		default:
			return CL_INVALID_VALUE;
		}
		if (bTooSmall && param_value != NULL)
			return CL_INVALID_VALUE;

		return CL_SUCCESS;
	}

	CL_API_ENTRY cl_program CL_API_CALL clCreateProgramWithBuiltInKernelsFCL(cl_context            context,
										 cl_uint               num_devices,
										 const cl_device_id *  device_list,
										 const char *          kernel_names,
										 cl_int *              errcode_ret) CL_API_SUFFIX__VERSION_1_2
	{
		MSG(clCreateProgramWithBuiltInKernelsFCL);
		if (device_list == NULL && num_devices > 0)
		{
			SET_RET(CL_INVALID_VALUE);
			return NULL;
		}
		if (!kernel_names)
		{
			SET_RET(CL_INVALID_VALUE);
			return NULL;
		}

		FreeOCL::unlocker unlock;
		if (!FreeOCL::is_valid(context))
		{
			SET_RET(CL_INVALID_CONTEXT);
			return NULL;
		}
		unlock.handle(context);

		std::set<size_t> device_targets;

		for(size_t i = 0 ; i < num_devices ; ++i)
		{
			if(std::find(context->devices.begin(), context->devices.end(), device_list[i]) != context->devices.end())
			{
				device_targets.insert(i);
			}
			else
			{
				SET_RET(CL_INVALID_DEVICE);
				return NULL;
			}
		}

		const std::deque<std::string> &splitted_kernel_names = FreeOCL::split(kernel_names, ";");
#ifdef RTLD_NOLOAD
		void *main_program = dlopen(NULL, RTLD_NOLOAD);
#else
        void *main_program = dlopen(NULL, 0);
#endif
        for(std::deque<std::string>::const_iterator i = splitted_kernel_names.begin() ; i != splitted_kernel_names.end() ; ++i)
		{
			if (!dlsym(main_program, ("__FCL_info_" + *i).c_str())
					|| !dlsym(main_program, ("__FCL_init_" + *i).c_str())
					|| !dlsym(main_program, ("__FCL_setwg_" + *i).c_str())
					|| !dlsym(main_program, ("__FCL_kernel_" + *i).c_str()))
			{
				SET_RET(CL_INVALID_VALUE);
				return NULL;
			}
		}

		cl_program program = new _cl_program(context);
		program->source_code.clear();
		FreeOCL::set<std::string> s_kernel_names;
		s_kernel_names.insert(splitted_kernel_names.begin(), splitted_kernel_names.end());
		program->kernel_names = s_kernel_names;

		for(std::vector<cl_device_id> it = device_targets.begin()
			    ; it != device_targets.end()
			    ; ++it)
		{
			_cl_program_binary *binary = program->binaries[it];
			binary->build_status = CL_BUILD_NONE;
			binary->handle = main_program;
			binary->binary_file.clear();
			binary->devices.push_back(FreeOCL::device);
			binary->build_status = CL_BUILD_SUCCESS;
		}

		SET_RET(CL_SUCCESS);
		return program;
	}

	CL_API_ENTRY cl_int CL_API_CALL	clCompileProgramFCL(cl_program           program,
							    cl_uint              num_devices,
							    const cl_device_id * device_list,
							    const char *         options,
							    cl_uint              num_input_headers,
							    const cl_program *   input_headers,
							    const char **        header_include_names,
							    void (CL_CALLBACK *  pfn_notify)(cl_program /* program */, void * /* user_data */),
							    void *               user_data) CL_API_SUFFIX__VERSION_1_2
	{
		MSG(clCompileProgramFCL);
		if (device_list == NULL && num_devices > 0)
			return CL_INVALID_VALUE;
		if (device_list != NULL && num_devices == 0)
			return CL_INVALID_VALUE;
		if (pfn_notify == NULL && user_data != NULL)
			return CL_INVALID_VALUE;
		if ((num_input_headers == 0 || input_headers == NULL || header_include_names == NULL)
				&& (num_input_headers != 0 || input_headers != NULL || header_include_names != NULL))
			return CL_INVALID_VALUE;

		if (!FreeOCL::is_valid(program))
			return CL_INVALID_PROGRAM;

		std::vector<cl_device_id> devices;

		if (device_list == NULL)
		{
			for(std::vector<cl_device_id> it = program->context->devices.begin(); it != program->context->devices.end(); ++it)
			{
				_cl_program_binary *binary = program->binaries[it];
				binary->device = it;
				devices.push_back(it);
			}
		}
		else
		{
			for(size_t i = 0; i < num_devices; ++i)
			{
				if((std::find(program->context->devices.begin(), program->context->devices.end(), device_list[i]) != program->context->devices.end()))
				{
					_cl_program_binary *binary = program->binaries[device_list[i]];
					binary->device = device_list[i];
					devices.push_back(device_list[i])
				}
				else
				{
					program->unlock();
					return CL_INVALID_DEVICE;
				}
			}
		}

		FreeOCL::map<std::string, std::string> headers;
		for(size_t i = 0 ; i < num_input_headers ; ++i)
		{
			if (input_headers[i] == NULL
					|| header_include_names[i] == NULL
					|| !FreeOCL::is_valid(input_headers[i]))
			{
				program->unlock();
				if (pfn_notify)	pfn_notify(program, user_data);
				return CL_INVALID_VALUE;
			}
			headers[header_include_names[i]] = input_headers[i]->source_code;
			input_headers[i]->unlock();
		}

		for(std::vector<cl_device_id> it = devices.begin(); it != devices.end(); ++it)
		{
			_cl_program_binary *binary = program->binaries[it];
			if (binary->build_status == CL_BUILD_IN_PROGRESS || program->kernels_attached > 0)
			{
				program->unlock();
				if (pfn_notify)	pfn_notify(program, user_data);
				return CL_INVALID_OPERATION;
			}

			binary->build_status = CL_BUILD_IN_PROGRESS;

			if (binary->handle)
				dlclose(binary->handle);
			if (!binary->binary_file.empty())
				remove(binary->binary_file.c_str());
			binary->handle = NULL;
			binary->binary_file.clear();

			const std::string &source_code = program->source_code;

			std::stringstream build_log;
			bool b_valid_options = true;
			FreeOCL::set<std::string> kernel_names;
			const std::string binary_file = FreeOCL::build_program(binary->device,
									       options ? options : std::string(),
									       source_code,
									       build_log,
									       kernel_names,
									       b_valid_options,
									       true,
									       headers,
									       &(binary->temporary_file));

			if (!b_valid_options)
			{
				binary->build_status = CL_BUILD_ERROR;
				binary->unlock();
				if (pfn_notify)	pfn_notify(program, user_data);
				return CL_INVALID_COMPILER_OPTIONS;
			}

			binary->binary_file = binary_file;
			binary->build_log = build_log.str();

			if (binary->binary_file.empty())
			{
				// Don't delete temporary files in case of errors
				binary->temporary_file.clear();
				binary->build_status = CL_BUILD_ERROR;
				if (pfn_notify)	pfn_notify(program, user_data);
				return CL_COMPILE_PROGRAM_FAILURE;
			}

			program->kernel_names = kernel_names;

			binary->build_options = options ? options : "";
			binary->build_status = CL_BUILD_SUCCESS;
			binary->binary_type = CL_PROGRAM_BINARY_TYPE_COMPILED_OBJECT;
		}

		program->unlock();
		if (pfn_notify)	pfn_notify(program, user_data);
		return CL_SUCCESS;
	}

	CL_API_ENTRY cl_program CL_API_CALL	clLinkProgramFCL(cl_context           context,
														 cl_uint              num_devices,
														 const cl_device_id * device_list,
														 const char *         options,
														 cl_uint              num_input_programs,
														 const cl_program *   input_programs,
														 void (CL_CALLBACK *  pfn_notify)(cl_program /* program */, void * /* user_data */),
														 void *               user_data,
														 cl_int *             errcode_ret) CL_API_SUFFIX__VERSION_1_2
	{
		MSG(clLinkProgramFCL);
		if (pfn_notify == NULL && user_data != NULL)
		{
			SET_RET(CL_INVALID_VALUE);
			return 0;
		}
		if ((num_devices == 0) || (device_list == NULL))
		{
			SET_RET(CL_INVALID_VALUE);
			return 0;
		}
		if (num_input_programs == 0 || input_programs == NULL)
		{
			SET_RET(CL_INVALID_VALUE);
			return 0;
		}
		FreeOCL::unlocker unlock;
		if (!FreeOCL::is_valid(context))
		{
			SET_RET(CL_INVALID_CONTEXT);
			return 0;
		}
		unlock.handle(context);

		for(size_t i = 0; i < num_devices; ++i)
		{
			if (std::find(context->devices.begin(), context->devices.end(), device_list[i]) == context->devices.end())
			{
				SET_RET(CL_INVALID_DEVICE);
				return 0;
			}
		}

		FreeOCL::set<std::string> kernel_names;
		std::vector<std::string> files_to_link;
		files_to_link.reserve(num_input_programs);
		for(size_t i = 0 ; i < num_input_programs ; ++i)
		{
			if (!FreeOCL::is_valid(input_programs[i]))
			{
				SET_RET(CL_INVALID_PROGRAM);
				return 0;
			}
			unlock.handle(input_programs[i]);
			for(std::map<cl_device_id, _cl_program_binary> it = program->binaries.begin()
				    ; it != program->binaries.end()
				    ; ++it)
			{
				if (it->binary_type == CL_PROGRAM_BINARY_TYPE_NONE
				    || it->binary_type == CL_PROGRAM_BINARY_TYPE_EXECUTABLE)
				{
					SET_RET(CL_INVALID_PROGRAM);
					return 0;
				}
				files_to_link.push_back(it->binary_file);
			}
			kernel_names.insert(input_programs[i]->kernel_names.begin(), input_programs[i]->kernel_names.end());
		}

		cl_program program = new _cl_program(context);
		for(std::map<cl_device_id, _cl_program_binary> it = program->binaries.begin()
			    ; it != program->binaries.end()
			    ; ++it)
		{
			std::stringstream log;
			bool b_valid_options = true;
			const std::string &binary_file = FreeOCL::link_program(it->device,
									       options ? options : "",
									       files_to_link,
									       log,
									       b_valid_options);
			if (!b_valid_options)
			{
				SET_RET(CL_INVALID_LINKER_OPTIONS);
				return 0;
			}
			if (binary_file.empty())
			{
				SET_RET(CL_LINK_PROGRAM_FAILURE);
				return 0;
			}

			it->binary_file = binary_file;
			it->binary_type = (*binary_file.rbegin() == 'a')
				? CL_PROGRAM_BINARY_TYPE_LIBRARY
				: CL_PROGRAM_BINARY_TYPE_EXECUTABLE;
			it->build_options = options ? options : "";
			it->build_log = log.str();
			program->kernel_names.swap(kernel_names);
			it->build_status = CL_BUILD_SUCCESS;

			if (it->binary_type == CL_PROGRAM_BINARY_TYPE_EXECUTABLE)
			{
				it->handle = dlopen(it->binary_file.c_str(), RTLD_NOW | RTLD_LOCAL);
				if (!it->handle)
				{
					delete program;
					SET_RET(CL_LINK_PROGRAM_FAILURE);
					return 0;
				}
			}
		}
		return program;
	}

	CL_API_ENTRY cl_int CL_API_CALL clUnloadPlatformCompilerFCL(cl_platform_id platform) CL_API_SUFFIX__VERSION_1_2
	{
		MSG(clUnloadPlatformCompilerFCL);
		if (platform != FreeOCL::platform)
			return CL_INVALID_PLATFORM;
		return CL_SUCCESS;
	}
}

_cl_program::_cl_program(cl_context context)
	: context_resource(context),
	  binary_type(CL_PROGRAM_BINARY_TYPE_NONE),
	  handle(NULL),
	  build_status(CL_BUILD_NONE),
	  kernels_attached(0)
{
	FreeOCL::global_mutex.lock();
	FreeOCL::valid_programs.insert(this);
	FreeOCL::global_mutex.unlock();
}

_cl_program::~_cl_program()
{
	FreeOCL::global_mutex.lock();
	FreeOCL::valid_programs.erase(this);
	FreeOCL::global_mutex.unlock();

	if (handle && !binary_file.empty())
		dlclose(handle);
	if (!binary_file.empty() && !getenv("FREEOCL_DEBUG"))
		remove(binary_file.c_str());
	if (!temporary_file.empty() && !getenv("FREEOCL_DEBUG"))
		remove(temporary_file.c_str());
}
