# What is needed to implement multiple backends

The goal of this branch is to implement multiple backends for FreeOCL, with the immediate goal of adding Epiphany support in addition to the CPU backend. In theory, any GCC backend is a potential target (as long as a backend is defined for communicating with the hardware).

## Overall points to consider

### Dispatching backend specific versions of API functions
FreeOCL actually has a wonderfully thorough structure to it, with objects able to have their own dispatch table. The problem is currently there is only one dispatch table present as a singleton, and access is granted across many objects (for the purpose of the icd_api). Work needs to be done to move away from the singleton and allow objects to change their dispatch table (for eg. an Epiphany device holds it's own dispatch table that includes pointers to Epiphany specific routines when needed, and the generic ones when not). The hurdle to this is the icd_api, which needs to access the dispatch table.

On this note, I think it'd be worthwhile moving more of the generic/error checking code from the `src/*.cpp` files to `src/icd/icd_api.cpp`. This is already redundantly repeated (to make sure the object is not null, in order to access it's dispatch table).

### Buffers and memory management
For the CPU backend, everything is local and that assumption is made throughout. However, a naive but simple approach would be to leave the host-side unchanged (hence storing all the buffers host-side), and add backend specific versions of the device-side buffer functions that just fetch from host memory.

Ideally, the backend should handle memory management, and be able to move memory to/from the device as needed.

### Kernel Functions

The series of `_FCL_xxxx` functions configure/query kernel properties. These are implemented as functions generated from the kernels, compiled, then `dlsymed` into the main program.

`_FCL_info` returns info about the kernel arguments, and this is only called when creating a kernel object. This can likely be abstracted into an object to allow backend specific implementations.

`_FCL_init` only sets the global kernel parameters and possibly setting a sync flag. This can also be abstracted into a kernel specific routine.

`_FCL_setwg` similar to above, just sets some global parameters.

`_FCL_kernel` calls the actual kernel function, once for each thread.

These are all CPU backend specific, and deal with the `include/FreeOCL/workitem.h`, which implements the CPU backend behavior for the device kernel. The host-side application should probably not be calling these directly, but a backend specific "get_kernel_info" or "init_kernels" that call backend specific routines. 

## `src/device.{cpp,h}`

This defines the devices available through the runtime, and how to query/control them through the API.

Implementing the `_cl_device_id` struct seems to be left to implementors. Client applications do not manipulate it themselves, but pass it to routines defined by the runtime (as only the runtime knows its definition).

FreeOCL currently implements `_cl_device_id` as an object, and uses the CPU backend as a singleton. Implementing backends will require an array of devices to be initialized, and procedures updated to probe and initialize all possible backends, as opposed to just returning/operating on the singleton implicitly. FreeOCL currently has a single initializer for the singleton.

The CPU backend dependent initializer was separated out into a separate object.

Each device is implemented as a child object that extends the `cl_device_id`.

### What needs to be done

#### `_cl_device_id` extended to include epiphany specific info

A CPU backend was added, and a similar Epiphany object is needed.

#### `_cl_device_id` extended to include compiler info

Each device will have a specific compiler requirements, and this is the best place to place them

## src/codebuilder.cpp

This defines the FreeOCL specific routines to compile kernels. Currently the compiler is hardcoded when building FreeOCL, but can be overridden by an environmental variable.

### What needs to be done

#### Compiler choice

This ties in with src/program.{h,cpp} and src/device.{h,cpp}

The ability to override the compiler at runtime through an environmental variable is desirable, but each device will need the ability to change the default compiler.

Each device will need to add a "compiler" field. `build_program` needs a way to figure this out, and the only logical way is to add the target device as a parameter. As this is called from `clBuildProgramFCL` and `clCompileProgramFCL`, these will need to be changed to do so (which fits with those routines needing to be changed to build for the list of devices given).

#### FreeOCL boilerplate code

This is hardcoded in `src/codebuilder.cpp`, and will need to be checked for CPU specific things (such as memory management).

This may necessitate backend specific boilerplate code to be written, and a way for this to be selected at runtime.

It wouldn't hurt to move this out into a separate file that is either included at compile time (possibly complicated, but fast), or read at runtime (easy but slow).

## `src/context.cpp`

`clCreateContextFromTypeFCL` hardcoded to return `CL_DEVICE_NOT_FOUND` for anything other than `CL_DEVICE_TYPE_CPU`. This needs to be changed. Likely just iterate the device array and check the type, collecting any devices that match.

## `src/freeocl.{h,cpp}`

`src/freeocl.h` contains a reference to the singleton `_cl_device_id`, will need to change this to the array.

## `src/image.cpp`

Refers to the FreeOCL singleton in clCreateImage{,2D,3D}FCL, when should be iterating through the context's array of devices, and this also only needs to know the devices max dimensions for images.

The rest of this seems to use the structs from `src/mem.{h,cpp}`.

## `src/kernel.cpp`

New kernels are `dlsym`ed with the `FCL_xxxx` functions generated by `src/codebuilder.cpp`. Either the `FCL_xxxx` functions need to contain the methods to start execution of the kernel on the devices, or kernel execution needs to be changed.

`clGetKernelWorkGroupInfoFCL` has some hard-coded return values, but I'm not sure if these are backend dependent.

`clGetKernelArgInfoFCL` mentions the access qualifiers for kernels. The FreeOCL CPU backend doesn't seem to distinguish between them, but for other backends, especially Epiphany, this may need to be treated specially.

## `src/mem.cpp`

The buffer objects seem to be a mostly host-side thing here. Most of the "interesting" behavior would be in the `src/utils/commandqueue.cpp` where the operations are actually carried out.

## `src/program.cpp`

The program will `dlclose` to cleanup program termination, and this may need to change for different backends, depending how the backends are implemented

`clLinkProgramFCL` and friends will `dlopen` the binary of the kernel. For Epiphany we won't need to do this. This may be backend dependent, and rely on calling a device specific `load_program` routine (for eg, Epiphany would call the proper `ehal` routines to upload the kernel)

A context and program object can be created for multiple devices. There is also a 1:1 relationship between devices and binary objects for the program. So far, program only stores a single binary object. This needs to be updated to hold a binary object per device, and the requisite queries and setter functions updated to reflect this.

Currently working on making this a separate struct, `_cl_program_binary`, with a program object holding a vector of them. Currently the `cl_device_id` is a member of the binary struct, but I may need to change this, as I'm having difficulty writing the methods. I may need to make it a set with the `cl_device_id` as a key.

## `src/utils/commandqueue.cpp`

`clFlushFCL` is unneccessary for the CPU backend, but may be necessary for the Epiphany (if we are sending commands over the elink). This will need to be abstracted.

`proc` seems to be the meat of backend specific behavior which will consume commands from the buffer. Essentially this covers all host-side behavior. The buffer/mem routines will need to considered (see above).

`proc->CL_COMMAND_NDRANGE_KERNEL` assumes a threadpool (CPU backend). Change this to call a device specific routine for NDRANGE_KERNEL.