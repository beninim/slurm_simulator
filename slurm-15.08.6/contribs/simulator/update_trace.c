#ifdef SLURM_SIMULATOR

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>

#include "sim_trace.h"

/*
 * This command modifies a job trace file depending on options
 * given by the user:
 *
 * -R implies linking jobs to reservations
 * -D implies linking a job to other by dependency
 *
 */


static struct option long_options[] = {
	{"reservation",	0, 0, 'R'},
	{"dependency",	0, 0, 'D'},
	{"rsv_name",	1, 0, 'n'},
	{"jobid",	1, 0, 'j'},
	{"ref_jobid",	1, 0, 'r'},
	{"account",	1, 0, 'a'},
	{"help",	0, 0, 'h'},
	{NULL,		0, 0, 0}
};

char help_msg[] = "\
[This command is deprecated.  Use edit_trace]\n\
update_trace [OPTIONS]\n\
      -R, --reservation           States to perform a reservation update\n\
      -D, --dependency            States to perform a dependency update\n\
      -n, --rsv_name     name     Name of reservation to use\n\
      -j, --jobid        jid      Select job 'jid' to modify\n\
      -r, --ref_jobid    rjid     Set 'rjid' as the target dependency\n\
      -a, --account\n\
      -h, --help                  This help message\n\
Notes:  There are two general formats, one for a dependency update and one\n\
        for reservation updates.\n\
	update_trace [-D | --dependency] [-j | --jobid] [-r | --ref_jobid]>\n\
	update_trace [-R | --reservation] [-n | --rsv_name] [-j | --jobid]\n\
                     [-a | --account]\n";

char *rsv_name;
int  reservation_opt = 0;
int  dependency_opt  = 0;
int  jobid;
char *ref_jobid;
char *account;

int main(int argc, char *argv[]) {

	job_trace_t job_trace;
	ssize_t     written;
	int         trace_file, new_file;
	int         option_index;
	int         opt_char, errs = 0;


	while((opt_char = getopt_long(argc, argv, "RDn:j:r:a:h",
			long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'R':
			printf("Reservation option\n");
			reservation_opt = 1;
			break;

		case (int)'D':
			printf("Dependency option\n");
			dependency_opt = 1;
			break;

		case (int)'n':
			rsv_name = strdup(optarg);
			printf("Parsing reservation name to %s\n", rsv_name);
			break;

		case (int)'j':
			jobid = atoi(optarg);
			printf("Parsing jobid to %d\n", jobid);
			break;

		case (int)'r':
			ref_jobid = strdup(optarg);
			break;

		case (int)'a':
			account = strdup(optarg);
			printf("Parsing account to %s\n", account);
			break;
		case (int)'h':
			printf("Usage: %s\n", help_msg);
			break;

		default:
			fprintf(stderr, "getopt error, returned %c\n",
				opt_char);
			exit(0);
		}
	}

	if (!reservation_opt && !dependency_opt) {
		printf("Command needs to specify reservation or dependency "
		       "action\n");
		return -1;
	}

	if (reservation_opt) {
		if ((rsv_name == NULL) || (jobid == 0) || (account == NULL)) {
			printf("Reservation option needs:\n\t --rsv_name and \n"
				"\t--jobid\n\t--account\n");
			return -1;
		}
	}
	if (dependency_opt) {
		if ((ref_jobid == NULL) || (jobid == 0)) {
			printf("Dependency option needs --jobid and --"
			       "ref_jobid\n");
			return -1;
		}
	}


	trace_file = open("test.trace", O_RDONLY);
	if (trace_file < 0) {
		printf("Error opening test.trace\n");
		return -1;
	}

	new_file = open(".test.trace.new", O_CREAT | O_RDWR, S_IRWXU);
	if (new_file < 0) {
		printf("Error creating temporal file at /tmp\n");
		return -1;
	}

	while (read(trace_file, &job_trace, sizeof(job_trace))) {

		if (reservation_opt) {
			if (job_trace.job_id != jobid) {
				written = write(new_file, &job_trace,
							sizeof(job_trace));
				if (written != sizeof(job_trace)) {
					printf("Error writing to file: "
						"%ld of %ld\n",
						written, sizeof(job_trace));
					++errs;
				}
				continue;
			}

			sprintf(job_trace.reservation, "%s", rsv_name);
			sprintf(job_trace.account, "%s", account);
		}

		if (dependency_opt) {
			if (job_trace.job_id != jobid) {
				written = write(new_file, &job_trace,
							sizeof(job_trace));
				if (written != sizeof(job_trace)) {
					printf("Error writing to file: "
						"%ld of %ld\n",
						written, sizeof(job_trace));
					++errs;
				}
				continue;
			}

			sprintf(job_trace.dependency, "%s", ref_jobid);
		}

		written = write(new_file, &job_trace, sizeof(job_trace));
		if (written != sizeof(job_trace)) {
			printf("Error writing to file: %ld of %ld\n",
						written, sizeof(job_trace));
			++errs;
		}
	}

	close(trace_file);
	close(new_file);

	if (!errs)
		if (rename(".test.trace.new", "./test.trace") < 0)
			printf("Error renaming file: %d\n", errno);

	return 0;
}
#else
#include <stdio.h>
int
main(int argc, char* argv[])
{
	printf("%s placeholder.  Only does something if built in simulator mode"
	       " (LIBS=-lrt CFLAGS=\"-D SLURM_SIMULATOR\").\n", argv[0]);
	return 0;
}
#endif
