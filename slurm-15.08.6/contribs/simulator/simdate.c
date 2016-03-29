#ifdef SLURM_SIMULATOR
/*
 * simdate: will print the time of the simulator (when using the simulator).
 *          Doubles as a means to view the rest of the Simulator's shared
 *          memory segment and to modify select fields for testing/debugging
 *          purposes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>  /* Map call and definitions */
#include <pthread.h>
#include <semaphore.h> /* For sem_open */
#include <time.h>      /* For ctime */
#include <fcntl.h>     /* O_RDWR */
#include <getopt.h>    /* getopt_long call */
#include "src/common/slurm_sim.h"

/* Pointers to shared memory data */
void     *     timemgr_data;
uint32_t *     current_sim;
uint32_t *     current_micro;
pid_t    *     sim_mgr_pid;
pid_t    *     slurmctl_pid;
int      *     slurmd_count;
int      *     slurmd_registered;
int      *     global_sync_flag;
pid_t    *     slurmd_pid;

/* Simulator synchronization variables */
char           syn_sem_name[]  = "serversem";
sem_t *        mutexserver = SEM_FAILED;

/* Global variables supporting debugging functionality */
int     incr                 = 0;
char    new_global_sync_flag = ' ';

/* Funtions */
const  char * _format_date();
extern void sim_close_sem(sem_t ** mutex_sync);
extern int sim_open_sem(char * sem_name, sem_t ** mutex_sync, int max_attempts);


static struct option long_options[] = {
	{"showmem",		0, 0, 's'},
	{"timeincr",		1, 0, 'i'},
	{"flag",		1, 0, 'f'},
	{"help",		0, 0, 'h'}
};
char optstr[] = "si:f:h";
char help_msg_fmt[] = "\
simdate [OPTIONS]\n\
	  -s, --showmem             Display contents of shared memory\n\
	  -i, --timeincr seconds    Increment simulated time by 'seconds'\n\
	  -f, --flag 1-%d            Set the global synchronization flag\n\
	  -h, --help                This help message\n\
\n\nNotes: The simdate command's primary function is to\n\
display the current simulated time.  If no arguments are\n\
given then this is the output.  However, it also serves to\n\
both display the synchronization semaphore value\n\
and the contents of the shared memory segment.\n\
Furthermore, it can increment the simulated time and set\n\
the global sync flag.\n";
char help_msg[2048];

/* Shared Memory Segment Functions */

const char* _format_date()
{
	time_t now = *current_sim;
	return ctime(&now);
}

/* Returns 1 if valid and 0 invalid */
int validate_sync_flag()
{
	int rv = 0;
	if (new_global_sync_flag >= 1 &&
			new_global_sync_flag <= *slurmd_count + 1)
		rv = 1;
	return rv;
}

int main (int argc, char *argv[])
{
	int val = -22;
	int option_index, opt_char, show_mem = 0;

	sprintf(help_msg, help_msg_fmt, *slurmd_count + 1);

	if (argc == 1) {
		printf("%s", _format_date());
		return 0;
	}

	while ((opt_char = getopt_long(argc, argv, optstr, long_options,
						&option_index)) != -1) {
		switch (opt_char) {
		case ('s'): show_mem = 1;                     break;
		case ('i'): incr = atoi(optarg);              break;
		case ('f'):
			new_global_sync_flag = atoi(optarg);

			if (!validate_sync_flag()) {
				printf("Error! Invalid Sync Flag.\n");
				return -1;
			}
			break;
		case ('h'): printf("%s", help_msg);           return 0;
		};
	}

	/* Display semaphore sync value if requested via command-line option */
	if (show_mem) {
		int sem_opened =
			sim_open_sem(syn_sem_name, &mutexserver, 1);

		if(mutexserver!=SEM_FAILED) sem_getvalue(mutexserver, &val);
		printf("Global Sync Semaphore Value: %d\n", val);

		if (!sem_opened)
			sim_close_sem(&mutexserver);
	}

	/* Get Semaphore Information if it exists */
	if (timemgr_data) {
		current_sim       = timemgr_data + SIM_SECONDS_OFFSET;
		current_micro     = timemgr_data + SIM_MICROSECONDS_OFFSET;
		sim_mgr_pid       = timemgr_data + SIM_SIM_MGR_PID_OFFSET;
		slurmctl_pid      = timemgr_data + SIM_SLURMCTLD_PID_OFFSET;
		slurmd_count      = timemgr_data + SIM_SLURMD_COUNT_OFFSET;
		slurmd_registered = timemgr_data + SIM_SLURMD_REGISTERED_OFFSET;
		global_sync_flag  = timemgr_data + SIM_GLOBAL_SYNC_FLAG_OFFSET;
		slurmd_pid        = timemgr_data + SIM_SLURMD_PID_OFFSET;

		/* Change any values if requested via command-line options */
		if(incr) {
			printf("Incrementing the Simulator time by %d seconds.\n",
							incr);
			*current_sim += incr;
		}

		if(new_global_sync_flag != ' ') {
			printf("Changing the Simulator sync flag to %c.\n",
							new_global_sync_flag);
			*global_sync_flag = new_global_sync_flag;
		}

		/* Display shared mem values if requested via command-line option */
		if (show_mem) {
			printf("current_sim       = %u %s\n", *current_sim,
							_format_date());
			printf("current_micro     = %u\n", *current_micro);
			printf("sim_mgr_pid       = %d\n",  *sim_mgr_pid);
			printf("slurmctl_pid      = %d\n",  *slurmctl_pid);
			printf("slurmd_count      = %d\n",  *slurmd_count);
			printf("slurmd_registered = %d\n",  *slurmd_registered);
			printf("global_sync_flag  = %d\n",  *global_sync_flag);
			printf("slurmd_pid        = %d\n",  *slurmd_pid);
		}
	} else {
		printf("The shared memory segment (%s) was not found.\n",
							SLURM_SIM_SHM);
	}

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
