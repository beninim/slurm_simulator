#ifdef SLURM_SIMULATOR

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "sim_trace.h"
#include "src/common/job_resources.h"

#include "semaphore.h"
#include "src/common/slurm_sim.h"

#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


FILE*        fp;
char         outfname[64];
char         default_trace_file[] = "test.trace";
char*        trace_file_name = NULL;
char         optstr[] = "ho:d:";
time_t       now;
int          trace_file;
job_trace_t  new_trace;
int          written;
int          dur_meth = 3;

static struct option long_options[] = {
	{"output",		1, 0, 'o'},
	{"duration_method",	1, 0, 'd'},
	{"help",		0, 0, 'h'}
};

char help_msg [] = "\
Usage: simqsnap [OPTIONS]\n\
    -o, --output file         The name of the workload trace file to produce.\n\
    -d, --duration_method num Number representing the method of determining\n\
			      the expected end time of the job.\n\
			      1 = random time length (1-max time for job)\n\
			      2 = amount of time that the job had been \n\
				  running on the real system at snapshot time\n\
				  If pending, then use expected time.\n\
			      3 = Expected time (the time limit or wclimit)\n\
				  if not finished--Default\n\
				  (Note: finished jobs don't typically\n\
				  linger long in the queue)\n\
    -h, --help                This help message\
";

/* Simulator synchronization variables */
char           syn_sem_name[]  = "serversem";
sem_t *        mutexserver = SEM_FAILED;

void recordRecord(slurm_job_info_t* j);
void recordJobResources(job_resources_t* jrcs);
int recordJob(slurm_job_info_t* j);

#define JOBSTATE(s)		(	\
        (s == JOB_PENDING)    ? "JOB_PENDING"   : \
        (s == JOB_RUNNING)    ? "JOB_RUNNING"   : \
        (s == JOB_SUSPENDED)  ? "JOB_SUSPENDED" : \
        (s == JOB_COMPLETE)   ? "JOB_COMPLETE"  : \
        (s == JOB_CANCELLED)  ? "JOB_CANCELLED" : \
        (s == JOB_FAILED)     ? "JOB_FAILED"    : \
        (s == JOB_TIMEOUT)    ? "JOB_TIMEOUT"   : \
        (s == JOB_NODE_FAIL)  ? "JOB_NODE_FAIL" : \
        (s == JOB_PREEMPTED)  ? "JOB_PREEMPTED" : \
        (s == JOB_END)        ? "JOB_END" : "UNKNOWN"  )


void
procArgs(int argc, char** argv) {
	int opt_char, option_index;
	while ((opt_char = getopt_long(argc, argv, optstr,
					long_options, &option_index)) != -1) {
		switch (opt_char) {
		    case (int)'o':
			trace_file_name = strdup(optarg);
			break;
		    case (int)'d':
			dur_meth = atoi(optarg);
			if (dur_meth > 3 || dur_meth < 1) {
				printf("Warning!  Duration method value is out "
				       "of range.  Using default.\n");
				dur_meth = 3;
			}
			break;
		    case (int)'h':
			printf("%s\n", help_msg);
			exit(0);
		};
	}
	if (!trace_file_name) trace_file_name = strdup(default_trace_file);
}

int
main(int argc, char* argv[]) {
	job_info_msg_t*   jobListPtr;
	slurm_job_info_t* job_ptr = NULL;
	int               rv, cnt, ix;
	struct tm*        localTime;

	procArgs(argc, argv);

	now       = time(NULL);
	localTime = localtime(&now);

	sprintf(outfname, "simqsnap.out.%04d%02d%02dT%02d%02d%02d",
			localTime->tm_year+1900, localTime->tm_mon+1,
			localTime->tm_mday, localTime->tm_hour,
			localTime->tm_min, localTime->tm_sec);

	printf("Output file name: (%s)\n", outfname);

	if (! (fp = fopen(outfname,"w"))) {
		printf("Can NOT open output file %s.\nAbort!\n", outfname);
		exit(-1);
	}

	if((trace_file = open(trace_file_name, O_CREAT | O_RDWR, S_IRUSR |
			S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
		printf("Error opening file %s\n", trace_file_name);
		return -1;
	}

	printf( "========================================================\n");
	printf( "========================================================\n\n");

	printf (    "Now: %s", asctime(localTime));
	fprintf(fp, "Now:              %ld\n", now);

	rv = slurm_load_jobs((time_t)NULL, &jobListPtr, SHOW_ALL);
	if (rv != SLURM_PROTOCOL_SUCCESS) {
		printf("Error!  Couldn not read Slurm jobs.\nAbort!");
		exit(-1);
	} else {
		cnt = jobListPtr->record_count;

		printf("Record count: %d\n", cnt);
		fprintf(fp, "Number of records %d\n", cnt);
	}

	job_ptr = jobListPtr->job_array;

	for( ix = 0; ix < cnt; ix++ ) {

		fprintf(fp,"JOB RECORD %d) %u\n", ix+1, job_ptr->job_id);
		recordRecord(job_ptr);
		if (recordJob(job_ptr) < 0) {
			printf("Warning! Job # %d did not get properly written "
			       "to %s\n", ix, trace_file_name);
		}

		++job_ptr;
	}

	free (trace_file_name);

	return 0;
}

void
recordRecord(slurm_job_info_t* j) {
	int ix;

	fprintf(fp, "account           %s\n",  j->account);
	fprintf(fp, "alloc_node        %s\n",  j->alloc_node);
	fprintf(fp, "alloc_sid         %u\n",  j->alloc_sid);
	fprintf(fp, "assoc_id          %u\n",  j->assoc_id);
	fprintf(fp, "batch_flag        %u\n",  j->batch_flag);
	fprintf(fp, "batch_host        %s\n",  j->batch_host);
	fprintf(fp, "batch_script      %s\n",  j->batch_script);
	fprintf(fp, "command           %s\n",  j->command);
	fprintf(fp, "comment           %s\n",  j->comment);
	fprintf(fp, "contiguous        %u\n",  j->contiguous);
	fprintf(fp, "cpus_per_task     %u\n",  j->cpus_per_task);
	fprintf(fp, "dependency        %s\n",  j->dependency);
	fprintf(fp, "derived_ec        %u\n",  j->derived_ec);
	fprintf(fp, "eligible_time     %ld\n", j->eligible_time);
	fprintf(fp, "end_time          %ld\n", j->end_time);
	fprintf(fp, "exc_nodes         %s\n",  j->exc_nodes);
	/*fprintf(fp, "exc_node_inx      %ld\n", j->exc_node_inx);*/
	fprintf(fp, "exit_code         %u\n",  j->exit_code);
	fprintf(fp, "features          %s\n",  j->features);
	fprintf(fp, "gres              %s\n",  j->gres);
	fprintf(fp, "group_id          %u\n",  j->group_id);
	fprintf(fp, "job_id            %u\n",  j->job_id);
	fprintf(fp, "job_state         %u\n",  j->job_state);
	fprintf(fp, "licenses          %s\n",  j->licenses);
	fprintf(fp, "max_cpus          %u\n",  j->max_cpus);
	fprintf(fp, "max_nodes         %u\n",  j->max_nodes);
	fprintf(fp, "boards_per_node   %u\n",  j->boards_per_node);
	fprintf(fp, "sockets_per_board %u\n",  j->sockets_per_board);
	fprintf(fp, "sockets_per_node  %u\n",  j->sockets_per_node);
	fprintf(fp, "cores_per_socket  %u\n",  j->cores_per_socket);
	fprintf(fp, "threads_per_core  %u\n",  j->threads_per_core);
	fprintf(fp, "name              %s\n",  j->name);
	fprintf(fp, "network           %s\n",  j->network);
	fprintf(fp, "nodes             %s\n",  j->nodes);
	fprintf(fp, "nice              %u\n",  j->nice);
	/*fprintf(fp, "node_inx          %ld\n", j->node_inx);*/
	fprintf(fp, "ntasks_per_core   %u\n",  j->ntasks_per_core);
	fprintf(fp, "ntasks_per_node   %u\n",  j->ntasks_per_node);
	fprintf(fp, "ntasks_per_socket %u\n",  j->ntasks_per_socket);
	fprintf(fp, "ntasks_per_board  %u\n",  j->ntasks_per_board);
	fprintf(fp, "num_nodes         %u\n",  j->num_nodes);
	fprintf(fp, "num_cpus          %u\n",  j->num_cpus);
	fprintf(fp, "partition         %s\n",  j->partition);
	fprintf(fp, "pn_min_memory     %u\n",  j->pn_min_memory);
	fprintf(fp, "pn_min_cpus       %u\n",  j->pn_min_cpus);
	fprintf(fp, "pn_min_tmp_disk   %u\n",  j->pn_min_tmp_disk);
	fprintf(fp, "pre_sus_time      %ld\n", j->pre_sus_time);
	fprintf(fp, "priority          %u\n",  j->priority);
	fprintf(fp, "qos               %s\n",  j->qos);
	fprintf(fp, "req_nodes         %s\n",  j->req_nodes);
	/*fprintf(fp, "req_node_inx      %ld\n", j->req_node_inx);*/
	fprintf(fp, "req_switch        %u\n",  j->req_switch);
	fprintf(fp, "requeue           %u\n",  j->requeue);
	fprintf(fp, "resize_time       %ld\n", j->resize_time);
	fprintf(fp, "restart_cnt       %u\n",  j->restart_cnt);
	fprintf(fp, "resv_name         %s\n",  j->resv_name);
	fprintf(fp, "shared            %u\n",  j->shared);
	fprintf(fp, "show_flags        %u\n",  j->show_flags);
	fprintf(fp, "start_time        %ld\n", j->start_time);
	fprintf(fp, "state_desc        %s\n",  j->state_desc);
	fprintf(fp, "state_reason      %u\n",  j->state_reason);
	fprintf(fp, "submit_time       %ld\n", j->submit_time);
	fprintf(fp, "suspend_time      %ld\n", j->suspend_time);
	fprintf(fp, "time_limit        %u\n",  j->time_limit);
	fprintf(fp, "time_min          %u\n",  j->time_min);
	fprintf(fp, "user_id           %u\n",  j->user_id);
	fprintf(fp, "preempt_time      %ld\n", j->preempt_time);
	fprintf(fp, "wait4switch       %u\n",  j->wait4switch);
	fprintf(fp, "wckey             %s\n",  j->wckey);
	fprintf(fp, "work_dir          %s\n",  j->work_dir);

	/* Print array fields. */
	ix = 0;
	while ( j->exc_node_inx[ix] != -1 ) {
		fprintf(fp, "exc_node_inx[...] %d\n",  j->exc_node_inx[ix]);
		++ix;
	}

	ix = 0;
	while ( j->node_inx[ix] != -1 ) {
		fprintf(fp, "node_inx[...]     %d\n",  j->node_inx[ix]);
		++ix;
	}

	ix = 0;
	while ( j->req_node_inx[ix] != -1 ) {
		fprintf(fp, "req_node_inx[...] %d\n",  j->req_node_inx[ix]);
		++ix;
	}

/*	recordJobResources(j->job_resrcs);*/
	fprintf(fp, "END RECORD          \n");
}

void
recordJobResources(job_resources_t* jrcs) {
	if(!jrcs) return;

	/*bitstr_t *      core_bitmap;*/
	/*bitstr_t *      core_bitmap_used;*/
	fprintf(fp, "JRcpu_array_cnt   %u\n",  jrcs->cpu_array_cnt);

	/*uint16_t *      cpu_array_value;*/
	/*uint32_t *      cpu_array_reps;*/
	/*uint16_t *      cpus;*/
	/*uint16_t *      cpus_used;*/
	/*uint16_t *      cores_per_socket;*/
	/*uint32_t *      memory_allocated;*/
	/*uint32_t *      memory_used;*/

	fprintf(fp, "JRnhosts          %u\n",  jrcs->nhosts);

	/*bitstr_t *      node_bitmap;*/

	fprintf(fp, "JRnode_req        %u\n",  jrcs->node_req);
	fprintf(fp, "JRnodes           %s\n",  jrcs->nodes);
	fprintf(fp, "JRncpus           %u\n",  jrcs->ncpus);

	/*uint32_t *      sock_core_rep_count;*/
	/*uint16_t *      sockets_per_node;*/
}

int
recordJob(slurm_job_info_t* j) {

	new_trace.job_id = j->job_id;
	new_trace.submit = j->submit_time;
	sprintf(new_trace.username, "%u", j->user_id);
	sprintf(new_trace.qosname, "%s", j->qos);

	/*
	 * The BSC Slurm Workload Simulator assigns random integers for the
	 * length of time that each job is to be run.  Since the purpose of
	 * this tool is to create the job simulation input file based upon a
	 * snapshot of some given Slurm system, we need to decide what value
	 * to place here.  Should it be:
	 * 	1) random time length
	 * 	2) the amount of time that the job had been running on the
	 *	   real system at snapshot time or expected time if pending.
	 *	3) Expected time (the time limit or wclimit) if not finished
	 *	   (note finished jobs don't typically linger long in the queue)
	 * 	4) something else
	 * In order to provide flexibility to the user, this is now a command
	 * line option with the default being #3 above, i.e. assign the expected
	 * run time (time_limit/wclimit) value.  The user may always modify this
	 * trace file with the edit_trace program if this is not what is desired.
	 */
	switch (dur_meth) {
	    case 1:
		new_trace.duration = rand() % j->time_limit;
		break;
	    case 2:
		if (j->job_state == JOB_RUNNING)
			new_trace.duration = now - j->start_time;
		else
			new_trace.duration = j->time_limit;
		break;
	    case 3:
		new_trace.duration = j->time_limit;
		break;
	};

	new_trace.tasks = j->num_cpus;

	new_trace.wclimit = j->time_limit;

	sprintf(new_trace.account, "%s", j->account);
	sprintf(new_trace.partition, "%s", j->partition);

	new_trace.cpus_per_task = j->cpus_per_task;
	new_trace.tasks_per_node = j->ntasks_per_node;

	written = write(trace_file, &new_trace, sizeof(new_trace));

	printf("JOB(%s): %d, %d\n", new_trace.username, new_trace.duration,
							new_trace.tasks);
	if(written != sizeof(new_trace)) {
			printf("Error writing to file: %d of %ld\n", written,
							sizeof(new_trace));
			return -1;
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
