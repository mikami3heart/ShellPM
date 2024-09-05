#include <PerfMonitor.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#include <stdio.h>
#include <math.h>
#include <string>
#include <sstream>
using namespace pm_lib;

int my_id, npes, num_threads;

PerfMonitor PM;

int main (int argc, char *argv[])
{
	int num_threads;

#ifdef _OPENMP
	char* c_env = std::getenv("OMP_NUM_THREADS");
// 
// ここちょっと変だね。後で直さなくちゃ。
// 
	if (c_env == NULL) {
		num_threads  = 1;
	} else {
		num_threads  = omp_get_max_threads();
	}
#else
	num_threads  = 1;
#endif

	fprintf(stderr, "\t<ShellPM> starts. max_threads=%d\n", num_threads);

	PM.initialize();
	PM.start("ShellPM");
	PM.save_pm_records();
	return 0;
}

