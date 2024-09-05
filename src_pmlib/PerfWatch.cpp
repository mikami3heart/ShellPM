/*
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
 */

//! @file   PerfWatch.cpp
//! @brief  PerfWatch class

// When compiling with USE_PAPI macro, openmp option should be enabled.
#include <string>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <iostream>

#ifdef DISABLE_MPI
#include "mpi_stubs.h"
#else
#include <mpi.h>
#endif

#include "PerfWatch.h"

extern void sortPapiCounterList ();
extern void outputPapiCounterHeader (FILE*, std::string);
extern void outputPapiCounterList (FILE*);
extern void outputPapiCounterLegend (FILE*);


namespace pm_lib {

  struct pmlib_papi_chooser papi;
  struct hwpc_group_chooser hwpc_group;
  double cpu_clock_freq;        /// processor clock frequency, i.e. Hz
  double second_per_cycle;  /// real time to take each cycle
  struct pmlib_power_chooser power;

  ///
  /// 単位変換.
  ///
  ///   @param[in] fops 浮動小数点演算量又はデータ移動量
  ///   @param[out] unit 単位の文字列
  ///   @param[in] is_unit ユーザー申告値かHWPC自動測定値かの指定
  ///              = 0: ユーザが引数で指定したデータ移動量(バイト)
  ///              = 1: ユーザが引数で指定した演算量(浮動小数点演算量)
  ///              = 2: HWPC が自動測定する data access bandwidth event
  ///              = 3: HWPC が自動測定する flops event
  ///              = 4: HWPC が自動測定する vectorization (SSE, AVX, SVE, etc)
  ///              = 5: HWPC が自動測定する cache hit, miss,
  ///              = 6: HWPC が自動測定する cycles, instructions
  ///              = 7: HWPC が自動測定する load/store instruction type
  ///   @return  単位変換後の数値
  ///
  ///   @note is_unitは通常PerfWatch::statsSwitch()で事前に決定されている
  ///
  double PerfWatch::unitFlop(double fops, std::string &unit, int is_unit)
  {

    double P, T, G, M, K, ret=0.0;
    K = 1000.0;
    M = 1000.0*K;
    G = 1000.0*M;
    T = 1000.0*G;
    P = 1000.0*T;

    if ( (is_unit == 0) || (is_unit == 2) )  {
      if      ( fops > P ) {
        ret = fops / P;
        unit = "PB/sec";
      }
      else if ( fops > T ) {
        ret = fops / T;
        unit = "TB/sec";
      }
      else if ( fops > G ) {
        ret = fops / G;
        unit = "GB/sec";
      }
      else {
        ret = fops / M;
        unit = "MB/sec";
      }
    } else

    if ( (is_unit == 1) || (is_unit == 3) )  {
      if      ( fops > P ) {
        ret = fops / P;
        unit = "Pflops";
      }
      else if ( fops > T ) {
        ret = fops / T;
        unit = "Tflops";
      }
      else if ( fops > G ) {
        ret = fops / G;
        unit = "Gflops";
      }
      else {
        ret = fops / M;
        unit = "Mflops";
      }
    } else

    if ( (is_unit == 4) || (is_unit == 5) || (is_unit == 7) )  {
        ret = fops;
        unit = "(%)";
    } else

    if ( is_unit == 6 )  {
      if      ( fops > P ) {
        ret = fops / P;
        unit = "P.ips";
      }
      else if ( fops > T ) {
        ret = fops / T;
        unit = "T.ips";
      }
      else if ( fops > G ) {
        ret = fops / G;
        unit = "G.ips";
      }
      else {
        ret = fops / M;
        unit = "M.ips";
      }
    }

    return ret;
  }



  /// Statistics among processes
  /// Translate in Japanese later on...
  /// 全プロセスの測定結果の平均値・標準偏差などの基礎的な統計計算
  /// 測定区間の呼び出し回数はプロセス毎に異なる場合がありえる
  ///
  void PerfWatch::statsAverage()
  {
	//	if (my_rank != 0) return;	// This was a bad idea. All ranks should compute the stats.

	// 平均値
	m_time_av = 0.0;
	m_flop_av = 0.0;
	for (int i = 0; i < num_process; i++) {
		m_time_av += m_timeArray[i];
		m_flop_av += m_flopArray[i];
	}
	m_time_av /= num_process;
	m_flop_av /= num_process;

	if (m_in_parallel) {
		m_count_av = lround((double)m_count_sum / (double)num_process);
	} else {
		m_count_av = lround((double)m_count_sum / (double)num_process);
	}
	
	// 標準偏差
	m_time_sd = 0.0;
	m_flop_sd = 0.0;
	if (num_process > 1) {
		for (int i = 0; i < num_process; i++) {
			double d_time = m_timeArray[i] - m_time_av;
			double d_flop = m_flopArray[i] - m_flop_av;
			m_time_sd += d_time*d_time;
			m_flop_sd += d_flop*d_flop;
		}
		m_time_sd = sqrt(m_time_sd / (num_process-1));
		m_flop_sd = sqrt(m_flop_sd / (num_process-1));
	}

	// 通信の場合，各ノードの通信時間の最大値
	m_time_comm = 0.0;
	if (m_typeCalc == 0) {
		double comm_max = 0.0;
		for (int i = 0; i < num_process; i++) {
			if (m_timeArray[i] > comm_max) comm_max = m_timeArray[i];
		}
		m_time_comm = comm_max;
	}
  }


  /// 計算量の選択を行う
  ///
  /// @return
  ///   0: ユーザが引数で指定したデータ移動量(バイト)を採用する
  ///   1: ユーザが引数で指定した計算量を採用する "Flops"
  ///   2: HWPC が自動的に測定する data access bandwidth
  ///   3: HWPC が自動的に測定する flops event
  ///   4: HWPC が自動的に測定する vectorized f.p. (SSE, AVX, SVE, etc) event
  ///   5: HWPC が自動的に測定する cache hit, miss
  ///   6: HWPC が自動的に測定する cycles, instructions
  ///   7: HWPC が自動的に測定する load/store instruction type
  ///
  /// @note
  /// 計算量としてユーザー申告値を用いるかHWPC計測値を用いるかの決定を行う
  /// 環境変数HWPC_CHOOSERの値が優先される
  ///
  int PerfWatch::statsSwitch()
  {
    int is_unit;

    // 0: user set bandwidth
    // 1: user set flop counts
    // 2: BANDWIDTH : HWPC measured data access bandwidth
    // 3: FLOPS     : HWPC measured flop counts
    // 4: VECTOR    : HWPC measured vectorization
    // 5: CACHE     : HWPC measured cache hit/miss
    // 6: CYCLE     : HWPC measured cycles, instructions
    // 7: LOADSTORE : HWPC measured load/store instruction type

    if (hwpc_group.number[I_bandwidth] > 0) {
      is_unit=2;
    } else if (hwpc_group.number[I_flops]  > 0) {
      is_unit=3;
    } else if (hwpc_group.number[I_vector] > 0) {
      is_unit=4;
    } else if (hwpc_group.number[I_cache] > 0) {
      is_unit=5;
    } else if (hwpc_group.number[I_cycle] > 0) {
      is_unit=6;
    } else if (hwpc_group.number[I_loadstore] > 0) {
      is_unit=7;
    } else if (m_typeCalc == 0) {
		is_unit=0;
    } else if (m_typeCalc == 1) {
		is_unit=1;
    } else {
    	is_unit=-1;	// this is an error case...
	}

    return is_unit;
  }



  /// Allgather the process level HWPC event values for all processes in MPI_COMM_WORLD
  /// Calibrate some numbers to represent the process value as the sum of thread values
  ///
  ///
  void PerfWatch::gatherHWPC()
  {
#ifdef USE_PAPI
	int is_unit = statsSwitch();
	if ( (is_unit == 0) || (is_unit == 1) ) {
		return;
	}
	if ( my_papi.num_events == 0) return;

	#ifdef DEBUG_PRINT_WATCH
	fprintf(stderr, "debug <gatherHWPC> [%s] starts. my_rank=%d \n", m_label.c_str(), my_rank );
	#endif

	sortPapiCounterList ();

	double perf_rate=0.0;
	if ( m_time > 0.0 ) { perf_rate = 1.0/m_time; }
    // 0: user set bandwidth
    // 1: user set flop counts
    // 2: BANDWIDTH : HWPC measured data access bandwidth
    // 3: FLOPS     : HWPC measured flop counts
    // 4: VECTOR    : HWPC measured vectorization
    // 5: CACHE     : HWPC measured cache hit/miss
    // 6: CYCLE     : HWPC measured cycles, instructions
    // 7: LOADSTORE : HWPC measured load/store instruction type
	m_flop = 0.0;
	m_percentage = 0.0;
	if ( is_unit >= 0 && is_unit <= 1 ) {
		m_flop = m_time * my_papi.v_sorted[my_papi.num_sorted-1] ;
	} else 
	if ( is_unit == 2 ) {
		m_flop = my_papi.v_sorted[my_papi.num_sorted-1] ;		// BYTES
	} else 
	if ( is_unit == 3 ) {
		m_flop = my_papi.v_sorted[my_papi.num_sorted-3] ;		// Total_FP
		// re-calculate Flops and peak % of the process values
		my_papi.v_sorted[my_papi.num_sorted-1] = m_flop*perf_rate / (hwpc_group.corePERF*num_threads) * 100.0;	// peak %
	} else 
	if ( is_unit == 4 ) {
		m_flop = my_papi.v_sorted[my_papi.num_sorted-3] ;		// Total_FP
		m_percentage = my_papi.v_sorted[my_papi.num_sorted-1] ;	// [Vector %]

	} else 
	if ( is_unit == 5 ) {
		m_flop = my_papi.v_sorted[0] + my_papi.v_sorted[1] ;	// load+store
		if (hwpc_group.i_platform == 11 ) {
			m_flop = my_papi.v_sorted[0] + my_papi.v_sorted[1] + my_papi.v_sorted[2] ;
		}
		m_percentage = my_papi.v_sorted[my_papi.num_sorted-1] ;	// [L*$ hit%]

	} else
	if ( is_unit == 6 ) {
		my_papi.v_sorted[0] = my_papi.v_sorted[0] / num_threads;	// average cycles
		m_flop = my_papi.v_sorted[1] ;								// TOT_INS

	} else
	if ( is_unit == 7 ) {
		m_flop = my_papi.v_sorted[0] + my_papi.v_sorted[1] ;	// load+store
		if (hwpc_group.i_platform == 11 ) {
			m_flop = my_papi.v_sorted[0] + my_papi.v_sorted[1] + my_papi.v_sorted[2] ;
		}
		m_percentage = my_papi.v_sorted[my_papi.num_sorted-1] ;	// [Vector %]
	}

	// The space is reserved only once as a fixed size array
	if ( m_sortedArrayHWPC == NULL) {
		m_sortedArrayHWPC = new double[num_process*my_papi.num_sorted];
		if (!(m_sortedArrayHWPC)) {
			printError("gatherHWPC", "new memory failed. %d x %d x 8\n", num_process, my_papi.num_sorted);
			PM_Exit(0);
		}
		#ifdef DEBUG_PRINT_WATCH
		fprintf(stderr, "debug <gatherHWPC> allocated [%s] array at %p,  size=%d Bytes for my_rank=%d \n",
			m_label.c_str(), m_sortedArrayHWPC, 8*num_process*my_papi.num_sorted, my_rank );
		#endif
	} else {
		#ifdef DEBUG_PRINT_WATCH
		fprintf(stderr, "debug <gatherHWPC> [%s] already exists. my_rank=%d \n",
			m_label.c_str(), my_rank );
		#endif
	}

	#ifdef DEBUG_PRINT_WATCH
	if ( num_process > 1 ) {
		fprintf(stderr, "debug <gatherHWPC> [%s] calling barrier \n", m_label.c_str() );
		int iret = MPI_Barrier(MPI_COMM_WORLD);
		if ( iret != 0 ) { printError("gatherHWPC", " MPI_Barrier failed. iret=%d\n", iret); }
	}
	fprintf(stderr, "debug <gatherHWPC> [%s] calling MPI_Allgather \n", m_label.c_str() );
	#endif

	if ( num_process > 1 ) {
		int iret =
		MPI_Allgather (my_papi.v_sorted, my_papi.num_sorted, MPI_DOUBLE,
					m_sortedArrayHWPC, my_papi.num_sorted, MPI_DOUBLE, MPI_COMM_WORLD);
		if ( iret != 0 ) {
			printError("gatherHWPC", " MPI_Allather failed.\n");
			PM_Exit(0);
		}
	} else {

        for (int i = 0; i < my_papi.num_sorted; i++) {
			m_sortedArrayHWPC[i] = my_papi.v_sorted[i];
		}
	}
	#ifdef DEBUG_PRINT_WATCH
	fprintf(stderr, "debug <gatherHWPC> [%s] ends. my_rank=%d \n",
			m_label.c_str(), my_rank );
	#endif

#endif
  }


  /// Allgather the thread level HWPC event values of all processes in MPI_COMM_WORLD
  /// Does not calibrate numbers, so they represent the actual thread values
  ///
  /// This API is called by PerfWatch::printDetailThreads() only.
  ///
  void PerfWatch::gatherThreadHWPC()
  {
#ifdef USE_PAPI
	int is_unit = statsSwitch();
	if ( (is_unit == 0) || (is_unit == 1) ) {
		return;
	}
	if ( my_papi.num_events == 0) return;

	sortPapiCounterList ();

	double perf_rate=0.0;
	if ( m_time > 0.0 ) { perf_rate = 1.0/m_time; }
    // 0: user set bandwidth
    // 1: user set flop counts
    // 2: BANDWIDTH : HWPC measured data access bandwidth
    // 3: FLOPS     : HWPC measured flop counts
    // 4: VECTOR    : HWPC measured vectorization
    // 5: CACHE     : HWPC measured cache hit/miss
    // 6: CYCLE     : HWPC measured cycles, instructions
    // 7: LOADSTORE : HWPC measured load/store instruction type
	m_flop = 0.0;
	m_percentage = 0.0;
	if ( is_unit >= 0 && is_unit <= 1 ) {
		m_flop = m_time * my_papi.v_sorted[my_papi.num_sorted-1] ;
	} else 
	if ( is_unit == 2 ) {
		m_flop = my_papi.v_sorted[my_papi.num_sorted-1] ;		// BYTES
	} else 
	if ( is_unit == 3 ) {
		m_flop = my_papi.v_sorted[my_papi.num_sorted-3] ;		// Total_FP
		my_papi.v_sorted[my_papi.num_sorted-1] = m_flop*perf_rate / hwpc_group.corePERF * 100.0;	// peak %
	} else 
	if ( is_unit == 4 ) {
		m_flop = my_papi.v_sorted[my_papi.num_sorted-3] ;		// Total_FP
		m_percentage = my_papi.v_sorted[my_papi.num_sorted-1] ;	// [Vector %]

	} else 
	if ( is_unit == 5 ) {
		m_flop = my_papi.v_sorted[0] + my_papi.v_sorted[1] ;	// load+store
		if (hwpc_group.i_platform == 11 ) {
			m_flop = my_papi.v_sorted[0] + my_papi.v_sorted[1] + my_papi.v_sorted[2] ;
		}
		m_percentage = my_papi.v_sorted[my_papi.num_sorted-1] ;	// [L*$ hit%]

	} else
	if ( is_unit == 6 ) {
		m_flop = my_papi.v_sorted[1] ;							// TOT_INS

	} else
	if ( is_unit == 7 ) {
		m_flop = my_papi.v_sorted[0] + my_papi.v_sorted[1] ;	// load+store
		if (hwpc_group.i_platform == 11 ) {
			m_flop = my_papi.v_sorted[0] + my_papi.v_sorted[1] + my_papi.v_sorted[2] ;
		}
		m_percentage = my_papi.v_sorted[my_papi.num_sorted-1] ;	// [Vector %]
	}

	// The space is reserved only once as a fixed size array
	if ( m_sortedArrayHWPC == NULL) {
		m_sortedArrayHWPC = new double[num_process*my_papi.num_sorted];
		if (!(m_sortedArrayHWPC)) {
			printError("gatherThreadHWPC", "new memory failed. %d x %d x 8\n", num_process, my_papi.num_sorted);
			PM_Exit(0);
		}
		#ifdef DEBUG_PRINT_WATCH
		fprintf(stderr, "<PerfWatch::gatherThreadHWPC> allocated %d Bytes for [%s] my_rank=%d \n",
			8*num_process*my_papi.num_sorted, m_label.c_str(), my_rank );
		#endif
	}

	if ( num_process > 1 ) {
		int iret =
		MPI_Allgather (my_papi.v_sorted, my_papi.num_sorted, MPI_DOUBLE,
					m_sortedArrayHWPC, my_papi.num_sorted, MPI_DOUBLE, MPI_COMM_WORLD);
		if ( iret != 0 ) {
			printError("gatherThreadHWPC", " MPI_Allather failed. iret=%d\n", iret);
			PM_Exit(0);
		}
	} else {

        for (int i = 0; i < my_papi.num_sorted; i++) {
			m_sortedArrayHWPC[i] = my_papi.v_sorted[i];
		}
	}

#endif
  }



  ///	Allgather the process level basic statistics, i.e. m_time, m_flop, m_count
  ///	
  void PerfWatch::gather()
  {

    int m_np;
    m_np = num_process;

	// The space should be reserved only once as fixed size arrays
	if (( m_timeArray == NULL) && ( m_flopArray == NULL) && ( m_countArray == NULL)) {
		m_timeArray  = new double[m_np];
		m_flopArray  = new double[m_np];
		m_countArray  = new long[m_np];
		if (!(m_timeArray) || !(m_flopArray) || !(m_countArray)) {
			printError("PerfWatch::gather", "new memory failed. %d(process) x 3 x 8 \n", num_process);
			PM_Exit(0);
		}

		#ifdef DEBUG_PRINT_WATCH
		//	if (my_rank == 0) {
		fprintf(stderr, "debug <PerfWatch::gather> allocated [%15s] 3 arrays at %p, %p,  %p \n",
			m_label.c_str(), m_timeArray, m_flopArray, m_countArray );
		//	}
		#endif
	} else {
		#ifdef DEBUG_PRINT_WATCH
		//	if (my_rank == 0) {
		fprintf(stderr, "debug <PerfWatch::gather> [%15s] arrays already exist at %p, %p,  %p \n",
			m_label.c_str(), m_timeArray, m_flopArray, m_countArray );
		//	}
		#endif
	}

    if ( m_np == 1 ) {
      m_timeArray[0] = m_time;
      m_flopArray[0] = m_flop;
      m_countArray[0]= m_count;
      m_count_sum = m_count;
    } else {
      if (MPI_Allgather(&m_time,  1, MPI_DOUBLE, m_timeArray, 1, MPI_DOUBLE, MPI_COMM_WORLD) != MPI_SUCCESS) PM_Exit(0);
      if (MPI_Allgather(&m_flop,  1, MPI_DOUBLE, m_flopArray, 1, MPI_DOUBLE, MPI_COMM_WORLD) != MPI_SUCCESS) PM_Exit(0);
      if (MPI_Allgather(&m_count, 1, MPI_LONG, m_countArray, 1, MPI_LONG, MPI_COMM_WORLD) != MPI_SUCCESS) PM_Exit(0);
      if (MPI_Allreduce(&m_count, &m_count_sum, 1, MPI_LONG, MPI_SUM, MPI_COMM_WORLD) != MPI_SUCCESS) PM_Exit(0);
    }
	// Above arrays will be used by the subsequent routines, and should not be deleted here
	// i.e. m_timeArray, m_flopArray, m_countArray

	#ifdef DEBUG_PRINT_WATCH
	fprintf(stderr, "\t<PerfWatch::gather> [%15s] my_rank=%d, m_countArray[0:*]:", m_label.c_str(), my_rank);
	for (int i=0; i<num_process; i++) { fprintf(stderr, " %ld",  m_countArray[i]); } fprintf(stderr, "\n");
	int iret;
	iret = MPI_Barrier(MPI_COMM_WORLD);
	if ( iret != 0 ) {
		printError("gather", " MPI_Barrier failed. my_rank=%d, iret=%d\n", my_rank, iret);
	} else {
		fprintf(stderr, "\t<PerfWatch::gather> [%15s] my_rank=%d  ends\n", m_label.c_str(), my_rank );
	}
	#endif

  }



  ///  Merging the thread parallel data into the master thread in three steps.
  ///  These three step routines are called by <PerfMonitor::mergeThreads>
  ///  which is called by <PerfMonitor::report> in a serial region.
  ///  After these steps, the master thread will retain the aggregated values
  ///  in its "my_papi" struct.
  ///  Shared struct "papi" is used as a scratch space during these steps.
  ///
  ///  The 1st step : Process the data generated in the serial hybrid region.
  ///  			Copy "my_papi" data of the master thread into shared "papi" space.
  ///
  ///	@note The 1st step master thread copy in should be done by the master thread
  ///
  ///
  void PerfWatch::mergeMasterThread(void)
  {
  #ifdef _OPENMP
	if (m_threads_merged) return;
	if (my_thread != 0) return;
	if (m_started) return; // still in the middle of active start/stop pair
		// The thread stats should be merged after the thread has stopped.

	#ifdef DEBUG_PRINT_WATCH
	if (my_rank == 0) {
		fprintf(stderr, "<mergeMasterThread> [%s] merge step 1. m_in_parallel=%s, &my_papi=%p \n",
					m_label.c_str(), m_in_parallel?"true":"false", &my_papi);
	}
	#endif

    int is_unit = statsSwitch();

	// In the following steps, "papi" shared structureis used as a scratch space.
	// First, copy the master thread local "my_papi" to shared "papi"
	if ( is_unit >= 2) { // PMlib HWPC counter mode
		for (int j=0; j<num_threads; j++) {
			for (int i=0; i<my_papi.num_events; i++) {
				papi.th_accumu[j][i] = my_papi.th_accumu[j][i];
				papi.th_v_sorted[j][i] = my_papi.th_v_sorted[j][i];
			}
		}

	} else {	// PMlib user counter mode
		for (int j=0; j<num_threads; j++) {
			for (int i=0; i<3; i++) {
				papi.th_accumu[j][i] = my_papi.th_accumu[j][i];	// This is not necessary. Just keeping symmetry.
				papi.th_v_sorted[j][i] = my_papi.th_v_sorted[j][i];
			}
		}
	}
	//  Note on the use of my_papi.th_v_sorted[][] array.
	//  PerfWatch::stop() should have saved following variables (both for HWPC mode and USER mode)
	//	my_papi.th_v_sorted[my_thread][0] = (double)m_count;	// call
	//	my_papi.th_v_sorted[my_thread][1] = m_time;				// time[s]
	//	my_papi.th_v_sorted[my_thread][2] = m_flop;				// operations
	//	So the copy volume in the above if block is somewhat overdone

  #endif
  }


  ///  Merging the thread parallel data into the master thread in three steps.
  ///  These three step routines are called by <PerfMonitor::gather>
  ///  which is called by <PerfMonitor::report/print> in a serial region.
  ///  After these steps, the master thread will retain the aggregated values
  ///  in its "my_papi" struct.
  ///  Shared struct "papi" is used as a scratch space during these steps.
  ///
  ///  The 2nd step : Process the data generated from parallel region.
  ///  			Aggregate the class private "my_papi" data into shared "papi" space.
  ///
  ///	@note This 2nd step aggregation must be called by all the threads inside parallel construct
  ///	@note  Only the sections executed inside of parallel construct are merged.
  ///		In Worksharing parallel structure, everything is in place and nothing is done here.
  ///
  void PerfWatch::mergeParallelThread(void)
  {
  #ifdef _OPENMP
	if (m_threads_merged) return;
	if (my_thread == 0) return;
	if (m_started) return; // still in the middle of active start/stop pair
		// The thread stats should be merged after the thread has stopped.

	if ( !(m_in_parallel) ) return;

	int i_thread;
	i_thread = omp_get_thread_num();
	if (i_thread != my_thread) {
		// collection of thread values must be done by each thread instances
		fprintf(stderr, "\n\t*** PMlib internal error <mergeParallelThread> [%s] my_thread:%d does not match OpenMP thread:%d\n ",
				m_label.c_str(), my_thread, i_thread);
	}

    int is_unit = statsSwitch();

	if ( is_unit >= 2) { // PMlib HWPC counter mode
		for (int i=0; i<my_papi.num_events; i++) {
			papi.th_accumu[my_thread][i] = my_papi.th_accumu[my_thread][i];
			papi.th_v_sorted[my_thread][i] = my_papi.th_v_sorted[my_thread][i];
		}

	} else {	// PMlib user counter mode
		for (int i=0; i<3; i++) {
			papi.th_v_sorted[my_thread][i] = my_papi.th_v_sorted[my_thread][i];
		}
	}

	#ifdef DEBUG_PRINT_WATCH
	//	if (my_rank == 0) {
		#pragma omp critical
		{
		fprintf(stderr, "<mergeParallelThread> [%s] merge step 2. my_thread=%d, &my_papi=%p \n",
					m_label.c_str(), my_thread, &my_papi);

		#ifdef DEBUG_PRINT_PAPI_THREADS
		if ( is_unit >= 2) { // PMlib HWPC counter mode
    		fprintf(stderr, "\t [%s] my_thread=%d\n", m_label.c_str(), my_thread);
			for (int i=0; i<my_papi.num_events; i++) {
				fprintf(stderr, "\t\t [%s] : [%8s]  my_papi.th_accumu[%d][%d]=%llu\n",
					m_label.c_str(), my_papi.s_name[i].c_str(), i, my_thread, my_papi.th_accumu[my_thread][i]);
			}
		} else {	// ( is_unit == 0 | is_unit == 1) : PMlib user counter mode
    		fprintf(stderr, "\t [%s] user mode: my_thread=%d, m_flop=%e\n", m_label.c_str(), my_thread, m_flop);
			for (int j=my_thread; j<my_thread+1; j++) {
				fprintf (stderr, "\t\t my_papi.th_v_sorted[%d][0:2]: %e, %e, %e \n",
					j, my_papi.th_v_sorted[j][0], my_papi.th_v_sorted[j][1], my_papi.th_v_sorted[j][2]);
			}
		}
		fprintf (stderr, "\t m_count=%ld, m_time=%e, m_flop=%e\n", m_count, m_time, m_flop);
		#endif
		}
	//	}
	#endif

  #endif
  }



  ///  Merging the thread parallel data into the master thread in three steps.
  ///  These three routines are called by <PerfMonitor::gather> which is
  ///  called by <PerfMonitor::report> in a serial region.
  ///  After these steps, the master thread will retain the aggregated values
  ///  in its "my_papi" struct.
  ///  Shared struct "papi" is used as a scratch space during these steps.
  ///
  ///  The 3rd step : Finally update some of the "isolated" stats.
  ///
  ///	@note The 3rd step should be done by the master thread only
  ///
  ///
  void PerfWatch::updateMergedThread(void)
  {
  #ifdef _OPENMP
	if (m_threads_merged) return;
	if (my_thread != 0) return;
	if (m_started) return; // still in the middle of active start/stop pair
		// The thread stats should be merged after the thread has stopped.

    int is_unit = statsSwitch();

	if ( is_unit >= 2) { // PMlib HWPC counter mode

		for (int j=0; j<num_threads; j++) {
			for (int i=0; i<my_papi.num_events; i++) {
				my_papi.th_accumu[j][i] = papi.th_accumu[j][i] ;
				my_papi.th_v_sorted[j][i] = papi.th_v_sorted[j][i] ;
			}
		}

		//
		// Normal HWPC events are isolated inside the compute core, and their values should be accumulated.
		// The below formula is valid for the most cases. Just accmulate the values.
		//
		for (int i=0; i<my_papi.num_events; i++) {
			my_papi.accumu[i] = 0.0;
			for (int j=0; j<num_threads; j++) {
				my_papi.accumu[i] += my_papi.th_accumu[j][i];
			}
		}

		// Some events such as memory controller are outside compute cores, and their values are shared,
		// i.e. their values should not be accumulated.

		// Detect A64FX BANDWIDTH event whose counter values are counter per CMG, not separated per core
		if ( ( is_unit == 2) && ( hwpc_group.i_platform == 21 ) ) {

			int np_node;			//	the number of processes on this node
			int my_rank_on_node;	// 	the local rank number of this process on this node

			char* cp_env;
			cp_env = std::getenv("PJM_PROC_BY_NODE");
			if (cp_env == NULL) {
				fprintf (stderr, "\n\t *** PMlib warning. BANDWIDTH option for A64FX is only supported on Fugaku.\n");
				fprintf (stderr, "\t\t The environment variable PJM_PROC_BY_NODE is not set. \n");
				fprintf (stderr, "\t\t The report will assume np_node(the number of processes per node) = 1. \n");
				np_node = 1;
			} else {
				np_node = atoi(cp_env);
				if (np_node < 1 || np_node > 48) {
				fprintf (stderr, "\n\t *** PMlib warning. BANDWIDTH option for A64FX is only supported on Fugaku.\n");
					fprintf (stderr, "\t\t The number of processes per node should be 1 <= np_node <= 48,\n");
					fprintf (stderr, "\t\t but the value is set as %d. \n", np_node);
					fprintf (stderr, "\t\t The report will assume np_node=1. \n");
					np_node = 1;
				} else {
					// np_node looks OK
				}
			}
			cp_env = std::getenv("PLE_RANK_ON_NODE");
			if (cp_env == NULL) {
				// unable to obtain the rank number of this process
				fprintf (stderr, "\n\t *** PMlib warning. The environment variable PLE_RANK_ON_NODE is not set. \n");
				fprintf (stderr, "\t\t The report will assume there is only 1 process on this node. \n");
				my_rank_on_node = 0;
			} else {
				my_rank_on_node = atoi(cp_env);
				if (my_rank_on_node < 0 || my_rank_on_node > 47) {
					fprintf (stderr, "\n\t *** PMlib warning. The value of PLE_RANK_ON_NODE should be 0 <= p <= 47.\n");
					fprintf (stderr, "\t\t but the value is set as %d. \n", my_rank_on_node);
					fprintf (stderr, "\t\t The report will assume my_rank_on_node=0. \n");
					my_rank_on_node = 0;
				} else {
					//  my_rank_on_node looks OK
				}
			}
			// by now, two important values are set as 1 <= np_node <= 48 and 0 <= my_rank_on_node <= 47
			// The normal packed thread affinity is assumed. scattered affinity is not currently supported.
			double share_ratio = 0.0;
			if (np_node <= 4) {
				int ncmg_proc = (num_threads-1)/12+1;		//	the number of occupied CMGs by this process
				for (int i=0; i<my_papi.num_events; i++) {
					my_papi.accumu[i] = 0.0;
					for (int k=0; k<ncmg_proc; k++) {
						my_papi.accumu[i] += my_papi.th_accumu[12*k][i];
					}
				}
				if (np_node == 3 && num_threads > 12) {
					share_ratio = 1.0/3.0;
					for (int i=0; i<my_papi.num_events; i++) {
						my_papi.accumu[i] += my_papi.th_accumu[num_threads-1][i] * share_ratio;
					}
				}
				#ifdef DEBUG_PRINT_PAPI_THREADS
    			fprintf(stderr, "<updateMergedThread> A64FX BANDWIDTH case: [%s] np_node=%d, my_rank_on_node=%d \n", m_label.c_str(), np_node, my_rank_on_node);
				#endif

			} else if (np_node >= 5) {
				int np_share = (np_node-1)/4+1;		// max. # of processes sharing CMG
				if ((my_rank_on_node % 4) <= ((np_node-1) % 4)) {
					share_ratio = 1.0/np_share;			// crowded CMG share
				} else {
					share_ratio = 1.0/(np_share-1.0);	// less crowded CMG share
				}

				for (int i=0; i<my_papi.num_events; i++) {
					my_papi.accumu[i] = my_papi.th_accumu[0][i] * share_ratio;
				}
				#ifdef DEBUG_PRINT_PAPI_THREADS
    			fprintf(stderr, "<updateMergedThread> A64FX BANDWIDTH case: [%s] np_node=%d, my_rank_on_node=%d \n", m_label.c_str(), np_node, my_rank_on_node);
    			fprintf(stderr, "\t\t np_share=%d, share_ratio=%f \n", np_share, share_ratio);
				#endif
			}
		}



	} else {	// PMlib user counter mode
		for (int j=0; j<num_threads; j++) {
			for (int i=0; i<3; i++) {
				my_papi.th_v_sorted[j][i] = papi.th_v_sorted[j][i] ;
			}
		}
	}

	m_threads_merged = true;

	double m_count_threads, m_time_threads, m_flop_threads;
	m_count_threads = 0.0;
	m_time_threads  = 0.0;
	m_flop_threads  = 0.0;

// 2021/9/2 Change the collective operations from max to summation
	for (int j=0; j<num_threads; j++) {
		//	m_count_threads = std::max(m_count_threads, my_papi.th_v_sorted[j][0]);	// maximum counts among threads
		//	m_time_threads = std::max(m_time_threads, my_papi.th_v_sorted[j][1]);	// longest time among threads
		//	m_flop_threads += my_papi.th_v_sorted[j][2];		// total values of all threads
		m_count_threads += my_papi.th_v_sorted[j][0];
		m_time_threads += my_papi.th_v_sorted[j][1];
		m_flop_threads += my_papi.th_v_sorted[j][2];
	}
	m_count = lround(m_count_threads);
	m_time = m_time_threads;
	m_flop = m_flop_threads;


	#ifdef DEBUG_PRINT_PAPI_THREADS
    //	if (my_rank == 0) {
		#pragma omp critical
		{
    	fprintf(stderr, "<updateMergedThread> [%s] merge step 3. master thread:\n", m_label.c_str());
		if ( is_unit >= 2) { // PMlib HWPC counter mode
			for (int i=0; i<my_papi.num_events; i++) {
				fprintf(stderr, "\t [%s] : [%8s] my_papi.accumu[%d]=%llu \n",
					m_label.c_str(), my_papi.s_name[i].c_str(), i, my_papi.accumu[i]);
				for (int j=0; j<num_threads; j++) {
					fprintf(stderr, "\t\t my_papi.th_accumu[%d][%d]=%llu\n", j, i, my_papi.th_accumu[j][i]);
				}
			}
		} else {	// ( is_unit == 0 | is_unit == 1) : PMlib user counter mode
    		fprintf(stderr, "\t\t [%s] user mode: my_thread=%d, m_flop=%e\n", m_label.c_str(), my_thread, m_flop);
			for (int j=0; j<num_threads; j++) {
				fprintf (stderr, "\t my_papi.th_v_sorted[%d][0:2]: %e, %e, %e \n",
					j, my_papi.th_v_sorted[j][0], my_papi.th_v_sorted[j][1], my_papi.th_v_sorted[j][2]);
			}
		}
		fprintf (stderr, "\t m_count=%ld, m_time=%e, m_flop=%e\n", m_count, m_time, m_flop);
		}
    //	}
	#endif

// we should clean up "papi" after these steps.

	if ( is_unit >= 2) { // PMlib HWPC counter mode
		for (int j=0; j<num_threads; j++) {
			for (int i=0; i<my_papi.num_events; i++) {
				papi.th_accumu[j][i] = 0;
				papi.th_v_sorted[j][i] = 0.0;
			}
		}
	} else {	// PMlib user counter mode
		for (int j=0; j<num_threads; j++) {
			for (int i=0; i<3; i++) {
				papi.th_accumu[j][i] = 0;
				papi.th_v_sorted[j][i] = 0.0;
			}
		}
	}

  #endif
  }



  /// 測定区間にプロパティを設定.
  ///
  ///   @param[in] label     ラベル
  ///   @param[in] id       ラベルに対応する番号
  ///   @param[in] typeCalc  測定量のタイプ(0:通信, 1:計算)
  ///   @param[in] nPEs            並列プロセス数
  ///   @param[in] my_rank_ID      ランク番号
  ///   @param[in] nTHREADs     並列スレッド数
  ///   @param[in] exclusive 排他測定フラグ
  ///
  void PerfWatch::setProperties(const std::string label, int id, int typeCalc, int nPEs, int my_rank_ID, int nTHREADs, bool exclusive)
  {
    m_label = label;
    m_id = id;
    m_typeCalc = typeCalc;
    m_exclusive =  exclusive;
    num_process = nPEs;
    my_rank = my_rank_ID;
    num_threads = nTHREADs;
	m_in_parallel = false;
	my_thread = 0;
	m_threads_merged = true;
#ifdef _OPENMP
	m_in_parallel = omp_in_parallel();
	my_thread = omp_get_thread_num();
	m_threads_merged = false;
#endif

	if (!m_is_set) {
		my_papi = papi;
#ifdef USE_POWER
		my_power = power;
		level_POWER = power.level_report;
#endif
		m_is_set = true;
	}

	if (m_in_parallel) {
#if defined (__INTEL_COMPILER)	|| \
    defined (__GXX_ABI_VERSION)	|| \
    defined (__CLANG_FUJITSU)	|| \
	defined (__PGI)
	// No problem. Go ahead.
#else
	// Nop. This compiler does not support threadprivate C++ class.
		printError("setProperties", "Calling [%s] from inside of parallel region is not supported by the C++ compiler which built PMlib.\n", label.c_str());
		//	m_is_set = false;
#endif
	}


    level_OTF = 0;
#ifdef USE_OTF
	// 環境変数OTF_TRACING が指定された場合
	// OTF_TRACING = none(default) | yes | on | full
    std::string s;
    char* cp_env;
    cp_env = std::getenv("OTF_TRACING");
    if (cp_env != NULL) {
      s = cp_env;
      std::transform(s.begin(), s.end(), s.begin(), toupper); // C func toupper()
      if ((s == "OFF") || (s == "NO") ) {
        level_OTF = 0;
      } else if ((s == "ON") || (s == "YES") ) {
        level_OTF = 1;
      } else if ((s == "FULL")) {
        level_OTF = 2;
      }
      #ifdef DEBUG_PRINT_OTF
      if (my_rank == 0) {
	    fprintf(stderr, "\t<getenv> OTF_TRACING=%s is provided.\n", cp_env);
      }
      #endif
    }
#endif

#ifdef DEBUG_PRINT_WATCH
    //	if (my_rank == 0) {
		// id is numbered per thread, i.e. each thread may have different value for this section id.
    	fprintf(stderr, "<PerfWatch::setProperties> [%s] thread:%d, id=%d, m_in_parallel=%s \n",
			label.c_str(), my_thread, id, m_in_parallel?"true":"false" );

		#ifdef DEBUG_PRINT_PAPI
		#pragma omp critical
		{
    	fprintf(stderr, "\t[%s] my_rank=%d, my_thread:%d, num_threads=%d, address check: &num_threads=%p, &papi=%p, &my_papi=%p\n",
			label.c_str(), my_rank, my_thread, num_threads,   &num_threads, &papi, &my_papi);
		#ifdef DEBUG_PRINT_PAPI_THREADS
		for (int j=0; j<num_threads; j++) {
			fprintf (stderr, "\tmy_papi.th_accumu[%d][*]:", j);
			for (int i=0; i<my_papi.num_events; i++) {
				fprintf (stderr, "%llu, ", my_papi.th_accumu[j][i]);
			};	fprintf (stderr, "\n");
		}
		#endif
    	}
		#endif

		#ifdef USE_POWER
		#pragma omp critical
		{
    	fprintf(stderr, "\t\t [%s] address check my_power thread:%d, &my_power=%p \n",
			label.c_str(), my_thread, &my_power);
    	}
		#endif
		// end of #pragma omp critical
	//	}
#endif

  }



  /// set the Power API reporting level for the Root section
  ///
  ///	@param[in] n  number of Power objects initialized by PerfMonitor class instance
  ///	@param[in] n  level of detail for power report [0..3]
  ///
  ///	@note num_power is always 20 for Fugaku implementation
  ///
  void PerfWatch::setRootPowerLevel(int num, int level)
  {
#ifdef USE_POWER
	power.num_power_stats = 0;
	power.level_report = level;
    if(power.level_report >  0) {
		power.num_power_stats = num;
	}
	#ifdef DEBUG_PRINT_WATCH
    //	if (my_rank == 0) {
    	fprintf(stderr, "<PerfWatch::setRootPowerLevel> [%s] level_report=%d num_power_stats=%d \n",
		m_label.c_str(), power.level_report, power.num_power_stats);
	//	}
	#endif
#endif
  }


  /// gather the estimated power consumption of all processes
  ///
  void PerfWatch::gatherPOWER(void)
  {
#ifdef USE_POWER
    if (level_POWER == 0) return;
	#ifdef DEBUG_PRINT_POWER_EXT
	(void) MPI_Barrier(MPI_COMM_WORLD);
   	fprintf(stderr, "<PerfWatch::gatherPOWER> [%s] my_rank:%d, thread:%d, w_accumu[0]=%e \n",
		m_label.c_str(), my_rank, my_thread, my_power.w_accumu[0]);
	#endif

	double t_joule;
	int iret;
	//	Sum up (MPI_Reduce) the estimated total power consumption my_power.w_accumu[0] into t_joule
	if ( num_process > 1 ) {
		iret = MPI_Reduce (&my_power.w_accumu[0], &t_joule, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
		if ( iret != 0 ) {
			fprintf(stderr, "*** error. <%s> MPI_Reduce failed. iret=%d\n", __func__, iret);
			t_joule = 0.0;
		}
	} else {
		t_joule = my_power.w_accumu[0];
	}
	m_power_av = t_joule/num_process;
	
#endif
  }



  /// ポスト処理用traceファイル出力用の初期化
  ///
  void PerfWatch::initializeOTF(void)
  {
#ifdef USE_OTF
    if (level_OTF == 0) return;

	// 環境変数 OTF_FILENAME が指定された場合
    std::string s;
    char* cp_env = std::getenv("OTF_FILENAME");
    if (cp_env != NULL) {
      s = cp_env;
      otf_filename = s;
    } else {
      otf_filename = "pmlib_otf_files";
    }
    double baseT = PerfWatch::getTime();
    my_otf_initialize(num_process, my_rank, otf_filename.c_str(), baseT);
#endif
  }


  /// 測定区間のラベル情報をOTF に出力
  ///
  ///   @param[in] label     ラベル
  ///   @param[in] id       ラベルに対応する番号
  ///
  void PerfWatch::labelOTF(const std::string& label, int id)
  {
#ifdef USE_OTF
    if (level_OTF == 0) return;

	int i_switch = statsSwitch();
    my_otf_event_label(num_process, my_rank, id+1, label.c_str(), m_exclusive, i_switch);

    if (id != 0) {
      level_OTF = 0;
    }
	#ifdef DEBUG_PRINT_OTF
    if (my_rank == 0) {
		fprintf(stderr, "\t<labelOTF> label=%s, m_exclusive=%d, i_switch=%d\n",
			label.c_str(), m_exclusive, i_switch);
    }
	#endif
#endif
  }


  /// OTF 出力処理を終了する
  ///
  void PerfWatch::finalizeOTF(void)
  {
#ifdef USE_OTF
    if (level_OTF == 0) return;

    std::string s_group, s_counter, s_unit;

	s_group = "PMlib-OTF counter group" ;

    int is_unit = statsSwitch();
	#ifdef DEBUG_PRINT_OTF
    if (my_rank == 0) {
	fprintf(stderr, "\t<finalizeOTF> is_unit=%d \n", is_unit);
	fprintf(stderr, "\tmy_papi.num_sorted-1=%d \n", my_papi.num_sorted-1);
    }
	#endif
	if ( is_unit == 0 || is_unit == 1 ) {
		s_counter =  "User Defined COMM/CALC values" ;
		s_unit =  "unit: B/sec or Flops";
	} else if ( 2 <= is_unit && is_unit <= Max_hwpc_output_group ) {
		s_counter =  "HWPC measured values" ;
		s_unit =  my_papi.s_sorted[my_papi.num_sorted-1] ;
	}

	(void) MPI_Barrier(MPI_COMM_WORLD);
	my_otf_finalize (num_process, my_rank, is_unit,
		otf_filename.c_str(), s_group.c_str(),
		s_counter.c_str(), s_unit.c_str());

    level_OTF = 0;

	#ifdef DEBUG_PRINT_OTF
    if (my_rank == 0) {
	fprintf(stderr, "\t<finalizeOTF> otf_filename=%s, is_unit=%d, s_unit=%s \n",
		otf_filename.c_str(), is_unit, s_unit.c_str());
    }
	#endif
#endif
  }


  /// start measuring the section
  ///
  void PerfWatch::start()
  {
#ifdef DEBUG_PRINT_WATCH
	fprintf (stderr, "<PerfWatch::start> [%s] my_thread=%d\n", m_label.c_str(), my_thread);
#endif

    if (!m_is_healthy) {
		fprintf (stderr, "\n\t *** PMlib warning <PerfWatch::start> [%s] rank=%d my_thread=%d is marked un_healthy. \n", m_label.c_str(), my_rank, my_thread);
		//	return;
	}

    if (m_started) {
		fprintf (stderr, "\n\t *** PMlib warning <PerfWatch::start> [%s] rank=%d my_thread=%d is already marked started. Duplicated start is ignored. \n", m_label.c_str(), my_rank, my_thread);
		//	return;
    }
	if (!m_is_set) {
		fprintf (stderr, "\n\t *** PMlib internal error. [%s] rank=%d my_thread=%d is marked m_is_set=FALSE. \n",
			m_label.c_str(), my_rank, my_thread);
		//	m_is_healthy=false;
		//	return;
	}
    m_started = true;
    m_startTime = getTime();
	m_threads_merged = false;

	if ( m_in_parallel ) {
		// The threads are active and running in parallel region
		startSectionParallel();
	} else {
		// The thread is running in serial region
		startSectionSerial();
	}

#ifdef USE_OTF
    if (level_OTF != 0) {
      int is_unit = statsSwitch();
      my_otf_event_start(my_rank, m_startTime, m_id, is_unit);
	}
#endif
  }

  /// start measuring the power of the section
  ///
  ///   @param[in] PWR_Cntxt pacntxt
  ///   @param[in] PWR_Cntxt extcntxt
  ///   @param[in] PWR_Obj obj_array
  ///   @param[in] PWR_Obj obj_ext
  ///
  ///	@note the arguments are Power API objects and attributes
  ///
  void PerfWatch::power_start(PWR_Cntxt pacntxt, PWR_Cntxt extcntxt, PWR_Obj obj_array[], PWR_Obj obj_ext[])
  {
#ifdef USE_POWER
	if (level_POWER == 0) return;

	if (my_power.num_power_stats != 0) {
		(void) my_power_bind_start (pacntxt, extcntxt, obj_array, obj_ext,
					my_power.pa64timer, my_power.u_joule);
	}

	#ifdef DEBUG_PRINT_POWER_EXT
    if (my_rank == 0)
	{
		fprintf (stderr, "<PerfWatch::power_start> [%s] my_thread=%d\n",
			m_label.c_str(), my_thread);
		for (int i=0; i<my_power.num_power_stats; i++) {
		//	for (int i=0; i<10; i++) {
			fprintf (stderr, "\t %10.2e\n", my_power.u_joule[i]);
		}
	}
	#endif
#endif
  }



///	Save the data for start/stop pair which is called from serial region
///
  void PerfWatch::startSectionSerial()
  {
	//	startSectionSerial() :	Only the master thread is active and is running in serial region
#ifdef DEBUG_PRINT_WATCH
    if (my_rank == 0) fprintf (stderr, "\t <startSectionSerial> [%s]\n", m_label.c_str());
#endif

    int is_unit = statsSwitch();
	if ( is_unit >= 2) {
#ifdef USE_PAPI
	#pragma omp parallel
	{
		//	parallel regionの全スレッドの処理
		int i_thread = omp_get_thread_num();
		struct pmlib_papi_chooser th_papi = my_papi;
		int i_ret;

		//	We call my_papi_bind_read() to preserve HWPC events for inclusive sections,
		//	in stead of calling my_papi_bind_start() which clears out the event counters.
		i_ret = my_papi_bind_read (th_papi.values, th_papi.num_events);
		if ( i_ret != PAPI_OK ) {
			fprintf(stderr, "*** error. <my_papi_bind_read> code: %d, thread:%d\n", i_ret, i_thread);
			//	PM_Exit(0);
		}

		#pragma ivdep
		for (int i=0; i<my_papi.num_events; i++) {
			my_papi.th_values[i_thread][i] = th_papi.values[i];
		}
	}	// end of #pragma omp parallel region

	#ifdef DEBUG_PRINT_PAPI_THREADS
		if (my_rank == 0) {
			for (int j=0; j<num_threads; j++) {
				fprintf (stderr, "\t<startSectionSerial> [%s] my_papi.th_values[%d][*]:", m_label.c_str(), j);
				for (int i=0; i<my_papi.num_events; i++) {
					fprintf (stderr, "%llu, ", my_papi.th_values[j][i]);
				};	fprintf (stderr, "\n");
			}
		}
	#endif
#endif // USE_PAPI
	} else {
		;
	}
  }



///	Save the data for start/stop pair which is called from inside of parallel region
///	All threads are active and running in parallel region
///
  void PerfWatch::startSectionParallel()
  {

	#ifdef DEBUG_PRINT_WATCH
	if (my_rank == 0) fprintf (stderr, "\t<startSectionParallel> [%s] my_thread=%d\n", m_label.c_str(), my_thread);
	#endif

    int is_unit = statsSwitch();
	if ( is_unit >= 2) {
#ifdef USE_PAPI
	struct pmlib_papi_chooser th_papi = my_papi;
	int i_ret;

	//	we call my_papi_bind_read() to preserve HWPC events for inclusive sections in stead of
	//	calling my_papi_bind_start() which clears out the event counters.
	i_ret = my_papi_bind_read (th_papi.values, th_papi.num_events);
	if ( i_ret != PAPI_OK ) {
		fprintf(stderr, "*** error. <my_papi_bind_read> code: %d, my_thread:%d\n", i_ret, my_thread);
		//	PM_Exit(0);
	}

	//	parallel regionの内側で呼ばれた場合は、my_threadはスレッドIDの値を持つ
	#pragma ivdep
	for (int i=0; i<my_papi.num_events; i++) {
		my_papi.th_values[my_thread][i] = th_papi.values[i];
	}
	#ifdef DEBUG_PRINT_PAPI_THREADS
	//	#pragma omp critical
	//	if (my_rank == 0) {
    //		fprintf (stderr, "\t\t my_thread=%d th_values[*]: ", my_thread);
	//		for (int i=0; i<my_papi.num_events; i++) {
	//			fprintf (stderr, "%llu, ", my_papi.th_values[my_thread][i] );
	//		};	fprintf (stderr, "\n");
	//	}
	#endif
#endif // USE_PAPI
	} else {
		;
	}

  }


  /// 測定区間ストップ.
  ///
  ///   @param[in] flopPerTask     測定区間の計算量(演算量Flopまたは通信量Byte)
  ///   @param[in] iterationCount  計算量の乗数（反復回数）
  ///
  ///   @note  引数はユーザ指定モードの場合にのみ利用され、計算量を
  ///          １区間１回あたりでflopPerTask*iterationCount として算出する。\n
  ///          HWPCによる自動算出モードでは引数は無視され、
  ///          内部で自動計測するHWPC統計情報から計算量を決定決定する。\n
  ///          レポート出力する情報の選択方法はPerfMonitor::stop()の規則による。\n
  ///
  void PerfWatch::stop(double flopPerTask, unsigned iterationCount)
  {
    if (!m_is_healthy) {
      printError("stop()",  "[%s] is marked Not healthy. Corrected. \n", m_label.c_str());
		m_is_healthy=true;
		//	return;
	}

    if (!m_started) {
      printError("stop()",  "[%s]  has not been started. Corrected. \n", m_label.c_str());
      m_started=true;
      //	m_is_healthy=false;
      //	return;
    }

    m_stopTime = getTime();
    m_time += m_stopTime - m_startTime;
    m_count++;
    m_started = false;

	if ( m_in_parallel ) {
		// The threads are active and running in parallel region
		stopSectionParallel(flopPerTask, iterationCount);
	} else {
		// The thread is running in serial region
		stopSectionSerial(flopPerTask, iterationCount);
	}
		// Move the following lines to the end, since sortPapiCounterList() overwrites them.
		//	my_papi.th_v_sorted[my_thread][0] = (double)m_count;
		//	my_papi.th_v_sorted[my_thread][1] = m_time;
		//	my_papi.th_v_sorted[my_thread][2] = m_flop;

	#ifdef DEBUG_PRINT_WATCH
	fprintf (stderr, "<PerfWatch::stop> [%s] my_thread=%d, fPT=%e, itC=%u, m_count=%ld, m_time=%f, m_flop=%e\n",
			m_label.c_str(), my_thread, flopPerTask, iterationCount, m_count, m_time, m_flop);
	fprintf (stderr, "\t\t m_startTime=%f, m_stopTime=%f\n", m_startTime, m_stopTime);
	#endif
#ifdef USE_OTF
    int is_unit = statsSwitch();
	double w=0.0;
	if (level_OTF == 0) {
		// OTFファイル出力なし
		;
	} else if (level_OTF == 1) {
		// OTFファイルには時間情報だけを出力し、カウンター値は0.0とする
		w = 0.0;
		my_otf_event_stop(my_rank, m_stopTime, m_id, is_unit, w);

	} else if (level_OTF == 2) {
		if ( (is_unit == 0) || (is_unit == 1) ) {
			// ユーザが引数で指定した計算量/time(計算speed)
    		w = (flopPerTask * (double)iterationCount) / (m_stopTime-m_startTime);
		} else if ( (2 <= is_unit) && (is_unit <= Max_hwpc_output_group) ) {
			// 自動計測されたHWPCイベントを分析した計算speed
			sortPapiCounterList ();

			// is_unitが2,3の時、v_sorted[]配列の最後の要素は速度の次元を持つ
			// is_unitが4,5の時は...
			w = my_papi.v_sorted[my_papi.num_sorted-1] ;
		}
		my_otf_event_stop(my_rank, m_stopTime, m_id, is_unit, w);
	}
	#ifdef DEBUG_PRINT_OTF
    if (my_rank == 0) {
		fprintf (stderr, "\t <PerfWatch::stop> OTF [%s] w=%e, m_time=%f, m_flop=%e \n"
				, m_label.c_str(), w, m_time, m_flop );
    }
	#endif
#endif	// end of #ifdef USE_OTF

	// Remark: *.th_v_sorted[][] may have been overwritten by sortPapiCounterList() if level_OTF == 2.
	// So save these values here.
	my_papi.th_v_sorted[my_thread][0] = (double)m_count;
	my_papi.th_v_sorted[my_thread][1] = m_time;
	my_papi.th_v_sorted[my_thread][2] = m_flop;
  }


  /// stop measuring the power of the section
  ///
  ///   @param[in] PWR_Cntxt pacntxt
  ///   @param[in] PWR_Cntxt extcntxt
  ///   @param[in] PWR_Obj obj_array
  ///   @param[in] PWR_Obj obj_ext
  ///
  ///	@note the arguments are Power API objects and attributes
  ///
  void PerfWatch::power_stop(PWR_Cntxt pacntxt, PWR_Cntxt extcntxt, PWR_Obj obj_array[], PWR_Obj obj_ext[])
  {
#ifdef USE_POWER
	if (level_POWER == 0) return;

	double t, uvJ, watt;
	if (my_power.num_power_stats != 0) {
		(void) my_power_bind_stop (pacntxt, extcntxt, obj_array, obj_ext,
					my_power.pa64timer, my_power.v_joule);

		t = m_stopTime - m_startTime;

		// output in Joule : 1 Joule == 1 Newton x meter == 1 Watt x second
		for (int i=0; i<my_power.num_power_stats; i++) {
			uvJ = my_power.v_joule[i] - my_power.u_joule[i];
			my_power.w_accumu[i] += uvJ;
			watt = uvJ / t;
			my_power.watt_max[i] = std::max (my_power.watt_max[i], watt);
		}
		#ifdef DEBUG_PRINT_POWER_EXT
    	if (my_rank == 0) {
		double u, v;
		fprintf (stderr, "<PerfWatch::power_stop> [%s] my_thread=%d, t=%e\n\t\t\t u, v, uvJ, watt\n",
			m_label.c_str(), my_thread, t);
		for (int i=0; i<my_power.num_power_stats; i++) {
			u = my_power.u_joule[i];
			v = my_power.v_joule[i];
			uvJ = v - u;
			watt = uvJ / t;
			fprintf (stderr, "\t\t %10.2e, %10.2e, %10.2e, %10.2e\n", u, v, uvJ, watt);
		}
		}
		#endif
	}
#endif
  }



///	Accumulate the data for start/stop pair, when they are called from serial region
///
  void PerfWatch::stopSectionSerial(double flopPerTask, unsigned iterationCount)
  {
	//	Only the master thread is active and is running in serial region

    int is_unit = statsSwitch();
	if ( is_unit >= 2) {
#ifdef USE_PAPI
	if (my_papi.num_events > 0) {
	#pragma omp parallel 
	{
		int i_thread = omp_get_thread_num();
		struct pmlib_papi_chooser th_papi = my_papi;
		int i_ret;

		i_ret = my_papi_bind_read (th_papi.values, th_papi.num_events);
		if ( i_ret != PAPI_OK ) {
			printError("stop",  "<my_papi_bind_read> code: %d, i_thread:%d\n", i_ret, i_thread);
		}

		#pragma ivdep
		for (int i=0; i<my_papi.num_events; i++) {
			my_papi.th_accumu[i_thread][i] += (th_papi.values[i] - my_papi.th_values[i_thread][i]);
		}
	}	// end of #pragma omp parallel region

		#ifdef DEBUG_PRINT_PAPI_THREADS
		if (my_rank == 0) {
			/**
			for (int j=0; j<num_threads; j++) {
				fprintf (stderr, "\t<stopSectionSerial> [%s] my_papi.th_accumu[%d][*]:", m_label.c_str(), j);
				for (int i=0; i<my_papi.num_events; i++) {
					fprintf (stderr, "%llu, ", my_papi.th_accumu[j][i]);
				};	fprintf (stderr, "\n");
			}
			**/
			fprintf (stderr, "<stopSectionSerial> [%s] \n", m_label.c_str());
			for (int j=0; j<num_threads; j++) {
				fprintf (stderr, "\tmy_papi.th_values[%d][*]:", j);
				for (int i=0; i<my_papi.num_events; i++) {
					fprintf (stderr, "%llu, ", my_papi.th_values[j][i]);
				};	fprintf (stderr, "\n");
				fprintf (stderr, "\tmy_papi.th_accumu[%d][*]:", j);
				for (int i=0; i<my_papi.num_events; i++) {
					fprintf (stderr, "%llu, ", my_papi.th_accumu[j][i]);
				};	fprintf (stderr, "\n");
			}
		}
		#endif
	}	// end of if (my_papi.num_events > 0) block
#endif	// end of #ifdef USE_PAPI
	} else
	if ( (is_unit == 0) || (is_unit == 1) ) {
		// ユーザが引数で指定した計算量
		m_flop += flopPerTask * (double)iterationCount;
		#ifdef DEBUG_PRINT_WATCH
    	if (my_rank == 0) fprintf (stderr, "\t<stopSectionSerial> User mode m_flop=%e\n", m_flop);
		#endif
	}
  }


///	Accumulate the data for start/stop pair when they are called from inside the parallel region
///	All threads are active and running inside parallel region
///
  void PerfWatch::stopSectionParallel(double flopPerTask, unsigned iterationCount)
  {

    int is_unit = statsSwitch();
	if ( is_unit >= 2) {
#ifdef USE_PAPI
	if (my_papi.num_events > 0) {
	struct pmlib_papi_chooser th_papi = my_papi;
	int i_ret;

	i_ret = my_papi_bind_read (th_papi.values, th_papi.num_events);
	if ( i_ret != PAPI_OK ) {
		printError("stop",  "<my_papi_bind_read> code: %d, my_thread:%d\n", i_ret, my_thread);
	}

	#pragma ivdep
	for (int i=0; i<my_papi.num_events; i++) {
		my_papi.th_accumu[my_thread][i] += (th_papi.values[i] - my_papi.th_values[my_thread][i]);
	}

	#ifdef DEBUG_PRINT_PAPI_THREADS
	#pragma omp critical
	if (my_rank == 0) {
		fprintf (stderr, "\t<stopSectionParallel> [%s] my_thread=%d, my_papi.th_accumu[%d][*]:",
			m_label.c_str(), my_thread, my_thread);
			for (int i=0; i<my_papi.num_events; i++) {
				fprintf (stderr, "%llu, ", my_papi.th_accumu[my_thread][i]);
			};	fprintf (stderr, "\n");
	}
	#endif
	}	// end of if (my_papi.num_events > 0) {
#endif	// end of #ifdef USE_PAPI
	} else
	if ( (is_unit == 0) || (is_unit == 1) ) {
		// ユーザが引数で指定した計算量
		m_flop += flopPerTask * (double)iterationCount;
		#ifdef DEBUG_PRINT_WATCH
    	if (my_rank == 0) fprintf (stderr, "\t<stopSectionParallel> User mode: my_thread=%d, m_flop=%e\n", my_thread, m_flop);
		#endif
	}
  }



/// reset the measuring section's HWPC counter values
///
  void PerfWatch::reset()
  {
    //	m_started = true;
    //	m_startTime = getTime();

    m_time = 0.0;
    m_count = 0;
	m_flop = 0.0;

#ifdef USE_PAPI
	if (my_papi.num_events > 0) {
		for (int i=0; i<my_papi.num_events; i++) {
			my_papi.accumu[i] = 0.0;
			my_papi.v_sorted[i] = 0.0;
		}
	}
	#ifdef _OPENMP
			#pragma omp barrier
			#pragma omp master
			for (int j=0; j<num_threads; j++) {
			for (int i=0; i<my_papi.num_events; i++) {
				my_papi.th_accumu[j][i] = 0.0 ;
				my_papi.th_v_sorted[j][i] = 0.0 ;
			}
			}
	#endif
#endif

  }



  /// MPIランク別測定結果を出力.
  ///
  ///   @param[in] fp 出力ファイルポインタ
  ///   @param[in] totalTime 全排他測定区間での計算時間(平均値)の合計
  ///
  ///   @note ノード0からのみ呼び出し可能。ノード毎に非排他測定区間も出力

  void PerfWatch::printDetailRanks(FILE* fp, double totalTime)
  {

    int m_np;
    m_np = num_process;

    double t_per_call, perf_rate;
    double tMax = 0.0;
    for (int i = 0; i < m_np; i++) {
      tMax = (m_timeArray[i] > tMax) ? m_timeArray[i] : tMax;
    }

    std::string unit;
    int is_unit = statsSwitch();
    if (is_unit == 0) unit = "B/sec";	// 0: user set bandwidth
    if (is_unit == 1) unit = "Flops";	// 1: user set flop counts
	//
    if (is_unit == 2) unit = "";		// 2: HWPC measured bandwidth
    if (is_unit == 3) unit = "";		// 3: HWPC measured flop counts
    if (is_unit == 4) unit = "";		// 4: HWPC measured vector %
    if (is_unit == 5) unit = "";		// 5: HWPC measured cache hit%
    if (is_unit == 6) unit = "";		// 6: HWPC measured instructions
    if (is_unit == 7) unit = "";		// 7: HWPC measured memory load/store (demand access, prefetch, writeback, streaming store)

    long total_count = 0;
    for (int i = 0; i < m_np; i++) total_count += m_countArray[i];

    if ( total_count > 0 && is_unit <= 1) {
      //	fprintf(fp, "Section Label : %s%s\n", m_label.c_str(), m_exclusive ? "" : "(*)" );
      fprintf(fp, "Section : %s%s%s\n",     m_label.c_str(), m_exclusive? "":" (*)" , m_in_parallel? " (+)":"" );
      fprintf(fp, "MPI rankID :     call   time[s] time[%%]  t_wait[s]  t[s]/call   counter     speed              \n");
      for (int i = 0; i < m_np; i++) {
		t_per_call = (m_countArray[i]==0) ? 0.0: m_timeArray[i]/m_countArray[i];
		perf_rate = (m_countArray[i]==0) ? 0.0 : m_flopArray[i]/m_timeArray[i];
		fprintf(fp, "Rank %5d : %8ld  %9.3e  %5.1f  %9.3e  %9.3e  %9.3e  %9.3e %s\n",
			i,
			m_countArray[i], // コール回数
			m_timeArray[i],  // ノードあたりの時間
			100*m_timeArray[i]/totalTime, // 非排他測定区間に対する割合
			tMax-m_timeArray[i], // ノード間の最大値を基準にした待ち時間
			t_per_call,      // 1回あたりの時間コスト
			m_flopArray[i],  // ノードあたりの演算数
			perf_rate,       // スピード　Bytes/sec or Flops
			unit.c_str()     // スピードの単位
			);
      }
    } else if ( total_count > 0 && is_unit >= 2) {
      //	fprintf(fp, "Section Label : %s%s\n", m_label.c_str(), m_exclusive ? "" : "(*)" );
      fprintf(fp, "Section : %s%s%s\n",     m_label.c_str(), m_exclusive? "":" (*)" , m_in_parallel? " (+)":"" );
      fprintf(fp, "MPI rankID :     call   time[s] time[%%]  t_wait[s]  t[s]/call   \n");
      for (int i = 0; i < m_np; i++) {
		t_per_call = (m_countArray[i]==0) ? 0.0: m_timeArray[i]/m_countArray[i];
		fprintf(fp, "Rank %5d : %8ld  %9.3e  %5.1f  %9.3e  %9.3e  \n",
			i,
			m_countArray[i], // コール回数
			m_timeArray[i],  // ノードあたりの時間
			100*m_timeArray[i]/totalTime, // 非排他測定区間に対する割合
			tMax-m_timeArray[i], // ノード間の最大値を基準にした待ち時間
			t_per_call      // 1回あたりの時間コスト
			);
      }
    }
  }


  ///   Groupに含まれるMPIランク別測定結果を出力.
  ///
  ///   @param[in] fp 出力ファイルポインタ
  ///   @param[in] totalTime 全排他測定区間での計算時間(平均値)の合計
  ///   @param[in] p_group プロセスグループ番号。0の時は全プロセスを対象とする。
  ///   @param[in] pp_ranks int*型 groupを構成するrank番号配列へのポインタ
  ///
  ///   @note ノード0からのみ呼び出し可能
  ///
  void PerfWatch::printGroupRanks(FILE* fp, double totalTime, MPI_Group p_group, int* pp_ranks)
  {
    int ip;
    int m_np;
    int new_id;
    if (p_group == 0) { // p_group should have some positive value
      fprintf(stderr, "*** error PerfWatch::printGroupRanks p_group is 0\n");
    }

    MPI_Group_size(p_group, &m_np);
    MPI_Group_rank(p_group, &new_id);
#ifdef DEBUG_PRINT_WATCH
    if (my_rank == 0) {
      fprintf(fp, "<printGroupRanks> pp_ranks[] has %d ranks:", m_np);
      for (int i = 0; i < m_np; i++) {
		fprintf(fp, "%3d ", pp_ranks[i]);
      }
      fprintf(fp, "\n");
    }
#endif

    double t_per_call, perf_rate;
    double tMax = 0.0;
    for (int i = 0; i < m_np; i++) {
      tMax = (m_timeArray[pp_ranks[i]] > tMax) ? m_timeArray[pp_ranks[i]] : tMax;
    }

    std::string unit;
    int is_unit = statsSwitch();
    if (is_unit == 0) unit = "B/sec";	// 0: user set bandwidth
    if (is_unit == 1) unit = "Flops";	// 1: user set flop counts
	//
    if (is_unit == 2) unit = "";		// 2: HWPC measured bandwidth
    if (is_unit == 3) unit = "";		// 3: HWPC measured flop counts
    if (is_unit == 4) unit = "";		// 4: HWPC measured vector %
    if (is_unit == 5) unit = "";		// 5: HWPC measured cache hit%
    if (is_unit == 6) unit = "";		// 6: HWPC measured instructions
    if (is_unit == 7) unit = "";		// 7: HWPC measured memory load/store (demand access, prefetch, writeback, streaming store)

    long total_count = 0;
    for (int i = 0; i < m_np; i++) total_count += m_countArray[pp_ranks[i]];

    if ( total_count > 0 && is_unit <= 1) {
      //	fprintf(fp, "Label  %s%s\n", m_exclusive ? "" : "*", m_label.c_str());
      //	fprintf(fp, "Header ID  :     call   time[s] time[%%]  t_wait[s]  t[s]/call   operations  performance\n");
      fprintf(fp, "Section Label : %s%s\n", m_label.c_str(), m_exclusive ? "" : "(*)" );
      fprintf(fp, "MPI rankID :     call   time[s] time[%%]  t_wait[s]  t[s]/call   operations  performance\n");
      for (int i = 0; i < m_np; i++) {
	ip = pp_ranks[i];
	t_per_call = (m_countArray[ip]==0) ? 0.0: m_timeArray[ip]/m_countArray[ip];
	perf_rate = (m_countArray[ip]==0) ? 0.0 : m_flopArray[ip]/m_timeArray[ip];
	fprintf(fp, "Rank %5d : %8ld  %9.3e  %5.1f  %9.3e  %9.3e  %9.3e  %9.3e %s\n",
			ip,
			m_countArray[ip], // コール回数
			m_timeArray[ip],  // ノードあたりの時間
			100*m_timeArray[ip]/totalTime, // 非排他測定区間に対する割合
			tMax-m_timeArray[ip], // ノード間の最大値を基準にした待ち時間
			t_per_call,      // 1回あたりの時間コスト
			m_flopArray[ip],  // ノードあたりの演算数
			perf_rate,       // スピード　Bytes/sec or Flops
			unit.c_str()     // スピードの単位
			);
      }
    } else if ( total_count > 0 && is_unit >= 2) {
      //	fprintf(fp, "Label  %s%s\n", m_exclusive ? "" : "*", m_label.c_str());
      //	fprintf(fp, "Header ID  :     call   time[s] time[%%]  t_wait[s]  t[s]/call   \n");
      fprintf(fp, "Section Label : %s%s\n", m_label.c_str(), m_exclusive ? "" : "(*)" );
      fprintf(fp, "MPI rankID :     call   time[s] time[%%]  t_wait[s]  t[s]/call   \n");
      for (int i = 0; i < m_np; i++) {
	ip = pp_ranks[i];
	t_per_call = (m_countArray[ip]==0) ? 0.0: m_timeArray[ip]/m_countArray[ip];
	fprintf(fp, "Rank %5d : %8ld  %9.3e  %5.1f  %9.3e  %9.3e  \n",
			ip,
			m_countArray[ip], // コール回数
			m_timeArray[ip],  // ノードあたりの時間
			100*m_timeArray[ip]/totalTime, // 非排他測定区間に対する割合
			tMax-m_timeArray[ip], // ノード間の最大値を基準にした待ち時間
			t_per_call      // 1回あたりの時間コスト
			);
      }
    }
  }



  /// header line for the averaged HWPC statistics in the Basic report
  ///
  ///   @param[in] fp         report file pointer
  ///   @param[in] maxLabelLen    maximum label field string length
  ///
  void PerfWatch::printBasicHWPCHeader(FILE* fp, int maxLabelLen)
  {
#ifdef USE_PAPI
    if (my_papi.num_events == 0) return;

    std::string s;
    int kp;
	char* cp_env;

	fprintf(fp, "\n");
	fprintf(fp, "\n# PMlib hardware performance counter (HWPC) report of the averaged process ------- #\n");
	fprintf(fp, "\n");

	fprintf(fp, "\tReport for option HWPC_CHOOSER=%s is generated.\n\n", hwpc_group.env_str_hwpc.c_str());

	// header line showing event names
	fprintf(fp, "Section"); for (int i=7; i< maxLabelLen; i++) { fputc(' ', fp); } fputc('|', fp);
    for(int i=0; i<my_papi.num_sorted; i++) {
        kp = my_papi.s_sorted[i].find_last_of(':');
        if ( kp < 0) {
            s = my_papi.s_sorted[i];
        } else {
            s = my_papi.s_sorted[i].substr(kp+1);
        }
        fprintf (fp, " %10.10s", s.c_str() );
    }
	fprintf (fp, "\n");

	for (int i=0; i< maxLabelLen; i++) { fputc('-', fp); }  fputc('+', fp);
	for (int i=0; i<(my_papi.num_sorted*11); i++) { fputc('-', fp); } fprintf(fp, "\n");

#endif
  }



  /// Report the averaged HWPC statistics as the Basic report
  ///
  ///   @param[in] fp         report file pointer
  ///   @param[in] maxLabelLen    maximum label field string length
  ///
  ///     @note   remark that power consumption is reported per node, not per process
  ///
  void PerfWatch::printBasicHWPCsums(FILE* fp, int maxLabelLen)
  {
#ifdef USE_PAPI
    if (my_papi.num_events == 0) return;
    if ( m_count_sum == 0 ) return;
    if (my_rank != 0) return;

    std::string s;
    int ip, jp, kp;
	double dx;

	s = m_label;
	if (!m_exclusive)  { s = s + " (*)"; }
	if (m_in_parallel) { s = s + " (+)"; }

	// stats line showing the average value of HWPC stats

    //	fprintf(fp, "%s\n", s.c_str());
	fprintf(fp, "%-*s:", maxLabelLen, s.c_str() );
    for(int n=0; n<my_papi.num_sorted; n++) {
		dx=0.0;
		for (int i=0; i<num_process; i++) {
			dx += fabs(m_sortedArrayHWPC[i*my_papi.num_sorted + n]);
		}

		dx = dx / num_process;
		fprintf (fp, "  %9.3e", dx);
    }
	if (!m_exclusive) {
		fprintf (fp, " (*)\n");
	} else if (m_in_parallel) {
		fprintf (fp, " (+)\n");
	} else {
		fprintf (fp, "\n");
	}

#endif
  }



  /// PAPI HWPC測定結果を区間毎に出力
  ///
  ///   @param[in] fp 出力ファイルポインタ
  ///   @param[in] s_label 区間のラベル
  ///
  ///   @note ノード0からのみ呼び出し可能
  ///
  void PerfWatch::printDetailHWPCsums(FILE* fp, std::string s_label)
  {
#ifdef USE_PAPI
    if (my_papi.num_events == 0) return;
    //	if (!m_exclusive) return;
    if ( m_count_sum == 0 ) return;
    if (my_rank == 0) {
      outputPapiCounterHeader (fp, s_label);
      outputPapiCounterList (fp);
    }
#endif
  }



  ///   Groupに含まれるMPIプロセスのHWPC測定結果を区間毎に出力
  ///
  ///   @param[in] fp 出力ファイルポインタ
  ///   @param[in] s_label 区間のラベル
  ///   @param[in] p_group プロセスグループ番号。
  ///   @param[in] pp_ranks int*型 groupを構成するrank番号配列へのポインタ
  ///
  ///   @note ノード0からのみ呼び出し可能
  ///
  void PerfWatch::printGroupHWPCsums(FILE* fp, std::string s_label, MPI_Group p_group, int* pp_ranks)
  {
#ifdef USE_PAPI
    if (my_papi.num_events == 0) return;
    //	if (!m_exclusive) return;
    if ( m_count_sum == 0 ) return;
    if (my_rank == 0) outputPapiCounterHeader (fp, s_label);
    outputPapiCounterGroup (fp, p_group, pp_ranks);

#endif
  }



  /// Show the PMlib related environment variables
  ///
  ///   @param[in] fp report file pointer
  ///
  void PerfWatch::printEnvVars(FILE* fp)
  {

	char* cp_env;
	std::string s_chooser;

	fprintf(fp, "\tThe following cotroll variables are provided to PMlib as environment variable.\n");

#ifdef USE_PAPI
	cp_env = std::getenv("HWPC_CHOOSER");
	if (cp_env == NULL) {
		fprintf(fp, "\t\tHWPC_CHOOSER is not provided. USER is assumed.\n");
	} else {
		s_chooser = cp_env;
		if (s_chooser == "FLOPS" ||
			s_chooser == "BANDWIDTH" ||
			s_chooser == "VECTOR" ||
			s_chooser == "CACHE" ||
			s_chooser == "CYCLE" ||
			s_chooser == "LOADSTORE" ||
			s_chooser == "USER" ) {
			fprintf(fp, "\t\tHWPC_CHOOSER=%s \n", s_chooser.c_str());
			;
		} else {
			;
			//	fprintf(fp, "\tInvalid HWPC_CHOOSER value %s is ignored.\n", s_chooser.c_str());
		}
	}
#endif

#ifdef USE_POWER
	cp_env = std::getenv("POWER_CHOOSER");
	if (cp_env == NULL) {
		fprintf(fp, "\t\tPOWER_CHOOSER is not provided. OFF is assumed.\n");
	} else {
		s_chooser = cp_env;
		if (s_chooser == "OFF" || s_chooser == "NO" ||
			s_chooser == "NODE" ||
			s_chooser == "NUMA" ||
			s_chooser == "PARTS" ) {
			fprintf(fp, "\t\tPOWER_CHOOSER=%s \n", s_chooser.c_str());
		} else {
			;
			//	fprintf(fp, "\tInvalid POWER_CHOOSER value %s is ignored.\n", s_chooser.c_str());
		}
	}
#endif

#ifdef USE_OTF
    cp_env = std::getenv("OTF_TRACING");
    if (cp_env != NULL) {
	  fprintf(fp, "\t\tOTF_TRACING=%s \n", cp_env);
    }
#endif

	cp_env = std::getenv("PMLIB_REPORT");
	if (cp_env == NULL) {
		fprintf(fp, "\t\tPMLIB_REPORT is not provided. BASIC is assumed.\n");
	} else {
		s_chooser = cp_env;
		if (s_chooser == "BASIC" ||
			s_chooser == "DETAIL" ||
			s_chooser == "FULL" ) {
			fprintf(fp, "\t\tPMLIB_REPORT=%s \n", s_chooser.c_str());
		} else {
			; // ignore other values
		}
	}

  }


  /// スレッド別詳細レポートを出力。
  ///
  ///   @param[in] fp           出力ファイルポインタ
  ///   @param[in] rank_ID      出力対象プロセスのランク番号
  ///
  void PerfWatch::printDetailThreads(FILE* fp, int rank_ID)
  {
    double perf_rate;
	#ifdef DEBUG_PRINT_WATCH
	//	if (my_rank == 0) {
		fprintf(stderr, "\t <PerfWatch::printDetailThreads> my_rank=%d  arg:rank_ID=%d\n", my_rank, rank_ID);
	//	}
	#endif

	if(rank_ID<0 || rank_ID>num_process) return;

    std::string unit;
    int is_unit = statsSwitch();

    if (is_unit == 0) unit = "B/sec";	// 0: user set bandwidth
    if (is_unit == 1) unit = "Flops";	// 1: user set flop counts
    if (is_unit == 2) unit = "";		// 2: BANDWIDTH : HWPC measured data access bandwidth
    if (is_unit == 3) unit = "";		// 3: FLOPS     : HWPC measured flop counts
    if (is_unit == 4) unit = "";		// 4: VECTOR    : HWPC measured vectorization
    if (is_unit == 5) unit = "";		// 5: CACHE     : HWPC measured cache hit/miss
    if (is_unit == 6) unit = "";		// 6: CYCLE     : HWPC measured cycles, instructions
    if (is_unit == 7) unit = "";		// 7: LOADSTORE : HWPC measured load/store instruction type

	if (my_rank == 0 && is_unit < 2) {
	    //	fprintf(fp, "Label  %s%s\n", m_exclusive ? "" : "*", m_label.c_str());
		fprintf(fp, "Section : %s%s%s\n", m_label.c_str(), m_exclusive? "":" (*)" , m_in_parallel? " (+)":"" );

    	fprintf(fp, "Thread  call  time[s]  t/tav[%%]  operations  performance\n");
	} else 
	if (my_rank == 0 && is_unit >= 2) {
	    //	fprintf(fp, "Label  %s%s\n", m_exclusive ? "" : "*", m_label.c_str());
		fprintf(fp, "Section : %s%s%s\n", m_label.c_str(), m_exclusive? "":" (*)" , m_in_parallel? " (+)":"" );

		std::string s;
		int ip, jp, kp;
    	fprintf(fp, "Thread  call  time[s]  t/tav[%%]");
		for(int i=0; i<my_papi.num_sorted; i++) {
			kp = my_papi.s_sorted[i].find_last_of(':');
			if ( kp < 0) {
				s = my_papi.s_sorted[i];
			} else {
				s = my_papi.s_sorted[i].substr(kp+1);
			}
			fprintf (fp, " %10.10s", s.c_str() );
		} fprintf (fp, "\n");
	}

	int i=rank_ID;

	// Well, we are going to destroy the process based stats with thread based stat.
	// Let's save some of them for later re-use.
	long save_m_count;
	double save_m_time, save_m_flop, save_m_time_av;
	save_m_count = m_count;
	save_m_time  = m_time;
	save_m_flop  = m_flop;
	save_m_time_av  = m_time_av;

	for (int j=0; j<num_threads; j++)
	{
		if ( !m_in_parallel && is_unit < 2 ) {

			if (j == 1) {
				if (my_rank == 0) {
				// user mode thread statistics for worksharing construct
				// are always represented by thread 0, because they can not be
				// split into threads in artificial manner.
				fprintf(fp, " %3d\t\t user mode worksharing threads are represented by thread 0\n", j);
				}
				continue;
			}
			if (j >= 1) {
				if (my_rank == 0) {
				fprintf(fp, " %3d\t\t ditto\n", j);
				}
				continue;
			}
		}

		PerfWatch::selectPerfSingleThread(j);

			#ifdef DEBUG_PRINT_PAPI_THREADS
			fprintf(stderr, "\t<printDetailThreads> calls <gatherThreadHWPC> \n");
			#endif


		PerfWatch::gatherThreadHWPC();

			#ifdef DEBUG_PRINT_PAPI_THREADS
			fprintf(stderr, "\t<printDetailThreads> calls <gather> \n");
			#endif

		PerfWatch::gather();

			#ifdef DEBUG_PRINT_PAPI_THREADS
			fprintf(stderr, "\t<printDetailThreads> prints  \n");
			#endif

		if (my_rank == 0) {
			if (is_unit < 2) {
			perf_rate = (m_countArray[i]==0) ? 0.0 : m_flopArray[i]/m_timeArray[i];
			fprintf(fp, " %3d%8ld  %9.3e  %5.1f   %9.3e  %9.3e %s\n",
				j,
				m_countArray[i], // コール回数
				m_timeArray[i],  // 時間
				100*m_timeArray[i]/m_time_av, // 時間の比率
				m_flopArray[i],  // 演算数
				perf_rate,       // スピード　Bytes/sec or Flops
				unit.c_str()     // スピードの単位
				);
				(void) fflush(fp);
			}
			else 
			if (is_unit >= 2) {
			fprintf(fp, " %3d%8ld  %9.3e  %5.1f ",
				j,
				m_countArray[i], // コール回数
				m_timeArray[i],  // 時間
				100*m_timeArray[i]/m_time_av);
	
				for(int n=0; n<my_papi.num_sorted; n++) {
				fprintf (fp, "  %9.3e", fabs(m_sortedArrayHWPC[i*my_papi.num_sorted + n]));
				}
				fprintf (fp, "\n");
				(void) fflush(fp);
			}
		}	// end of if (my_rank == 0) 
		#pragma omp barrier
	}	// end of for (int j=0; j<num_threads; j++)
	m_count = save_m_count;
	m_time  = save_m_time;
	m_flop  = save_m_flop;
	m_time_av  = save_m_time_av;

		#ifdef DEBUG_PRINT_PAPI_THREADS
		fprintf(stderr, "\t<printDetailThreads> returns  \n");
		#endif

  }



  ///	Select the single thread value for reporting
  ///
  ///   @param[in] i_thread : choosing thread number
  ///
  void PerfWatch::selectPerfSingleThread(int i_thread)
  {
	for (int ip=0; ip<my_papi.num_events; ip++) {
		my_papi.accumu[ip] = my_papi.th_accumu[i_thread][ip];
	}

    	//	int is_unit = statsSwitch();
		//	if (is_unit < 2 && !m_in_parallel) {
	if (m_in_parallel) {
		m_count = llround(my_papi.th_v_sorted[i_thread][0]);
		m_time = my_papi.th_v_sorted[i_thread][1];
		m_flop = my_papi.th_v_sorted[i_thread][2];
	} else {
		m_count = llround(my_papi.th_v_sorted[0][0]);
		m_time = my_papi.th_v_sorted[0][1];
		m_flop = my_papi.th_v_sorted[0][2];
	}

#ifdef DEBUG_PRINT_PAPI_THREADS
	//    if (my_rank == 0) {
	//    	fprintf(stderr, "\t <selectPerfSingleThread> [%s] i_thread=%d, m_time=%e, m_flop=%e, m_count=%lu\n",
	//						m_label.c_str(), i_thread, m_time, m_flop, m_count );
	//    }
#endif
  }


  /// printing the HWPC Legend and Power API Legend
  ///
  ///   @param[in] fp output file pointer
  ///
  void PerfWatch::printHWPCLegend(FILE* fp)
  {
#ifdef USE_PAPI
	outputPapiCounterLegend (fp);
#endif

#ifdef USE_POWER
	fprintf(fp, "\n    Symbols in PMlib power consumption report: \n" );
	fprintf(fp, "\t The available POWER_CHOOSER values and their output data are shown below.\n\n");

	if (hwpc_group.platform == "A64FX" ) {
	
		fprintf(fp, "\t POWER_CHOOSER=OFF(default):\n");
		fprintf(fp, "\t\t power consumption report is not produced: \n" );
	
		fprintf(fp, "\t POWER_CHOOSER=NODE:\n");
		fprintf(fp, "\t\t total     : Total of all parts. (CMG + MEMORY + TF+A+U) \n");
		fprintf(fp, "\t\t CMG+L2    : All compute cores and L2 cache memory in all 4 CMGs \n");
		fprintf(fp, "\t\t MEMORY    : Main memory (HBM)\n");
		fprintf(fp, "\t\t TF+A+U    : TofuD network router and interface + Assistant cores + other UnCMG parts \n");
		fprintf(fp, "\t\t Energy[Wh]: power comsumption in watt-hour unit\n");
	
		fprintf(fp, "\t POWER_CHOOSER=NUMA:\n");
		fprintf(fp, "\t\t total     : Total of all parts. (CMG[0-3] + MEM[0-3] + TF+A+U)\n");
		fprintf(fp, "\t\t CMG0+L2   : compute cores and L2 cache memory in CMG0. ditto for CMG[1-3]+L2. \n");
		fprintf(fp, "\t\t MEM[0-3]  : Main memory (HBM) attached to CMG0[1,2,3]\n");
		fprintf(fp, "\t\t TF+A+U    : TofuD network router and interface + Assistant cores + other UnCMG parts \n");
		fprintf(fp, "\t\t Energy[Wh]: power comsumption in watt-hour unit\n");
	
		fprintf(fp, "\t POWER_CHOOSER=PARTS:\n");
		fprintf(fp, "\t\t total     : Total of all parts. \n");
		fprintf(fp, "\t\t CMG[0-3]  : compute cores in CMG0, CMG1, CMG2, CMG3 \n");
		fprintf(fp, "\t\t L2CMG[0-3]: L2 cache memory in CMG0, CMG1, CMG2, CMG3 \n");
		fprintf(fp, "\t\t Acore[0-1]: Assistant core 0, 1. \n");
		fprintf(fp, "\t\t TofuD     : TofuD network router and interface \n");
		fprintf(fp, "\t\t UnCMG     : Other UnCMG parts (CPU parts excluding compute cores, assistant cores or TofuD) \n");
		fprintf(fp, "\t\t PCI       : PCI express interface \n");
		fprintf(fp, "\t\t TofuOpt   : Tofu optical modules \n");
		fprintf(fp, "\t\t Energy[Wh]: power comsumption in watt-hour unit\n");
	}
	fprintf(fp, "\n");
#endif
  }



  /// print an error message
  ///
  ///   @param[in] func  name of a function
  ///   @param[in] fmt   output text contents and format
  ///
  void PerfWatch::printError(const char* func, const char* fmt, ...)
  {
    if (my_rank == 0) {
      fprintf(stderr, "\n\n*** PMlib Error. PerfWatch::%s [%s] \n",
                      func, m_label.c_str());
      va_list ap;
      va_start(ap, fmt);
      vfprintf(stderr, fmt, ap);
      va_end(ap);
    }
  }


#if defined (USE_PRECISE_TIMER) // Platform specific precise timer
	#if defined (__APPLE__)				// Mac Clang and/or GCC
		#include <unistd.h>
		#include <mach/mach.h>
		#include <mach/mach_time.h>
	#elif defined (__FUJITSU)			// Fugaku A64FX, FX100, K computer
		#include <fjcex.h>
	#endif
#endif

  /// 時刻を取得
  ///
  ///   @return 時刻値(秒)
  ///
  double PerfWatch::getTime()
  {
#if defined (USE_PRECISE_TIMER) // Platform specific precise timer
	#if defined (__APPLE__)				// Mac Clang and/or GCC
		//	printf("[__APPLE__] is defined.\n");
		//	return ((double)mach_absolute_time() * second_per_cycle);
		// mach_absolute_time() appears to return nano-second unit value
		return ((double)mach_absolute_time() * 1.0e-9);

	#elif defined (__FUJITSU)			// Fugaku A64FX, FX100, K computer and Fujitsu compiler/library
		//	printf("[__FUJITSU] is defined.\n");
		register double tval;
		tval = __gettod()*1.0e-6;
		return (tval);

	#elif defined(__x86_64__)			// Intel Xeon processor
		#if defined (__INTEL_COMPILER) || (__gnu_linux__)
		// Can replace the following code segment using inline assembler
		unsigned long long tsc;
		unsigned int lo, hi;
		__asm __volatile__ ( "rdtsc" : "=a"(lo), "=d"(hi) );
		tsc = ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
		return ((double)tsc * second_per_cycle);

		#else	// precise timer is not available. use gettimeofday() instead.
		struct timeval tv;
		gettimeofday(&tv, 0);
		return (double)tv.tv_sec + (double)tv.tv_usec * 1.0e-6;
		#endif
	#else		// precise timer is not available. use gettimeofday() instead.
		struct timeval tv;
		gettimeofday(&tv, 0);
		return (double)tv.tv_sec + (double)tv.tv_usec * 1.0e-6;
	#endif
#else // Portable timer gettimeofday() on Linux, Unix, Macos
	struct timeval tv;
	gettimeofday(&tv, 0);
	return (double)tv.tv_sec + (double)tv.tv_usec * 1.0e-6;
#endif

  }


  void PerfWatch::read_cpu_clock_freq()
  {
	cpu_clock_freq = 1.0;
	second_per_cycle = 1.0;
#if defined (USE_PRECISE_TIMER)
	#if defined (__APPLE__)
		FILE *fp;
		char buf[256];
		long long llvalue;
		char cpuvendor[80], cputype[80];
		std::string s_cpuvendor, s_cputype;

	// The following block valid for Intel core only. But not for arm core.
		fp = popen("sysctl -n machdep.cpu.brand_string", "r");
		if (fp == NULL) {
			printError("<read_cpu_clock_freq>",  "popen(sysctl) failed. \n");
			return;
		}
		if (fgets(buf, sizeof(buf), fp) != NULL) {
			//	string "Intel(R) Core(TM) i7-6820HQ CPU @ 2.70GHz"
			//	string "Apple M1 Max"
			sscanf(buf, "%s %s", cpuvendor, cputype);
			s_cpuvendor = cpuvendor;
			s_cputype = cputype;
		} else {
			printError("<read_cpu_clock_freq>",  "unexpected end of record.\n");
		}

		if (s_cpuvendor == "Intel(R)") {
			fp = popen("sysctl -n hw.cpufrequency", "r");
			//		integer 2700000000
			if (fp == NULL) {
				printError("<read_cpu_clock_freq>",  "no hw.cpufrequency\n");
				return;
			}
			if (fgets(buf, sizeof(buf), fp) != NULL) {
				sscanf(buf, "%lld", &llvalue);
				if (llvalue <= 0) {
					printError("<read_cpu_clock_freq>",  "hw.cpufrequency value is not valid\n");
					llvalue=1.0;
				}
				cpu_clock_freq = (double)llvalue;
				second_per_cycle = 1.0/cpu_clock_freq;
				#ifdef DEBUG_PRINT_WATCH
				if (my_rank == 0) {
				fprintf(stderr, "<read_cpu_clock_freq> cpu_clock_freq=%lf, second_per_cycle=%16.12lf \n",
									cpu_clock_freq, second_per_cycle);
				}
				#endif
			} else {
				printError("<read_cpu_clock_freq>",  "can not detect Intel hw.cpufrequency \n");
			}
		} else
		if (s_cpuvendor == "Apple") {
		//	
		//	Apple M1/M2/M3 silicon does not support sysctl hw.cpufrequency
		//	Root may try it as: powermetrics -s cpu_power -n 1 | grep -i freq
		//	But no idea to extract silicon frequency at user level
		//	So, hard code here, instead.
		//	
		//		string "Apple M1 Max"
			if (s_cputype == "M1") {
				cpu_clock_freq = 3200000000.0;
			} else if (s_cputype == "M2") {
				cpu_clock_freq = 3490000000.0;
			} else if (s_cputype == "M3") {
				cpu_clock_freq = 4050000000.0;
			} else {
				printError("<read_cpu_clock_freq>",  "Unknown Apple silicon\n");
				return;
			}
			second_per_cycle = 1.0/cpu_clock_freq;
			#ifdef DEBUG_PRINT_WATCH
			fprintf(stderr, "<read_cpu_clock_freq> cpu_clock_freq=%lf, second_per_cycle=%16.12lf \n",
								cpu_clock_freq, second_per_cycle);
			#endif
		} else {
			printError("<read_cpu_clock_freq>",  "unknown Mac cpu vendor %s\n", cpuvendor);
		}

		pclose(fp);
		return;

	#elif defined (__FUJITSU)			// Fugaku A64FX, FX100, K computer and Fujitsu compiler/library
		//	__gettod() on Fujitsu compiler/library doesn't require cpu_clock_freq
		return;

	#elif defined(__x86_64__)					// Intel Xeon
		#if defined (__INTEL_COMPILER) || (__gnu_linux__)
	    // read the cpu freqency from /proc/cpuinfo

	    FILE *fp;
	    double value;
	    char buffer[1024];
	    fp = fopen("/proc/cpuinfo","r");
	    if (fp == NULL) {
	    	printError("<read_cpu_clock_freq>",  "Can not open /proc/cpuinfo \n");
	    	return;
	    }
	    while (fgets(buffer, 1024, fp) != NULL) {
			//	fprintf(stderr, "<read_cpu_clock_freq> buffer=%s \n", buffer);
	    	if (!strncmp(buffer, "cpu MHz",7)) {
	    		sscanf(buffer, "cpu MHz\t\t: %lf", &value);
	    		// sscanf handles regexp such as: sscanf (buffer, "%[^\t:]", value);
	    		cpu_clock_freq = (value * 1.0e6);
	    		break;
	    	}
	    }
	    fclose(fp);
	    if (cpu_clock_freq <= 0.0) {
	    	printError("<read_cpu_clock_freq>",  "Failed parsing /proc/cpuinfo \n");
	    	return;
	    }
	    second_per_cycle = 1.0/(double)cpu_clock_freq;
		#ifdef DEBUG_PRINT_WATCH
	   	if (my_rank == 0 && my_thread == 0) {
			fprintf(stderr, "<read_cpu_clock_freq> cpu_clock_freq=%lf second_per_cycle=%16.12lf \n",
								cpu_clock_freq, second_per_cycle);
	   	 }
		#endif

		#else
		return;
	    #endif
	#endif
#endif	// (USE_PRECISE_TIMER)
    return;
  }

} /* namespace pm_lib */

