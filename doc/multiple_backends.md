# What is needed to implement multiple backends

The goal of this branch is to implement multiple backends for FreeOCL, with the immediate goal of adding Epiphany support in addition to the CPU backend. In theory, any GCC backend is a potential target (as long as a backend is defined for communicating with the hardware).

## Overall points to consider

### Dispatching backend specific versions of API functions
FreeOCL actually has a wonderfully thorough structure to it, with objects able to have their own dispatch table. The problem is currently there is only one dispatch table and it's replicated across many objects (for the purpose of the icd_api). Part of the work should go into cleaning up the use of the dispatch table (limiting what objects have a copy), and be able for these objects to change their dispatch table (for eg. an Epiphany device holds it's own dispatch table that includes pointers to Epiphany specific routines when needed, and the generic ones when not).

On this note, I think it'd be worthwhile moving more of the generic/error checking code from the `src/*.cpp` files to `src/icd/icd_api.cpp`.

### Buffers and memory management
For the CPU backend, everything is local and that assumption is made throughout. However, a naive but simple approach would be to leave the host-side unchanged (hence storing all the buffers host-side), and add backend specific versions of the device-side buffer functions that just fetch from host memory.

Ideally, the backend should handle memory management, and be able to move memory to/from the device as needed.

## src/device.{cpp,h}

This defines the devices available through the runtime, and how to query/control them through the API.

Implementing the `_cl_device_id` struct seems to be left to implementors. Client applications do not manipulate it themselves, but pass it to routines defined by the runtime (as only the runtime knows its definition).

FreeOCL currently implements `_cl_device_id` as an object, and uses the CPU backend as a singleton. Implementing backends will require an array of devices to be initialized, and procedures updated to probe and initialize all possible backends, as opposed to just returning/operating on the singleton implicitly.

### What needs to be done

#### `_cl_device_id` extended to include epiphany specific info

There are a few options here:

1. Backend specific variables can be added to this (further bloating this struct).
2. Each backend implements a device specific `_cl_device_id` that inherits from a generic _cl_device_id (requires casting whenever device specific operations needed).
3. Backend specific behavior defined in a separate FreeOCL object that is pointed to from the `_cl_device_id` (compositional objects).

FreeOCL currently has a single initializer for the singleton. Most of this seems CPU backend dependent, and can be separated. Separating device specific initialization will make the code cleaner, and the extra function calls should not be a concern.

For **2**, each device specific object just needs to implement its own initializer. **3** requires that, as well as adding the newly created object to `_cl_device_id` (likely through a void pointer).

This could be accomplished with interfaces, but that does not fit with the rest of the codebase (which seems to favor either regular/compositional inheritance).

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

## `src/freeocl.{h,cpp}

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

## `src/utils/commandqueue.cpp`

`clFlushFCL` is unneccessary for the CPU backend, but may be necessary for the Epiphany (if we are sending commands over the elink). This will need to be abstracted.

`proc` seems to be the meat of backend specific behavior which will consume commands from the buffer. Essentially this covers all host-side behavior. The buffer/mem routines will need to considered (see above).

`proc->CL_COMMAND_NDRANGE_KERNEL` assumes a threadpool (CPU backend). Change this to call a device specific routine for NDRANGE_KERNEL.