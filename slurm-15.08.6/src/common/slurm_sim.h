#ifndef _SLURM_SIM_H
#define _SLURM_SIM_H

#ifdef SLURM_SIMULATOR

#define SLURM_SIM_SHM "/tester_slurm_sim.shm"
#define SIM_SHM_SEGMENT_SIZE         64

/* Offsets */
#define SIM_SECONDS_OFFSET            0
#define SIM_MICROSECONDS_OFFSET       8
#define SIM_SIM_MGR_PID_OFFSET       16
#define SIM_SLURMCTLD_PID_OFFSET     24
#define SIM_SLURMD_COUNT_OFFSET      32
#define SIM_SLURMD_REGISTERED_OFFSET 40
#define SIM_GLOBAL_SYNC_FLAG_OFFSET  48
#define SIM_SLURMD_PID_OFFSET        56

#include "slurm/slurm.h"

void         * timemgr_data;
uint32_t     * current_sim;
uint32_t     * current_micro;
pid_t        * sim_mgr_pid;
pid_t        * slurmctl_pid;
int          * slurmd_count;
int          * slurmd_registered;
int          * global_sync_flag;
pid_t        * slurmd_pid;

extern char    syn_sem_name[];
extern sem_t * mutexserver;

extern char    sig_sem_name[];
extern sem_t * mutexsignal;
#endif
#endif
