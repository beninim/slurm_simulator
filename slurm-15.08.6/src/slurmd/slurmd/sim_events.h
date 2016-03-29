#ifdef SLURM_SIMULATOR
/*
 ** Definitions for simulation mode
 ** */

typedef struct simulator_event {
    uint32_t job_id;
    int type;
    time_t when;
    char *nodelist;
    volatile struct simulator_event *next;
} simulator_event_t;

typedef struct simulator_event_info {
    uint32_t job_id;
    uint32_t duration;
    struct simulator_event_info *next;
} simulator_event_info_t;
#endif
