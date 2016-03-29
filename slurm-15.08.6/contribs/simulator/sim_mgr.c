#ifdef SLURM_SIMULATOR

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/tcp.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/syscall.h> /* SYS_gettid */
#include <pwd.h>
#include <ctype.h>
#include <sim_trace.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <config.h>
#include <slurm/slurm_errno.h>
#include <src/common/forward.h>
#include <src/common/hostlist.h>
#include <src/common/node_select.h>
#include <src/common/parse_time.h>
#include <src/common/slurm_accounting_storage.h>
#include <src/common/slurm_jobcomp.h>
#include <src/common/slurm_protocol_pack.h>
#include <src/common/switch.h>
#include <src/common/xassert.h>
#include <src/common/xstring.h>
#include <src/common/assoc_mgr.h>
#include <src/common/slurm_sim.h>
#include <getopt.h>

#undef DEBUG
int sim_mgr_debug_level = 3;

#define SAFE_PRINT(s) (s) ? s : "<NULL>"

char  daemon1[1024];
char  daemon2[1024];
char  default_sim_daemons_path[] = "/sbin";
char* sim_daemons_path = NULL;

static int launch_daemons = 1;
char** envs;

long int sim_start_point; /* simulation time starting point */
long int sim_end_point;   /* simulation end point is an optional parameter */

job_trace_t *trace_head, *trace_tail;
rsv_trace_t *rsv_trace_head, *rsv_trace_tail;

char*  global_envp[100];  /* Uhmmm ... I do not like this limitted number of env values */
time_t time_incr = 1;     /* Amount to increment the simulated time by on each pass */
char*  workload_trace_file = NULL; /* Name of the file containing the workload to simulate */
char   default_trace_file[] = "test.trace";
extern char * libc_paths[3];
hostlist_t hostlist = NULL;
char   help_msg[] = "\
sim_mgr [endtime] [OPTIONS]\n\
	Valid OPTIONS are:\n\
      -c, --compath cpath      'cpath' is the path to the slurmctld and slurmd\n\
			       (applicable only if launching daemons).  \n\
			       Specification of this option supersedes any \n\
			       setting of SIM_DAEMONS_PATH.  If neither is \n\
			       specified then the sim_mgr looks in a sibling \n\
			       directory of where it resides called sbin.\n\
			       Finally, if still not found then the default\n\
			       is /sbin.\n\
      -n, --nofork             Do NOT fork the controller and daemons.  The\n\
			       user will be responsible for starting them\n\
			       separately.\n\
      -a, --accelerator secs   'secs' is the interval, in simulated seconds, \n\
			       to increment the simulated time after each \n\
			       cycle instead of merely one.\n\
      -w, --wrkldfile filename 'filename' is the name of the trace file \n\
			       containing the information of the jobs to \n\
			       simulate.\n\
      -s, --nodenames nodeexpr 'nodeexpr' is an expression representing all \n\
			       of the slurmd names to use when launching the \n\
			       daemons--should correspond exactly with what \n\
			       is defined in the slurm.conf.\n\
      -h, --help	       This help message.\n\
Notes:\n\
      'endtime' is specified as seconds since Unix epoch. If 0 is specified \n\
      then the simulator will run indefinitely.\n\n\
      The debug level can be increased by sending the SIGUSR1 signal to \n\
      the sim_mgr.\n\
		 $ kill -SIGUSR1 `pidof sim_mgr`\n\
      The debug level will be incremented by one with each signal sent, \n\
      wrapping back to zero after eight.";


/* Simulator synchronization variables */
char    syn_sem_name[]  = "serversem";
char    sig_sem_name[]  = "signalsem";
sem_t * mutexserver = SEM_FAILED;
sem_t * mutexsignal = SEM_FAILED;

int     failed_submissions = 0;

/* Function prototypes */
static int   generate_job(job_trace_t* jobd);
static void  dumping_shared_mem();
static void  fork_daemons();
static char* get_path_from_self(char*);
static char* get_path_from_env_var(char*);
static int   get_args(int, char**);
static int   count_env_vars(char** envp);
static uid_t userid_from_name(const char *name, gid_t* gid);
extern void free_simulation_users();

/*
 * Currently provides manual backdoor for increment the slurmd registered count.
 * Should probably replace with an option on simdate.
 */
static void
handlerSignal(int signo)
{
	*slurmd_registered += 1;
	info ("[%d] got a %d signal--registered slurmd: %d", getpid(),
				signo, *slurmd_registered);
}


static void
change_debug_level(int signum)
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;

	if (++sim_mgr_debug_level > 8) sim_mgr_debug_level = 0;

	log_opts.stderr_level  = (log_level_t)sim_mgr_debug_level;
	log_opts.logfile_level = (log_level_t)sim_mgr_debug_level;
	log_opts.syslog_level  = (log_level_t)sim_mgr_debug_level;

	log_alter(log_opts, (log_facility_t)LOG_DAEMON, "sim_mgr.log");

	info("%s--Debug level is now %d.", __FUNCTION__, sim_mgr_debug_level);
}

static void
terminate_simulation(int signum)
{
	info("Number of failed job submissions: %d\n", failed_submissions);
	dumping_shared_mem();

	sem_close(mutexserver);
	sem_unlink(syn_sem_name);
	sem_unlink(sig_sem_name);

	free_simulation_users();
	if (envs) xfree(envs);

	slurm_shutdown(0); /* 0-shutdown all daemons without a core file */

	if(signum == SIGINT)
		exit(0);

	return;
}

/* Debugging */
static void
dumping_shared_mem()
{
	struct timeval t1;

	info("Let's dump the shared memory contents");

	gettimeofday(&t1, NULL);
	info("Time: [%u][%ld][%ld]", current_sim[0], t1.tv_sec, t1.tv_usec);
	info("sim_mgr_pid       = %d",  *sim_mgr_pid);
	info("slurmctl_pid      = %d",  *slurmctl_pid);
	info("slurmd_count      = %d",  *slurmd_count);
	info("slurmd_registered = %d",  *slurmd_registered);
	info("global_sync_flag  = %d",  *global_sync_flag);
	info("slurmd_pid        = %d",  *slurmd_pid);

	return;
}

/* This is the main simulator function */
static void *
time_mgr(void *arg)
{
	pid_t child;
	struct timeval t1;

	info("INFO: Creating time_mgr thread");

	current_sim[0]   = sim_start_point;
	current_micro[0] = 0;

	info("Waiting for slurmd registrations, slurmd_registered: %d...",
				*slurmd_registered);
	while (*slurmd_registered < *slurmd_count ) {
		sleep(1);
		info("... slurmd_registered: %d", *slurmd_registered);
	}
	info("Done waiting.");

	gettimeofday(&t1, NULL);
	debug("[%u][%ld][%ld]", current_sim[0], t1.tv_sec, t1.tv_usec);


	/* Main simulation manager loop */
	while (1) {

		/* Do we have to end simulation in this cycle? */
		if (sim_end_point && sim_end_point <= current_sim[0]) {
			terminate_simulation(SIGINT);
		}

		gettimeofday(&t1, NULL);
		debug("[%u][%ld][%ld]", current_sim[0], t1.tv_sec, t1.tv_usec);

		/* Checking if a new reservation needs to be created */
		if (rsv_trace_head &&
			(current_sim[0] >= rsv_trace_head->creation_time) ) {

			int exec_result;

			info("Creation reservation for %s [%u - %ld]",
				rsv_trace_head->rsv_command, current_sim[0],
				rsv_trace_head->creation_time);

			child = fork();

			if (child == 0) { /* the child */
				char* const rsv_command_args[] = {
						rsv_trace_head->rsv_command,
						NULL
				};

				if(execve(rsv_trace_head->rsv_command,
						rsv_command_args,
						global_envp) < 0) {
					info("Error in execve for %s",
						rsv_trace_head->rsv_command);
					info("Exiting...");
					exit(-1);
				}
			}

			waitpid(child, &exec_result, 0);
			if (exec_result == 0)
				debug("reservation created");
			else
				debug("reservation failed");

			rsv_trace_head = rsv_trace_head->next;
		}

		/* Now checking if a new job needs to be submitted */
		while (trace_head) {
			/*
			 * Uhmm... This is necessary if a large number of jobs
			 * are submitted at the same time but it seems
			 * to have an impact on determinism
			 */

			debug("time_mgr: current %u and next trace %ld",
					*(current_sim), trace_head->submit);

			if (*(current_sim) >= trace_head->submit) {

				/*job_trace_t *temp_ptr;*/

				debug("[%d] time_mgr--current simulated time: "
				       "%u\n", __LINE__, *current_sim);

				if (generate_job (trace_head) < 0)
					++failed_submissions;

				/* Let's free trace record */
				/*temp_ptr = trace_head;*/
				trace_head = trace_head->next;
				/*if (temp_ptr) xfree(temp_ptr);*/
			} else {
				/*
				 * The job trace list is ordered in time so
				 * there is nothing else to do.
				 */
				break;
			}
		}

		/* Synchronization with daemons */
		sem_wait(mutexserver);
		*global_sync_flag = 2;
		sem_post(mutexserver);
		while (*global_sync_flag > 1) {
			usleep(100000);
		}
		/*
		 * Time accelerating added but currently unstable; for now, run
		 * with only 1 second intervals
		 */

		*(current_sim) = *(current_sim) + time_incr;
		debug("[%d] time_mgr--current simulated time: %u\n",
					__LINE__, *current_sim);
	}

	return 0;
}

static void
print_job_specs(job_desc_msg_t* dmesg)
{
	debug("\tdmesg->job_id: %d\n", dmesg->job_id);
	debug2("\t\tdmesg->time_limit: %d\n", dmesg->time_limit);
	debug2("\t\tdmesg->name: (%s)\n", dmesg->name);
	debug2("\t\tdmesg->user_id: %d\n", dmesg->user_id);
	debug2("\t\tdmesg->group_id: %d\n", dmesg->group_id);
	debug2("\t\tdmesg->work_dir: (%s)\n", dmesg->work_dir);
	debug2("\t\tdmesg->qos: (%s)\n", dmesg->qos);
	debug2("\t\tdmesg->partition: (%s)\n", dmesg->partition);
	debug2("\t\tdmesg->account: (%s)\n", dmesg->account);
	debug2("\t\tdmesg->reservation: (%s)\n", dmesg->reservation);
	debug2("\t\tdmesg->dependency: (%s)\n", dmesg->dependency);
	debug2("\t\tdmesg->num_tasks: %d\n", dmesg->num_tasks);
	debug2("\t\tdmesg->min_cpus: %d\n", dmesg->min_cpus);
	debug2("\t\tdmesg->cpus_per_task: %d\n", dmesg->cpus_per_task);
	debug2("\t\tdmesg->ntasks_per_node: %d\n", dmesg->ntasks_per_node);
	debug2("\t\tdmesg->duration: %d\n", dmesg->duration);
	debug2("\t\tdmesg->env_size: %d\n", dmesg->env_size);
	debug2("\t\tdmesg->environment[0]: (%s)\n", dmesg->environment[0]);
	debug2("\t\tdmesg->script: (%s)\n", dmesg->script);
}

static int
generate_job(job_trace_t* jobd)
{
	job_desc_msg_t dmesg;
	submit_response_msg_t * respMsg = NULL;
	int rv = 0;
	char *script, line[1024];
	uid_t uidt;
	gid_t gidt;

	script = xstrdup("#!/bin/bash\n");

	slurm_init_job_desc_msg(&dmesg);

	/* Set up and call Slurm C-API for actual job submission. */
	dmesg.time_limit    = jobd->wclimit;
	dmesg.job_id        = jobd->job_id;
	dmesg.name	    = xstrdup("sim_job");
	uidt = userid_from_name(jobd->username, &gidt);
	dmesg.user_id       = uidt;
	dmesg.group_id      = gidt;
	dmesg.work_dir      = xstrdup("/tmp"); /* hardcoded to /tmp for now */
	dmesg.qos           = xstrdup(jobd->qosname);
	dmesg.partition     = xstrdup(jobd->partition);
	dmesg.account       = xstrdup(jobd->account);
	dmesg.reservation   = xstrdup(jobd->reservation);
	dmesg.dependency    = xstrdup(jobd->dependency);
	dmesg.num_tasks     = jobd->tasks;
	dmesg.min_cpus      = jobd->tasks;
	dmesg.cpus_per_task = jobd->cpus_per_task;
	dmesg.ntasks_per_node = jobd->tasks_per_node;
	dmesg.duration      = jobd->duration;

	/* Need something for environment--Should make this more generic! */
	dmesg.environment  = (char**)xmalloc(sizeof(char*)*2);
	dmesg.environment[0] = xstrdup("HOME=/root");
	dmesg.env_size = 1;

	/* Standard dummy script. */
	sprintf(line,"#SBATCH -n %u\n", jobd->tasks);
	xstrcat(script, line);
	xstrcat(script, "\necho \"Generated BATCH Job\"\necho \"La Fine!\"\n");

	dmesg.script        = xstrdup(script);

	print_job_specs(&dmesg);

	if ( slurm_submit_batch_job(&dmesg, &respMsg) ) {
		slurm_perror ("slurm_submit_batch_job");
		rv = -1;
	}

	if (respMsg)
		info("Response from job submission\n\terror_code: %u"
				"\n\tjob_id: %u",
				respMsg->error_code, respMsg->job_id);

	/* Cleanup */
	if (respMsg) slurm_free_submit_response_response_msg(respMsg);
	if (script) xfree(script);
	if (dmesg.name)        xfree(dmesg.name);
	if (dmesg.work_dir)    xfree(dmesg.work_dir);
	if (dmesg.qos)         xfree(dmesg.qos);
	if (dmesg.partition)   xfree(dmesg.partition);
	if (dmesg.account)     xfree(dmesg.account);
	if (dmesg.reservation) xfree(dmesg.reservation);
	if (dmesg.dependency)  xfree(dmesg.dependency);
	if (dmesg.script)      xfree(dmesg.script);
	xfree(dmesg.environment[0]);
	xfree(dmesg.environment);

	return rv;
}

static uid_t
userid_from_name(const char *name, gid_t* gid)
{
	struct passwd *pwd;
	uid_t u;
	char *endptr;

	*gid = -1;			    /* In case of error, a -1     */
					    /* would be returned.         */

	if (name == NULL || *name == '\0')  /* On NULL or empty string    */
		return -1;                  /* return an error            */

	u = strtol(name, &endptr, 10);      /* As a convenience to caller */
	if (*endptr == '\0') {              /* allow a numeric string     */
		pwd = getpwuid(u);
		if (pwd == NULL)
			debug("Warning: Could not find the group id "
			       "corresponding to the user id: %u", u);
		else
			*gid = pwd->pw_gid;
		return u;
	}

	pwd = getpwnam(name);
	if (pwd == NULL)
		return -1;

	*gid = pwd->pw_gid;

	return pwd->pw_uid;
}


static int
insert_trace_record(job_trace_t *new)
{
	if (trace_head == NULL) {
		trace_head = new;
		trace_tail = new;
	} else {
		trace_tail->next = new;
		trace_tail = new;
	}
	return 0;
}

static int
insert_rsv_trace_record(rsv_trace_t *new)
{
	if (rsv_trace_head == NULL) {
		rsv_trace_head = new;
		rsv_trace_tail = new;
	} else {
		rsv_trace_tail->next = new;
		rsv_trace_tail = new;
	}
	return 0;
}

static int
init_trace_info(void *ptr)
{
	rsv_trace_t *new_rsv_trace_record;

	new_rsv_trace_record = calloc(1, sizeof(rsv_trace_t));
	if (new_rsv_trace_record == NULL) {
		error("%s--Error in calloc for new reservation", __FUNCTION__);
		return -1;
	}

	*new_rsv_trace_record = *(rsv_trace_t *)ptr;
	new_rsv_trace_record->next = NULL;

	info("Inserting new reservation trace record for time %ld",
				new_rsv_trace_record->creation_time);

	insert_rsv_trace_record(new_rsv_trace_record);

	return 0;
}

static int
init_job_trace()
{
	struct stat  stat_buf;
	int          nrecs = 0, idx = 0;
	job_trace_t* job_arr;
	int trace_file;

	trace_file = open(workload_trace_file, O_RDONLY);
	if (trace_file < 0) {
		error("Error opening file %s", workload_trace_file);
		return -1;
	}

	fstat(trace_file, &stat_buf);
	nrecs = stat_buf.st_size / sizeof(job_trace_t);
	info("Ci dev'essere %d job records to be read.", nrecs);

	job_arr = (job_trace_t*)malloc(sizeof(job_trace_t)*nrecs);
	if (!job_arr) {
		printf("Error.  Unable to allocate memory for all job records.\n");
		return -1;
	}

	while (read(trace_file, &job_arr[idx], sizeof(job_trace_t))) {
		if (idx == 0) {
			sim_start_point = job_arr[0].submit - 60;
			/*first_submit = new_trace_record->submit;*/
		}
		job_arr[idx].next = NULL;
		insert_trace_record(&job_arr[idx]);
		++idx;
	}

	info("Trace initialization done. Total trace records: %d", idx);

	close(trace_file);
	/*free (job_arr);*/

	return 0;
}

static int
init_rsv_trace()
{
	int trace_file;
	rsv_trace_t new_rsv;
	int total_trace_records = 0;
	int count;
	char new_char;
	char buff[20];

	trace_file = open("rsv.trace", O_RDONLY);
	if(trace_file < 0) {
		info("Warning: Can NOT open file rsv.trace");
		return -1;
	}

	new_rsv.rsv_command = xmalloc(100);
	if(new_rsv.rsv_command < 0) {
		info("Warning: Malloc problem with reservation creation");
		return -1;
	}

	memset(buff, '\0', 20);
	memset(new_rsv.rsv_command, '\0', 100);

	while(1) {
		count = 0;

		/* First reading the creation time value */
		while(read(trace_file, &new_char, sizeof(char))) {
			if(new_char == '=')
				break;

			buff[count++] = new_char;
		}

		if(count == 0)
			break;

		new_rsv.creation_time = (long int)atoi(buff);

		count = 0;

		/* then reading the script name to execute */
		while(read(trace_file, &new_char, sizeof(char))) {
			new_rsv.rsv_command[count++] = new_char;

			if(new_char == '\n') { 
				new_rsv.rsv_command[--count] = '\0';
				break;
			}
		}

		debug("Reading filename %s for execution at %ld",
				new_rsv.rsv_command, new_rsv.creation_time);

		init_trace_info(&new_rsv);
		total_trace_records++;
		if(total_trace_records > 10)
			break;
	}


	info("Trace initializarion done. Total trace records for "
			   "reservations: %d", total_trace_records);

	close(trace_file);

	return 0;
}

static int
open_global_sync_semaphore()
{
	mutexserver = sem_open(syn_sem_name, O_CREAT, 0755, 1);
	if(mutexserver == SEM_FAILED) {
		perror("unable to create server semaphore");
		sem_unlink(syn_sem_name);
		return -1;
	}

	return 0;
}

static int
open_slurmd_ready_semaphore()
{
	mutexsignal = sem_open(sig_sem_name, O_CREAT, 0755, 1);
	if(mutexsignal == SEM_FAILED) {
		perror("unable to create mutexsignal semaphore");
		sem_unlink(sig_sem_name);
		return -1;
	}

	return 0;
}


int main(int argc, char *argv[], char *envp[])
{

	pthread_attr_t attr;
	pthread_t id_mgr;   
	struct stat buf;
	int ix, envcount = count_env_vars(envp);

	log_options_t opts = LOG_OPTS_INITIALIZER;

	signal (SIGUSR2, handlerSignal);
	signal (SIGUSR1, change_debug_level);

	if ( !get_args(argc, argv) ) {
		info("Usage: %s", help_msg);
		exit(-1);
	}

	info("Reduced version of sim_mgr.");
	log_init(argv[0], opts, LOG_DAEMON, "sim_mgr.log");

	current_sim       = timemgr_data + SIM_SECONDS_OFFSET;
	current_micro     = timemgr_data + SIM_MICROSECONDS_OFFSET;
	sim_mgr_pid       = timemgr_data + SIM_SIM_MGR_PID_OFFSET;
	slurmctl_pid      = timemgr_data + SIM_SLURMCTLD_PID_OFFSET;
	slurmd_count      = timemgr_data + SIM_SLURMD_COUNT_OFFSET;
	slurmd_registered = timemgr_data + SIM_SLURMD_REGISTERED_OFFSET;
	global_sync_flag  = timemgr_data + SIM_GLOBAL_SYNC_FLAG_OFFSET;
	slurmd_pid        = timemgr_data + SIM_SLURMD_PID_OFFSET;

	memset(timemgr_data, 0, SIM_SHM_SEGMENT_SIZE);
	*sim_mgr_pid     = getpid();

	/* Determine location of the simulator library and Slurm daemons */

	if (!sim_daemons_path)
		sim_daemons_path = get_path_from_env_var("SIM_DAEMONS_PATH");

	if (!sim_daemons_path)  sim_daemons_path = get_path_from_self("sbin");

	if (!sim_daemons_path)
		sim_daemons_path = xstrdup(default_sim_daemons_path);

	sprintf(daemon1,"%s/slurmctld", sim_daemons_path);
	sprintf(daemon2,"%s/slurmd",    sim_daemons_path);

	if (stat(daemon1, &buf) == -1) {
		error("Can not stat the slurmctld command: (%s).\n"
		       "Aborting the sim_mgr.", daemon1);
		exit(-1);
	}
	if (stat(daemon2, &buf) == -1) {
		error("Can not stat the slurmd command: (%s).\n"
		       "Aborting the sim_mgr.", daemon2);
		exit(-1);
	}

	info("\n\tSettings:\n\t--------\n\tlaunch_daemons: %d\n\t"
		"sim_daemons_path: %s\n\tsim_end_point: %ld\n\tslurmctld: %s"
		"\n\tslurmd: %s\n\tenvironment\n\n",
		launch_daemons, SAFE_PRINT(sim_daemons_path),
		sim_end_point, daemon1, daemon2);

	/*Set up global environment list for later use in forking the daemons.*/
	envs = (char**)xmalloc(sizeof(char*)*(envcount+1));
	for(ix=0; ix<envcount; ix++) envs[ix] = envp[ix];
	envs[ix]   = NULL;

	ix = 0;
	while (envp[ix]){
		global_envp[ix] = envp[ix];
		++ix;
	}
	global_envp[ix] = NULL;


	if (open_global_sync_semaphore() < 0) {
		error("Error opening the global synchronization semaphore.");
		return -1;
	};

	if (open_slurmd_ready_semaphore() < 0) {
		error("Error opening the Simulator Slurmd-ready semaphore.");
		return -1;
	};

	/* Launch the slurmctld and slurmd here */
	if (launch_daemons) fork_daemons();

	if (init_job_trace() < 0) {
		error("A problem was detected when reading trace file.\n"
		       "Exiting...");
		return -1;
	}

	if (init_rsv_trace() < 0) {
		info("Warning: A problem was detected when reading reservation "
								"file.");
	}

	pthread_attr_init(&attr);
	signal(SIGINT,  terminate_simulation);
	signal(SIGHUP,  terminate_simulation);

	pthread_attr_init(&attr);

	/* This is a thread for flexibility */
	while (pthread_create(&id_mgr, &attr, &time_mgr, 0)) {
		error("Error with pthread_create for time_mgr");
		return -1;
	}

	pthread_join(id_mgr, NULL);
	sleep(1);

	if (sim_daemons_path)    xfree(sim_daemons_path);
	if (workload_trace_file) xfree(workload_trace_file);

	return 0;
}

static int
count_env_vars(char** envp)
{
	int rv = 0;
	while(envp[rv]) ++rv;
	return rv;
}

static void
fork_daemons()
{
	char* args[4], *name;
	pid_t pid;
	int status_ctld, status_slud, ix;

	debug("PROCESS: %d THREAD: %ld FILE: %s FUNCTION: %s LINE: %d--about to"
	       " fork", getpid(), syscall(SYS_gettid),
		__FILE__, __FUNCTION__, __LINE__);

	*slurmd_count = hostlist_count(hostlist);
	info("Number of slurmd hosts: %d", *slurmd_count);

	/* Launch the controller */
	pid = fork();
	if(pid==0) {
		args[0] = daemon1;
		args[1] = "-c";
		args[2] = NULL;
		execve(daemon1, args, envs);
		error("Reached here--something went wrong!");
	}
	waitpid(pid, &status_ctld, 0);

	/* Launch the slurmd's */
	if (*slurmd_count <= 0) {
		info("About to launch a single slurmd");
		pid = fork();
		if(pid==0) {
			args[0] = daemon2;
			args[1] = NULL;
			execve(daemon2, args, envs);
			error("Reached here--something went wrong!");
		}
		*slurmd_count = 1;
		waitpid(pid, &status_slud, 0);
	} else {

		for (ix=0; ix<*slurmd_count; ix++) {
			name = hostlist_shift(hostlist);
			info("About to launch slurmd #%d--(%s)", ix, name);
			pid = fork();
			if(pid==0) {
				args[0] = daemon2;
				args[1] = "-N";
				args[2] = name;
				args[3] = NULL;
				execve(daemon2, args, envs);
				error("Reached here--something went wrong!");
			}
			waitpid(pid, &status_slud, 0);
		}
	}

	hostlist_destroy(hostlist);
	hostlist = NULL;
}

/*
 * Tries to find a path based on the location of the sim_mgr executable.
 * If found, appends the "subpath" argument to it.
 */
static char *
get_path_from_self(char* subpath)
{
	char* path,* ptr,* newpath = NULL;

	path = getenv("_");
	if (path) {
		newpath = xstrdup(path);
		ptr = strrchr(newpath, '/');
		if (ptr) {
			*ptr = '\0';
			ptr = strrchr(newpath, '/');
			if (ptr) {
				*ptr = '\0';
			} else {
				sprintf(newpath,"..");
			}
			xstrcat(newpath,"/");
			xstrcat(newpath, subpath);
		}
	}

	return newpath;
}

static char *
get_path_from_env_var(char* env_var)
{
	char* path, *newpath = NULL;

	path = getenv(env_var);
	if(path) newpath = xstrdup(path);

	return newpath;
}

/*
 * Returns 1 if input is valid
 *         0 if input is not valid
 */
static int
get_args(int argc, char** argv)
{
	static struct option long_options[]  = {
		{"nofork",	0, 0, 'n'},
		{"compath",	1, 0, 'c'},
		{"accelerator",	1, 0, 'a'},
		{"wrkldfile",	1, 0, 'w'},
		{"nodenames",   1, 0, 's'},
		{"help",	0, 0, 'h'}
	};
	int ix = 0, valid = 1;
	int opt_char, option_index;
	char* ptr;

	while (1) {
		if ((opt_char = getopt_long(argc, argv, "nc:ha:w:s:",
				long_options, &option_index)) == -1 )
			break;

		switch(opt_char) {
			case ('n'):
				launch_daemons = 0;
				break;
			case ('c'):
				sim_daemons_path = xstrdup(optarg);
				break;
			case ('a'): /* Eventually use strtol instead of atoi */
				time_incr = atoi(optarg);
				break;
			case ('w'):
				workload_trace_file = xstrdup(optarg);
				break;
			case ('s'):
				hostlist = hostlist_create(optarg);
				break;
			case ('h'):
				printf("%s\n", help_msg);
				exit(0);
		};
	}

	if (optind < argc)
		for(ix = optind; ix<argc; ix++) {
			sim_end_point = strtol(argv[ix], &ptr, 10);

			/* If the argument is an empty string or ptr does not
			 * point to a Null character then the input is not
			 * valid.
			 */
			if ( !(argv[ix][0] != '\0' && *ptr == '\0') ) valid = 0;
		}

	if (!workload_trace_file)
		workload_trace_file = xstrdup(default_trace_file);

	return valid;
}
#else
#include <stdio.h>
int main(int argc, char* argv[])
{
	info("%s placeholder.  Only does something if built in simulator mode"
	       " (LIBS=-lrt CFLAGS=\"-D SLURM_SIMULATOR\").", argv[0]);
	return 0;
}
#endif
