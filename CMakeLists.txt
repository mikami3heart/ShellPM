################################################################################
#
# ShellPM - Shell level Performance Monitor command
#
# Copyright (c) 2024 Riken Center for Computational Science - R-CCS
#
# All rights reserved.
#
################################################################################
#
#	cmake_minimum_required(VERSION 2.6)
cmake_minimum_required(VERSION 3.0)

#######
# Build options
#######

option (with_PAPI "Enable PAPI" "OFF")
option (with_POWER "Enable Power API" "OFF")
option (with_OTF "Enable tracing" "OFF")
option (enable_OPENMP "Enable OpenMP" "OFF")
option (enable_PreciseTimer "Enable PRECISE TIMER" "ON")

#######
# Project setting
#######

#	project(ShellPM C CXX Fortran)
project(ShellPM C CXX)

if(INSTALL_DIR STREQUAL "")
  set(INSTALL_DIR "/usr/local/ShellPM")
endif()

# project()でCMAKE_INSTALL_PREFIX=/usr/localとなるので、再定義
set (CMAKE_INSTALL_PREFIX "${INSTALL_DIR}")


#######
set(PROJECT_VERSION "0.1.0")
set(LIB_REVISION "20240905_1500")

message(" ")
message("${PROJECT_NAME} Version : ${PROJECT_VERSION}")
message(" ")
#######


#######
# Cmake module
#######

set(CMAKE_MODULE_PATH  ${PROJECT_SOURCE_DIR}/cmake)
include(${PROJECT_SOURCE_DIR}/cmake/CompileOptionSelector.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/pm_package.cmake)

#######
# Options
#######

AddOptimizeOption()

if(enable_Fortran)
  FreeForm()
endif()

checkOpenMP()

if(NOT with_MPI)
  SET(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -DDISABLE_MPI")
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDISABLE_MPI")
  SET(CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -DDISABLE_MPI")
endif()

if(enable_PreciseTimer)
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_PRECISE_TIMER")
endif()

#######
# Find libraries to depend
#######


#######
# Display options
#######

message(" ")
message( STATUS "Destination PATH  : "    ${CMAKE_INSTALL_PREFIX})
message( STATUS "CMAKE_C_COMPILER  : "    ${CMAKE_C_COMPILER})
message( STATUS "CMAKE_C_FLAGS     : "    ${CMAKE_C_FLAGS})
message( STATUS "CMAKE_CXX_COMPILER: "    ${CMAKE_CXX_COMPILER})
message( STATUS "CMAKE_CXX_FLAGS   : "    ${CMAKE_CXX_FLAGS})

if(enable_Fortran)
  message( STATUS "CMAKE_Fortran_COMPILER : "  ${CMAKE_Fortran_COMPILER})
  message( STATUS "CMAKE_Fortran_FLAGS    : "  ${CMAKE_Fortran_FLAGS})
endif()

message(" ")
message( STATUS "OpenMP            : "    ${enable_OPENMP})
message( STATUS "MPI               : "    ${with_MPI})
message( STATUS "PAPI              : "    ${with_PAPI})
message( STATUS "POWER             : "    ${with_POWER})
message( STATUS "OTF               : "    ${with_OTF})
message(" ")

if(USE_F_TCS STREQUAL "YES")
else()
  if(with_MPI)
    include(FindMPI)

    if(MPI_CXX_FOUND)
      message(" ")
      message("MPI-CXX-CMP  : ${MPI_CXX_COMPILER}")
      message("MPI-CXX-FLAGS: ${MPI_CXX_COMPILE_FLAGS}")
      message("MPI-CXX-INC  : ${MPI_CXX_INCLUDE_PATH}")
      message("MPI-LNK-FLAGS: ${MPI_CXX_LINK_FLAGS}")
      message("MPI-LIBS     : ${MPI_CXX_LIBRARIES}")
      message(" ")
    endif(MPI_CXX_FOUND)
  endif()
endif()


#######
# include PATH for libs
#######

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    )


add_definitions(-DHAVE_CONFIG_H)


#######
# PAPI
#######

if(NOT with_PAPI)
elseif(with_PAPI STREQUAL "yes")
  add_definitions(-DUSE_PAPI)
  set(OPT_PAPI "ON")
else()
  add_definitions(-DUSE_PAPI)
  set(OPT_PAPI "ON")
  set(PAPI_DIR "${with_PAPI}")
endif()

#######
# POWER
#######

if(NOT with_POWER)
elseif(with_POWER STREQUAL "yes")
  add_definitions(-DUSE_POWER)
  set(OPT_POWER "ON")
else()
  add_definitions(-DUSE_POWER)
  set(OPT_POWER "ON")
  set(POWER_DIR "${with_POWER}")
endif()


#######
# OTF
#######

if(NOT with_OTF)
elseif(with_OTF STREQUAL "yes")
  add_definitions(-DUSE_OTF)
  set(OPT_OTF "ON")
else()
  add_definitions(-DUSE_OTF)
  set(OPT_OTF "ON")
  set(OTF_DIR "${with_OTF}")
endif()


#######
# Check header files
#######

include(CheckIncludeFiles)

CHECK_INCLUDE_FILES(inttypes.h HAVE_INTTYPES_H)
CHECK_INCLUDE_FILES(memory.h HAVE_MEMORY_H)
CHECK_INCLUDE_FILES(stdint.h HAVE_STDINT_H)
CHECK_INCLUDE_FILES(stdlib.h HAVE_STDLIB_H)
CHECK_INCLUDE_FILES(strings.h HAVE_STRINGS_H)
CHECK_INCLUDE_FILES(string.h HAVE_STRING_H)
CHECK_INCLUDE_FILES(sys/stat.h HAVE_SYS_STAT_H)
CHECK_INCLUDE_FILES(sys/types.h HAVE_SYS_TYPES_H)
CHECK_INCLUDE_FILES(unistd.h HAVE_UNISTD_H)


#######
# Sub Directories
#######

add_subdirectory(src_pmlib)

if(OPT_PAPI)
  add_subdirectory(src_papi_ext)
endif()

if(OPT_POWER)
  add_subdirectory(src_power_ext)
endif()

if(OPT_OTF)
  add_subdirectory(src_otf_ext)
endif()

add_subdirectory(src_main)


#######
# configure files
#######

configure_file( config.h.in.cmake config.h )
configure_file( include/pmVersion.h.in ${PROJECT_BINARY_DIR}/include/pmVersion.h @ONLY)


#######
# Install files
#######

install(FILES  ChangeLog.md
               Readme.md
               License.txt
               ${PROJECT_BINARY_DIR}/CMakeCache.txt
        DESTINATION share)


#######
# Installer
#######

pm_package()
