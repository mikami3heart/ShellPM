###################################################################################
#
# PMlib - Performance Monitor Library
#
###################################################################################

set(CMAKE_SYSTEM_NAME Linux)

include(CMakeForceCompiler)

if(with_MPI)
	set(CMAKE_C_COMPILER mpiicx)
	set(CMAKE_CXX_COMPILER mpiicpx)
	set(CMAKE_Fortran_COMPILER mpiifx)
else()
	set(CMAKE_C_COMPILER icx)
	set(CMAKE_CXX_COMPILER icpx)
	set(CMAKE_Fortran_COMPILER ifx)
endif()

# compiler location

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(TARGET_ARCH "Intel")

# libpapi.so and libpfm.so are under /opt/FJSVxos/devkit/aarch64/rfs/usr/lib64

