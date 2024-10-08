###################################################################################
#
# PMlib - Performance Monitor Library
#
# Copyright (c) 2010-2011 VCAD System Research Program, RIKEN.
# All rights reserved.
#
# Copyright (c) 2012-2020 Advanced Institute for Computational Science(AICS), RIKEN.
# All rights reserved.
#
# Copyright (c) 2016-2020 Research Institute for Information Technology(RIIT), Kyushu University.
# All rights reserved.
#
###################################################################################


set(pm_files
       PerfCpuType.cpp
       PerfMonitor.cpp
       PerfWatch.cpp
       PerfProgFortran.cpp
       PerfProgC.cpp
       PerfRecord.cpp
       SupportReportFortran.F90
       SupportReportCPP.cpp
       SupportReportC.c)

if(NOT with_MPI)
  add_library(PM STATIC ${pm_files})

  # pmVersion.hをサーチするためにBINARYを先に書く
  include_directories(${PROJECT_BINARY_DIR}/include ${PROJECT_SOURCE_DIR}/include)

  install(TARGETS PM DESTINATION lib)

else()
  add_library(PMmpi STATIC ${pm_files})
  include_directories(${PROJECT_BINARY_DIR}/include)
  include_directories(${PROJECT_SOURCE_DIR}/include)

  install(TARGETS PMmpi DESTINATION lib)

endif()

if(with_PAPI)
  # header ファイルはPerfWatchクラスなどをコンパイルする場合にも参照される
  include_directories(${PAPI_DIR}/include)
endif()

if(with_POWER)
  include_directories(${POWER_DIR}/include)
endif()

install(FILES ${PROJECT_SOURCE_DIR}/include/mpi_stubs.h
              ${PROJECT_SOURCE_DIR}/include/PerfMonitor.h
              ${PROJECT_SOURCE_DIR}/include/PerfWatch.h
              ${PROJECT_SOURCE_DIR}/include/pmlib_otf.h
              ${PROJECT_SOURCE_DIR}/include/pmlib_papi.h
              ${PROJECT_SOURCE_DIR}/include/pmlib_power.h
              ${PROJECT_SOURCE_DIR}/include/pmlib_api_C.h
              ${PROJECT_BINARY_DIR}/include/pmVersion.h
        DESTINATION include )
