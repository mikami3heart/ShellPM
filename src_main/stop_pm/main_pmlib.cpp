#include <PerfMonitor.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#include <stdio.h>
#include <math.h>
#include <string>
#include <sstream>
using namespace pm_lib;
PerfMonitor PM;

int main (int argc, char *argv[])
{

	fprintf(stderr, "\t<ShellPM> stop and report\n");
	PM.initialize();
	PM.start("ShellPM");
	PM.load_pm_records();
	PM.stop("ShellPM");
	PM.report(stdout);
	return 0;
}

