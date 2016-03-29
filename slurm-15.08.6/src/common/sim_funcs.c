#ifdef SLURM_SIMULATOR

#include <errno.h>
#include <sys/stat.h>
#include "src/common/sim_funcs.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
extern errno;

/* Structures, macros and other definitions */
#undef DEBUG
//#define DEBUG


typedef struct sim_user_info{
    uid_t sim_uid;
    char *sim_name;
    struct sim_user_info *next;
}sim_user_info_t;

/* Function Pointers */
int (*real_gettimeofday)(struct timeval *,struct timezone *) = NULL;
time_t (*real_time)(time_t *)                                = NULL;

/* Global Variables */
sim_user_info_t * sim_users_list;
int             * slurmctl_pid;     /* Shared Memory */
int             * slurmd_count;     /* Shared Memory */
int             * slurmd_registered;/* Shared Memory */
int             * global_sync_flag; /* Shared Memory */
int             * slurmd_pid;       /* Shared Memory */
char            * users_sim_path = NULL;
char            * lib_loc;
char            * libc_paths[3] = {"/lib/x86_64-linux-gnu/libc.so.6",
				   "/lib/libc.so.6",
				   NULL};

extern void     * timemgr_data;     /* Shared Memory */
extern uint32_t * current_sim;      /* Shared Memory */
extern uint32_t * current_micro;    /* Shared Memory */
extern char     * default_slurm_config_file;

/* Function Prototypes */
static void init_funcs();
void init_shared_memory_if_needed();
int getting_simulation_users();

time_t time(time_t *t)
{
	init_shared_memory_if_needed();
	/* If the current_sim pointer is NULL that means that there is no
	 * shared memory segment, at least not yet, therefore use real function
	 * for now.
	 * Note, here we are examing the location of to where the pointer points
	 *       and not the value itself.
	 */
	if (!(current_sim) && !real_time) init_funcs();
	if (!(current_sim)) {
		return real_time(t);
	} else {
		if(t) {
			*t = *(current_sim);}
		return *(current_sim);
	}
}

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	init_shared_memory_if_needed();

	if (!(current_sim) && !real_gettimeofday) init_funcs();
	if (!(current_sim)) {
		return real_gettimeofday(tv, tz);
	} else {
		tv->tv_sec       = *(current_sim);
		*(current_micro) = *(current_micro) + 100;
		tv->tv_usec      = *(current_micro);
	}

	return 0;
}

static int build_shared_memory()
{
	int fd;

	fd = shm_open(SLURM_SIM_SHM, O_CREAT | O_RDWR,
				S_IRWXU | S_IRWXG | S_IRWXO);
	if (fd < 0) {
		error("Error opening %s", SLURM_SIM_SHM);
		return -1;
	}

	if (ftruncate(fd, SIM_SHM_SEGMENT_SIZE)) {
		info("Warning!  Can not truncate shared memory segment.");
	}

	timemgr_data = mmap(0, SIM_SHM_SEGMENT_SIZE, PROT_READ | PROT_WRITE,
							MAP_SHARED, fd, 0);

	if(!timemgr_data){
		debug("mmaping %s file can not be done\n", SLURM_SIM_SHM);
		return -1;
	}

	return 0;

}

/*
 * Slurmctld and slurmd do not really build shared memory but they use that
 * one built by sim_mgr
 */
extern int attaching_shared_memory()
{
	int fd;

	fd = shm_open(SLURM_SIM_SHM, O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO );
	if (fd >= 0) {
		if (ftruncate(fd, SIM_SHM_SEGMENT_SIZE)) {
			info("Warning! Can't truncate shared memory segment.");
		}
		timemgr_data = mmap(0, SIM_SHM_SEGMENT_SIZE,
				    PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	} else {
		build_shared_memory();
	}

	if (!timemgr_data) {
		error("mmaping %s file can not be done", SLURM_SIM_SHM);
		return -1;
	}

	/* Initializing pointers to shared memory */
	current_sim       = timemgr_data + SIM_SECONDS_OFFSET;
	current_micro     = timemgr_data + SIM_MICROSECONDS_OFFSET;
	sim_mgr_pid       = timemgr_data + SIM_SIM_MGR_PID_OFFSET;
	slurmctl_pid      = timemgr_data + SIM_SLURMCTLD_PID_OFFSET;
	slurmd_count      = timemgr_data + SIM_SLURMD_COUNT_OFFSET;
	slurmd_registered = timemgr_data + SIM_SLURMD_REGISTERED_OFFSET;
	global_sync_flag  = timemgr_data + SIM_GLOBAL_SYNC_FLAG_OFFSET;
	slurmd_pid        = timemgr_data + SIM_SLURMD_PID_OFFSET;

	return 0;
}

static void
determine_libc() {
	struct stat buf;
	int ix;
	char found = 0;

	libc_paths[2] = getenv("SIM_LIBC_PATH");

	for (ix=2; ix>=0 && !found; --ix) {
		lib_loc = libc_paths[ix];
		if (lib_loc) {
			if (!stat(lib_loc, &buf)) ++found;
		}
	}

	if (!found) {
		error("Could not find the libc file."
		      "  Try setting SIM_LIBC_PATH.");
	}
}

static void init_funcs()
{
	void* handle;

	if (real_gettimeofday == NULL) {
		debug("Looking for real gettimeofday function");

		handle = dlopen(lib_loc, RTLD_LOCAL | RTLD_LAZY);
		if (handle == NULL) {
			debug("Error in dlopen %s", dlerror());
			return;
		}

		real_gettimeofday = dlsym( handle, "gettimeofday");
		if (real_gettimeofday == NULL) {
			error("Error: no sleep function found");
			return;
		}
	}

	if (real_time == NULL) {
		debug("Looking for real time function");

		handle = dlopen(lib_loc, RTLD_LOCAL | RTLD_LAZY);
		if (handle == NULL) {
			error("Error in dlopen: %s", dlerror());
			return;
		}
		real_time = dlsym( handle, "time");
		if (real_time == NULL) {
			error("Error: no sleep function found");
			return;
		}
	}
}

void init_shared_memory_if_needed()
{
	if (!(current_sim)) {
		if (attaching_shared_memory() < 0) {
			error("Error attaching/building shared memory "
			      "and mmaping it");
		};
	}
}

/* User- and uid-related functions */
uid_t sim_getuid(const char *name)
{
	sim_user_info_t *aux;

	if (!sim_users_list) getting_simulation_users();

	aux = sim_users_list;
	debug2("sim_getuid: starting search for username %s", name);

	while (aux) {
		if (strcmp(aux->sim_name, name) == 0) {
			debug2("sim_getuid: found uid %u for username %s",
						aux->sim_uid, aux->sim_name);
			debug2("sim_getuid--name: %s uid: %u",
						name, aux->sim_uid);
			return aux->sim_uid;
		}
		aux = aux->next;
	}

	debug2("sim_getuid--name: %s uid: <Can NOT find uid>", name);
	return -1;
}

char *sim_getname(uid_t uid)
{
	sim_user_info_t *aux;
	char *user_name;

	aux = sim_users_list;

	while (aux) {
		if (aux->sim_uid == uid) {
			user_name = xstrdup(aux->sim_name);
			return user_name;
		}
		aux = aux->next;
	}

	return NULL;
}

int getpwnam_r(const char *name, struct passwd *pwd, 
		char *buf, size_t buflen, struct passwd **result)
{

	pwd->pw_uid = sim_getuid(name);
	if (pwd->pw_uid == -1) {
		*result = NULL;
		debug("No user found for name %s", name);
		return ENOENT;
	}
	pwd->pw_name = xstrdup(name);
	debug("Found uid %u for name %s", pwd->pw_uid, pwd->pw_name);

	*result = pwd;

	return 0;
}

int getpwuid_r(uid_t uid, struct passwd *pwd,
		char *buf, size_t buflen, struct passwd **result)
{

	pwd->pw_name = sim_getname(uid);

	if (pwd->pw_name == NULL) {
		*result = NULL;
		debug("No user found for uid %u", uid);
		return ENOENT;
	}
	pwd->pw_uid = uid;
	pwd->pw_gid = 100;  /* users group. Is this portable? */
	debug("Found name %s for uid %u", pwd->pw_name, pwd->pw_uid);

	*result = pwd;

	return 0;

}

void determine_users_sim_path()
{
	char *ptr = NULL;

	if (!users_sim_path) {
		char *name = getenv("SLURM_CONF");
		if (name) {
			users_sim_path = xstrdup(name);
		} else {
			users_sim_path = xstrdup(default_slurm_config_file);
		}

		ptr = strrchr(users_sim_path, '/');
		if (ptr) {
			/* Found a path, truncate the file name */
			++ptr;
			*ptr = '\0';
		} else {
			xfree(users_sim_path);
			users_sim_path = xstrdup("./");
		}
	}
}

int getting_simulation_users()
{
	char              username[100], users_sim_file_name[128];
	char              uid_string[10];
	sim_user_info_t * new_sim_user;
	/*uid_t             sim_uid;*/
	int               fich, pos, rv = 0;
	char              c;

	if (sim_users_list)
		return 0;

	determine_users_sim_path();
	sprintf(users_sim_file_name, "%s%s", users_sim_path, "users.sim");
	fich = open(users_sim_file_name, O_RDONLY);
	if (fich < 0) {
		info("ERROR: no users.sim available");
		return -1;
	}

	debug("Starting reading users...");

	while (1) {

		memset(&username, '\0', 100);
		pos = 0;

		while (read(fich, &c, 1) > 0) {
			username[pos] = c;
			if (username[pos] == ':') {
				username[pos] = '\0';
				break;
			}
			pos++;
		}

		if (pos == 0)
			break;

		new_sim_user = xmalloc(sizeof(sim_user_info_t));
		if (new_sim_user == NULL) {
			error("Malloc error for new sim user");
			rv = -1;
			goto finish;
		}
		debug("Reading user %s", username);
		new_sim_user->sim_name = xstrdup(username);

		pos = 0;
		memset(&uid_string, '\0', 10);

		while (read(fich, &c, 1) > 0) {
			uid_string[pos] = c;
			if (uid_string[pos] == '\n') {
				uid_string[pos] = '\0';
				break;
			}
			pos++;
		}
		debug("Reading uid %s", uid_string);

		new_sim_user->sim_uid = (uid_t)atoi(uid_string);

		/* Inserting as list head */
		new_sim_user->next = sim_users_list;
		sim_users_list = new_sim_user;
	}

finish:
	if (fich) close(fich);
	return rv;
}

void
free_simulation_users()
{
	sim_user_info_t * sim_user = sim_users_list;

	while (sim_user) {
		sim_users_list = sim_user->next;
	
		/* deleting the list head */
		xfree(sim_user->sim_name);
		xfree(sim_user);

		sim_user = sim_users_list;
	}
	sim_users_list = NULL;
}

/*
 * "Constructor" function to be called before the main of each Slurm
 * entity (e.g. slurmctld, slurmd and commands).
 */

void __attribute__ ((constructor)) sim_init(void)
{
	void *handle;
#ifdef DEBUG
	sim_user_info_t *debug_list;
#endif
	determine_libc();

	if (attaching_shared_memory() < 0) {
		error("Error attaching/building shared memory and mmaping it");
	};


	if (getting_simulation_users() < 0) {
		error("Error getting users information for simulation");
	}

#ifdef DEBUG
	debug_list = sim_users_list;
	while (debug_list) {
		info("User %s with uid %u", debug_list->sim_name,
					debug_list->sim_uid);
		debug_list = debug_list->next;
	}
#endif

	if (real_gettimeofday == NULL) {
		debug("Looking for real gettimeofday function");

		handle = dlopen(lib_loc, RTLD_LOCAL | RTLD_LAZY);
		if (handle == NULL) {
			error("Error in dlopen %s", dlerror());
			return;
		}

		real_gettimeofday = dlsym( handle, "gettimeofday");
		if (real_gettimeofday == NULL) {
			error("Error: no sleep function found");
			return;
		}
	}

	if (real_time == NULL) {
		debug("Looking for real time function");

		handle = dlopen(lib_loc, RTLD_LOCAL | RTLD_LAZY);
		if (handle == NULL) {
			error("Error in dlopen: %s", dlerror());
			return;
		}
		real_time = dlsym( handle, "time");
		if (real_time == NULL) {
			error("Error: no sleep function found\n");
			return;
		}
	}

	debug("sim_init: done");
}

int
sim_open_sem(char * sem_name, sem_t ** mutex_sync, int max_attempts)
{
	int iter = 0, max_iter = max_attempts;
	if (!max_iter) max_iter = 10;
	while ((*mutex_sync) == SEM_FAILED && iter < max_iter) {
		(*mutex_sync) = sem_open(sem_name, 0, 0755, 0);
		if ((*mutex_sync) == SEM_FAILED && max_iter > 1) {
			int err = errno;
			info("ERROR! Could not open semaphore (%s)-- %s",
					sem_name, strerror(err));
			sleep(1);
		}
		++iter;
	}

	if ((*mutex_sync) == SEM_FAILED)
		return -1;
	else
		return 0;
}

void
sim_perform_global_sync(char * sem_name, sem_t ** mutex_sync)
{
	static uint32_t oldtime = 0;

	while (*global_sync_flag < 2 || *current_sim < oldtime + 1) {
		usleep(100000); /* one-tenth second */
	}

	if (*mutex_sync != SEM_FAILED) {
		sem_wait(*mutex_sync);
	} else {
		while ( *mutex_sync == SEM_FAILED ) {
			sim_open_sem(sem_name, mutex_sync, 0);
		}
		sem_wait(*mutex_sync);
	}

	*global_sync_flag += 1;
	if (*global_sync_flag > *slurmd_count + 1)
		*global_sync_flag = 1;
	oldtime = *current_sim;
	sem_post(*mutex_sync);
}

void
sim_perform_slurmd_register(char * sem_name, sem_t ** mutex_sync)
{
	if (*mutex_sync != SEM_FAILED) {
		sem_wait(*mutex_sync);
	} else {
		while ( *mutex_sync == SEM_FAILED ) {
			sim_open_sem(sem_name, mutex_sync, 0);
		}
		sem_wait(*mutex_sync);
	}

	*slurmd_registered += 1;
	sem_post(*mutex_sync);
}

void
sim_close_sem(sem_t ** mutex_sync)
{
	if ((*mutex_sync) != SEM_FAILED) {
		sem_close((*mutex_sync));
	}
}
#endif
