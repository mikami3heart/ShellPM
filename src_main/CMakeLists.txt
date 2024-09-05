##################################################################################
#
# PMlib - Performance Monitor Library
#
# Copyright (c) 2010-2011 VCAD System Research Program, RIKEN.
# All rights reserved.
#
# Copyright (c) 2012-2020 RIKEN Center for Computational Science(R-CCS), RIKEN.
# All rights reserved.
#
# Copyright (c) 2016-2020 Research Institute for Information Technology(RIIT), Kyushu University.
# All rights reserved.
#
###################################################################################

message(" ")
message("src_main : ")
message(STATUS "PAPI_DIR            = " ${PAPI_DIR})
message(STATUS "POWER_DIR            = " ${POWER_DIR})
message(STATUS "OTF_DIR             = " ${OTF_DIR})
message(STATUS "with_MPI            = " ${with_MPI})

#message(STATUS "PROJECT_BINARY_DIR = " ${PROJECT_BINARY_DIR})
#message(STATUS "PROJECT_SOURCE_DIR = " ${PROJECT_SOURCE_DIR})
#message(STATUS "IMPLICIT_LINK_LIBS = " ${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES})
#message(STATUS "CFLAGS              = " ${CMAKE_C_FLAGS})
#message(STATUS "CXXFLAGS            = " ${CMAKE_CXX_FLAGS})
#message(STATUS "FortranFLAGS        = " ${CMAKE_Fortran_FLAGS})

include_directories(${PROJECT_BINARY_DIR}/include ${PROJECT_SOURCE_DIR}/include)

link_directories(${PROJECT_BINARY_DIR}/src_pmlib)

if(OPT_PAPI)
  include_directories(${PAPI_DIR}/include)
  link_directories(${PROJECT_BINARY_DIR}/src_papi_ext)
  link_directories(${PAPI_DIR}/lib)
  #	link_directories(${PAPI_DIR}/lib64)
endif()

if(OPT_POWER)
  # Power API option is available on supercomputer Fugaku only
  include_directories(${POWER_DIR}/include)
  link_directories(${PROJECT_BINARY_DIR}/src_power_ext)
  link_directories(${POWER_DIR}/lib64)
endif()

if(OPT_OTF)
  include_directories(${OTF_DIR}/include/open-trace-format)
  link_directories(${PROJECT_BINARY_DIR}/src_otf_ext)
  link_directories(${OTF_DIR}/lib)
endif()


### Shell level commands

#### shellpm_start

add_executable(shellpm_start ./start_pm/main_pmlib.cpp)

if(with_MPI)
  target_link_libraries(shellpm_start -lPMmpi)
else()
  target_link_libraries(shellpm_start -lPM)
endif()

# If static papi libraries are available, the following linker line is more portable
#	target_link_libraries(example1 -lpapi_ext -Wl,'-Bstatic,-lpapi,-lpfm,-Bdynamic')
# RedHat 8 default papi comes with shared libraries only. So link as following line.
#	target_link_libraries(example1 -lpapi_ext -Wl,'-lpapi,-lpfm')

if(OPT_PAPI)
  if(TARGET_ARCH STREQUAL "FUGAKU")
    target_link_libraries(shellpm_start -lpapi_ext -lpapi -lpfm -Nnofjprof)
  else()
    #	GNU linker ld options are better passed using -Wl, rather than -B compiler phase option
    target_link_libraries(shellpm_start -lpapi_ext -Wl,'-lpapi,-lpfm')
  endif()
endif()

if(OPT_POWER)
  if(TARGET_ARCH STREQUAL "FUGAKU")
    target_link_libraries(shellpm_start -lpower_ext -lpwr )
  endif()
endif()

if(OPT_OTF)
  target_link_libraries(shellpm_start -lotf_ext -lopen-trace-format)
endif()



#### shellpm_report

add_executable(shellpm_report ./stop_pm/main_pmlib.cpp)

if(with_MPI)
  target_link_libraries(shellpm_report -lPMmpi)
else()
  target_link_libraries(shellpm_report -lPM)
endif()


if(OPT_PAPI)
  if(TARGET_ARCH STREQUAL "FUGAKU")
    target_link_libraries(shellpm_report -lpapi_ext -lpapi -lpfm -Nnofjprof)
  else()
    target_link_libraries(shellpm_report -lpapi_ext -Wl,'-lpapi,-lpfm')
  endif()
endif()

if(OPT_POWER)
  if(TARGET_ARCH STREQUAL "FUGAKU")
    target_link_libraries(shellpm_report -lpower_ext -lpwr )
  endif()
endif()

if(OPT_OTF)
  target_link_libraries(shellpm_report -lotf_ext -lopen-trace-format)
endif()

### end
