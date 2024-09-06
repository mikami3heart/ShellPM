
#include "PerfMonitor.h"
#include <time.h>
#include <unistd.h> // for getppid()
#include <cmath>
#include "power_obj_menu.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <cerrno>


namespace pm_lib {
//
//	PerfMonitor class
//

  void PerfMonitor::save_pm_records(void)
  {
    if (!is_PMlib_enabled) return;

	std::string dir_name;
	std::string file_name;
	PerfMonitor::pm_storage_dir_name(dir_name);
	PerfMonitor::pm_storage_file_name(file_name);

	int iret = mkdir(dir_name.c_str(), 0700);

	//	std::string mkdir_command = "mkdir -p " + dir_name;
	//	int iret = system(mkdir_command.c_str() );


	// Note:
	// system("mkdir .. ") argument can contain shell variables.
	// mkdir(" .. ") argument does not evaluate shell variables.

		//	mkdir("~/xyz123", 0700);				// NG
		//	mkdir("${HOME}/xyz123", 0700);			// NG
		//	mkdir("/Users/mikami/xyz123", 0700);	// OK!

		//	system("mkdir -p ~/xyz123");			// OK!
		//	system("mkdir -p ${HOME}/xyz123");		// OK!
		//	system("mkdir -p /Users/mikami/xyz123");	// OK!

	#ifdef DEBUG_PRINT_MONITOR
	if (iret == 0) {
		fprintf(stderr, "ShellPM created directory : %s\n", dir_name.c_str());
	} else {
		fprintf(stderr, "mkdir %s, returned=%d, errno=%d \n", dir_name.c_str(), iret, errno);
	}
	#endif

	file_name = dir_name + "/" + file_name;

	#ifdef DEBUG_PRINT_MONITOR
	fprintf(stderr, "<save_pm_records> writing to %s\n", file_name.c_str());
	#endif

	FILE *fp;
	fp=fopen(file_name.c_str(),"w+");
	if (fp == NULL) {
		fprintf(stderr, "*** ShellPM Error. <save_pm_records> can not open %s ***\n", file_name.c_str());
		exit(99);
	}

	fprintf(fp,"ShellPM HWPC_CHOOSER=%s\n", env_str_hwpc.c_str());
	for (int i=0; i<m_nWatch; i++) {
		m_watchArray[i].save_pm_records(fp);
		#ifdef USE_POWER
		if (level_POWER != 0)
		//	m_watchArray[id].save_power_records( pm_pacntxt, pm_extcntxt, pm_obj_array, pm_obj_ext);
		#endif
	}

	fclose(fp);
  }

  void PerfMonitor::load_pm_records(void)
  {
    if (!is_PMlib_enabled) return;

	std::string file_name;
	std::string dir_name;
	PerfMonitor::pm_storage_file_name(file_name);
	PerfMonitor::pm_storage_dir_name(dir_name);

	file_name = dir_name + "/" + file_name;

	FILE *fp;
	fp=fopen(file_name.c_str(),"r");
	if (fp == NULL) {
		fprintf(stderr, "*** ShellPM Error. <load_pm_records> can not open %s\n", file_name.c_str());
		exit(99);
	}

	#ifdef DEBUG_PRINT_MONITOR
	fprintf(stderr, "<load_pm_records> reading %s\n", file_name.c_str());
	#endif

	fprintf(fp,"ShellPM HWPC_CHOOSER=%s\n", env_str_hwpc.c_str());
	for (int i=0; i<m_nWatch; i++) {
		m_watchArray[i].load_pm_records(fp);
		#ifdef USE_POWER
		if (level_POWER != 0)
		//	m_watchArray[id].load_power_records( pm_pacntxt, pm_extcntxt, pm_obj_array, pm_obj_ext);
		#endif
	}
	fclose(fp);

	// delete the data record
	int iret = remove(file_name.c_str());
	if (iret != 0) {
		fprintf(stderr, "*** failed to remove file: %s\n", file_name.c_str());
		return;
	}
	#ifdef DEBUG_PRINT_MONITOR
	fprintf(stderr, "ShellPM removed file: %s\n", file_name.c_str());
	#endif


/**
	iret = remove(dir_name.c_str());
	if (iret != 0) {
		fprintf(stderr, "*** failed to remove dir: %s\n", dir_name.c_str());
		return;
	}
**/

  }

  void PerfMonitor::pm_storage_file_name(std::string& pm_file_name)
  {
	char* cp_env;
	std::string s_filename;

	cp_env = std::getenv("PJM_JOBNAME");
	if (cp_env == NULL) {
		s_filename = "shellpm";
    } else {
        s_filename = cp_env;
    }
	cp_env = std::getenv("PJM_JOBID");
	if (cp_env == NULL) {
        s_filename += ".record";
    } else {
        s_filename += ".";
        s_filename += cp_env;
    }
	s_filename += "." + std::to_string(getppid());

    pm_file_name = s_filename;

	#ifdef DEBUG_PRINT_MONITOR
	fprintf(stderr, "<pm_storage_file_name> returns: %s\n", pm_file_name.c_str());
	#endif
  }

  void PerfMonitor::pm_storage_dir_name(std::string& pm_dir_name)
  {

	// Note:
	// The argument to mkdir() must be a full path name.
	// The argument to system("mkdir .. ") can contain shell variables.
	// We compose the full path name that can be used by both system calls
	//

	char* cp_env;
	std::string s_dirname;
	cp_env = std::getenv("HOME");
	if (cp_env == NULL) {
		cp_env = std::getenv("USER");
		s_dirname = "/tmp/" + std::string(cp_env);
    } else {
        s_dirname = cp_env;
    }

    pm_dir_name = s_dirname + "/.shellpm_data";

	#ifdef DEBUG_PRINT_MONITOR
	fprintf(stderr, "<pm_storage_dir_name> returns: %s\n", pm_dir_name.c_str());
	#endif
  }

//
//	PerfWatch class
//

  void PerfWatch::save_pm_records(FILE* fp)
  {
	#ifdef DEBUG_PRINT_MONITOR
	fprintf(stderr, "<PerfWatch::save_pm_records> section [%s]\n", m_label.c_str());
	#endif

	//
	// Saving the values in external storage device
	//

//    m_started;	// = should be true;
//    m_threads_merged;	// = should be false;
//    m_startTime;	// = getTime();
	fprintf(fp, "section %s m_startTime= %20.15e\n", m_label.c_str(), m_startTime);
	fprintf(fp, "num_threads= %d, my_papi.num_events= %d\n", num_threads, my_papi.num_events);
	fprintf(fp, "my_papi.th_values[num_threads][my_papi.num_events]:\n" );

	for (int j=0; j<num_threads; j++) {
        for (int i=0; i<my_papi.num_events; i++) {
			fprintf(fp, "%lld\n", my_papi.th_values[j][i]);
        }
	}
	#ifdef USE_POWER
	if (level_POWER != 0)
;
	#endif
  }


  void PerfWatch::load_pm_records(FILE* fp)
  {
;
  }

} /* namespace pm_lib */

