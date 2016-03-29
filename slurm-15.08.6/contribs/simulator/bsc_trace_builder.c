#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include "sim_trace.h"

#define MAX_WALLCLOCK	7200
#define MIN_WALLCLOCK	60

#define MAX_USERS 1000
/* this program creates a file with a set of job traces for slurm simulator */

static struct option long_options[] = {
    {"cpus",      1, 0, 'c'},
    {"jobs",  1, 0, 'j'},
    {"partition",  1, 0, 'p'},
    {"account",     1, 0, 'a'},
    {"cpus_per_task",     1, 0, 'm'},
    {"tasks_per_node",     1, 0, 't'},
    {"submit_time",     1, 0, 's'},
    {NULL,       0, 0, 0}
};


/* job_id, user, partition, account, class, submit_time, duration, wclimit, tasks, tasks_per_node, cpus_per_task */

int trace_file;
int job_counter = 1001;
int total_cpus = 0;
int total_jobs = 0;
int submit_time = 0;
int cpus_per_task = 0;
int tasks_per_node = 0;

job_trace_t new_trace;

char *username[1000];
int total_users = 0;

char *qosname[1000];
int total_qos = 0;

char *default_partition;
char *default_account;

int get_duration(){

		unsigned int value;
		int frand;

		frand = open("/dev/urandom", O_RDONLY);
		if(frand < 0){
				printf("Error opening /dev/urandom\n");
				return -1;
		}

		read(frand, &value, 4);

		close(frand);

		value = value % 10;

		switch (value) {

				case 0:
				case 1:
						return 30;
				case 2:
				case 3: 
						return 60;

				case 4:
				case 5:
						return 300;

				case 6:
				case 7:
						return 600;

				case 8:
						return 1800;
				case 9:
						return 3600;

				default:

						return 60;
		}

		return 0;

}

int get_tasks(){

		unsigned int value;
		int frand;
		static int last_big;

		frand = open("/dev/urandom", O_RDONLY);
		if(frand < 0){
				printf("Error opening /dev/urandom\n");
				return -1;
		}

		read(frand, &value, 4);

		close(frand);

		value = value % 20;

		switch (value) {

				case 0:
				case 1:
				case 2:
				case 3: 
						return 4;

				case 4:
				case 5:
				case 6:
				case 7:
						return 8;
				case 8:
						return 16;

				case 9:
						return 32;
				case 10:
						return 40;
		}

		if(total_cpus > 40){

				switch (value) {
						case 11:
								if(total_cpus < 64)
										return total_cpus;
								else
										return 64;
						case 12:
								if(total_cpus < 80)
										return total_cpus;
								else
										return 80;
						case 13:
								if(total_cpus < 128)
										return total_cpus;
								else
										return 128;
						case 14:
								if(total_cpus < 256)
										return total_cpus;
								else
										return 256;
						case 15:
								if(total_cpus < 512)
										return total_cpus;
								else
										return 512;
						case 16:
								if(total_cpus < 1024)
										return total_cpus;
								else
										return 1024;
						case 17:
								if(last_big == 0){

										last_big++;

										if(total_cpus < 2048)
												return total_cpus;
										else
												return 2048;
								}
								if(last_big == 1){

										last_big++;

										if(total_cpus < 4096)
												return total_cpus;
										else
												return 4096;
								}
								if(last_big == 2){

										last_big = 0;
										if(total_cpus < 8192)
												return total_cpus;
										else
												return 8192;
								}
						default:
								return 4;
				}
		
		}
		return 0;
}


int main(int argc, char *argv[]){

		int i;
		int written;
		int  userfile;
		int  qosfile;
		int char_pos;
		int endfile = 0;
		char *name;
        int option_index;
        int opt_char;

        while((opt_char = getopt_long(argc, argv, "cjpamt",
                        long_options, &option_index)) != -1) {
            switch (opt_char) {
                case (int)'c':
                    total_cpus = atoi(optarg);

                case (int)'j':
                    total_jobs = atoi(optarg);
                    break;

                case (int)'p':
                    default_partition = strdup(optarg);
                    break;

                case (int)'a':
                    default_account = strdup(optarg);
                    break;

                case (int)'m':
                    cpus_per_task = atoi(optarg);
                    break;

                case (int)'t':
                    tasks_per_node = atoi(optarg);
                    break;

                case (int)'s':
                    submit_time = atoi(optarg);
                    break;

                default:
                    fprintf(stderr, "getopt error, returned %c\n",
                            opt_char);
                    exit(0);
            }
        }

        if(total_cpus == 0){
            printf("Usage: %s --cpus=xx --jobs=xx --partition=xxxx --account=xxxx --cpus_per_task=xx --tasks_per_node=xx --submit_time=xx(unixtime)\n", argv[0]);
            return -1;
        }
		if(total_jobs == 0){
            printf("Usage: %s --cpus=xx --jobs=xx --partition=xxxx --account=xxxx --cpus_per_task=xx --tasks_per_node=xx --submit_time=xx(unixtime)\n", argv[0]);
            return -1;
        }
		if(default_partition == NULL){
            printf("Usage: %s --cpus=xx --jobs=xx --partition=xxxx --account=xxxx --cpus_per_task=xx --tasks_per_node=xx --submit_time=xx(unixtime)\n", argv[0]);
            return -1;
        }
		if(default_account == NULL){
            printf("Usage: %s --cpus=xx --jobs=xx --partition=xxxx --account=xxxx --cpus_per_task=xx --tasks_per_node=xx --submit_time=xx(unixtime)\n", argv[0]);
            return -1;
        }

        if(cpus_per_task == 0){
            printf("Usage: %s --cpus=xx --jobs=xx --partition=xxxx --account=xxxx --cpus_per_task=xx --tasks_per_node=xx --submit_time=xx(unixtime)\n", argv[0]);
            return -1;
        }

        if(tasks_per_node == 0){
            printf("Usage: %s --cpus=xx --jobs=xx --partition=xxxx --account=xxxx --cpus_per_task=xx --tasks_per_node=xx --submit_time=xx(unixtime)\n", argv[0]);
            return -1;
        }

        if(submit_time == 0){
            printf("Usage: %s --cpus=xx --jobs=xx --partition=xxxx --account=xxxx --cpus_per_task=xx --tasks_per_node=xx --submit_time=xx(unixtime)\n", argv[0]);
            return -1;
        }

		if(total_cpus < 40){
				printf("Setting total_cpus to the minimum value: 40\n");
				total_cpus = 40;
		}

		userfile = open("users.sim", O_RDONLY);

		if(userfile < 0){
			printf("users.sim file not found\n");
			return -1;
		}

		while(1){
		  	char_pos = 0;
			name = malloc(30);
			while(1){
				if(read(userfile, &name[char_pos], 1) <= 0){
					endfile = 1;
					break;
				};
				//printf("Reading char: %c\n", username[char_pos][total_users]);
				if(name[char_pos] == '\n'){
					name[char_pos] = '\0';
					break;
				}
				char_pos++;
			}
			if(endfile)
				break;
			username[total_users] = name;
			printf("Reading user: %s\n", username[total_users]);
			total_users++;
		}

#if 0
		qosfile = open("qos.sim", O_RDONLY);

		if(qosfile < 0){
			printf("qos.sim file not found\n");
			return -1;
		}

		endfile = 0;
		while(1){
		  	char_pos = 0;
			name = malloc(30);
			while(1){
				if(read(qosfile, &name[char_pos], 1) <= 0){
					endfile = 1;
					break;
				};
				//printf("Reading char: %c\n", username[char_pos][total_users]);
				if(name[char_pos] == '\n'){
					name[char_pos] = '\0';
					break;
				}
				char_pos++;
			}
			if(endfile)
				break;
			qosname[total_qos] = name;
			printf("Reading qos: %s\n", qosname[total_qos]);
			total_qos++;
		}
#endif

		if((trace_file = open("test.trace", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0){
				printf("Error opening file test.trace\n");
				return -1;
		}

    /*    submit_time = 1316242565;*/

		for(i = 0; i < total_jobs; i++){

				int j;

				new_trace.job_id = job_counter++;

				/* Submitting a job every 30s */
				new_trace.submit = submit_time;
				submit_time += 30;

				sprintf(new_trace.username, username[i % total_users]);

				/*sprintf(new_trace.qosname, qosname[i % total_qos]); st on 14-aprile-2015 */
				sprintf(new_trace.qosname, "normal");

				new_trace.duration = get_duration();
				if(new_trace.duration < 0){
						printf("frand did not work. Exiting...\n");
						return -1;
				}

				new_trace.tasks = get_tasks();
				if(new_trace.tasks < 0){
						printf("frand did not work. Exiting...\n");
						return -1;
				}

				new_trace.wclimit = new_trace.duration + (new_trace.duration *0.30);


				if(new_trace.wclimit < 60)
					new_trace.wclimit = 60;

				sprintf(new_trace.account, "%s", default_account);
				sprintf(new_trace.partition, "%s", default_partition);

				new_trace.cpus_per_task = 1;
				new_trace.tasks_per_node = 4;

				written = write(trace_file, &new_trace, sizeof(new_trace));

				printf("JOB(%s): %d, %d, %d\n", new_trace.username, job_counter - 1, new_trace.duration, new_trace.tasks);
				if(written != sizeof(new_trace)){
						printf("Error writing to file: %d of %ld\n", written, sizeof(new_trace));
						return -1;
				}

		}

		return 0;
}
