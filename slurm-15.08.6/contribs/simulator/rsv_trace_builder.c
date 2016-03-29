#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>

#include "sim_trace.h"

int option_index;
static struct option long_options[] = {
    {"name",      1, 0, 'n'},
    {"account",  1, 0, 'a'},
    {"user", 1, 0, 'u'},
    {"partition",  1, 0, 'p'},
    {"starttime",     1, 0, 's'},
    {"duration",     1, 0, 'd'},
    {"nodecnt",     1, 0, 'N'},
    {"flags",     1, 0, 'f'},
    {NULL,       0, 0, 0}
};

int trace_file;
job_trace_t new_trace;

char *user;
char *account;
char *rsv_name;
char *partition;
char *starttime;
char *duration;
char *flags;
char *nodecount;

char scontrol_command[1000];

int main(int argc, char *argv[]){

		int i;
		int written;
		int  userfile;
		int  qosfile;
		int char_pos;
		int endfile = 0;
		char *name;
        int opt_char;
        int option_index;

        while((opt_char = getopt_long(argc, argv, "naupsdNf",
                        long_options, &option_index)) != -1) {
            switch (opt_char) {
                case (int)'n':
                    rsv_name = strdup(optarg);
                    /*printf("Using name %s for the reservation\n", rsv_name);*/
                    break;

                case (int)'a':
                    account = strdup(optarg);
                    /*printf("Using account %s for the reservation\n", account);*/
                    break;

                case (int)'u':
                    user = strdup(optarg);
                    break;

                case (int)'p':
                    partition = strdup(optarg);
                    break;

                case (int)'s':
                    starttime = strdup(optarg);
                    break;

                case (int)'d':
                    duration = strdup(optarg);
                    break;

                case (int)'f':
                    flags = strdup(optarg);
                    break;

                case (int)'N':
                    nodecount = strdup(optarg);
                    break;

                default:
                    fprintf(stderr, "getopt error, returned %c\n",
                            opt_char);
                    exit(0);
            }
        }

        sprintf(scontrol_command, "scontrol create reservation=%s users=%s accounts=%s partitionname=%s starttime=%s duration=%s flags=%s nodecnt=%s\n", 
                rsv_name, user, account, partition, starttime, duration, flags, nodecount);

        if((trace_file = open("test.trace", O_CREAT | O_APPEND | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0){
            printf("Error opening file test.trace\n");
            return -1;
		}

        write(trace_file, scontrol_command, strlen(scontrol_command));

        close(trace_file);
        

		return 0;
}
