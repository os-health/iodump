/*
 *
 *  Copyright (c) 2022, Alibaba Group;
 *  Licensed under the MIT License (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *       https://mit-license.org/
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/magic.h>
#include <sys/mman.h>
#include <sys/vfs.h> 
#include <sys/epoll.h>
#include <sys/file.h>
#include "iodump.h"

#define LOW_BUFLEN         4096
#define HIGH_BUFLEN        262144
#define LAST_TIME          300

#define PARAM_INCORRECT    150
#define INVALID_CPU_TOTAL  151
#define IODUMP_RUNNING     152
#define ABNORMAL_CPU_LARGE 153
#define MAX_EVENTS         256
#define MAX_ABNORMAL_CPU     3

#define BLK_RQ             10001
#define BLK_BIO            10002

#define ARRAY_NR(array) (sizeof((array))/sizeof((array)[0]))

enum opts_flag_bits{
    __OPTS_DATETIME,
    __OPTS_TIMESTAMP,
    __OPTS_COMM,
    __OPTS_PID,
    __OPTS_TID,
    __OPTS_IOSIZE,
    __OPTS_SECTOR,
    __OPTS_PARTITION,
    __OPTS_RWPRI,
    __OPTS_RWSEC,
    __OPTS_LAUNCHER,
    __OPTS_INO,
    __OPTS_FULLPATH,
};

#define OPTS_DATETIME      (1UL << __OPTS_DATETIME)
#define OPTS_TIMESTAMP     (1UL << __OPTS_TIMESTAMP)
#define OPTS_COMM          (1UL << __OPTS_COMM)
#define OPTS_PID           (1UL << __OPTS_PID)
#define OPTS_TID           (1UL << __OPTS_TID)
#define OPTS_IOSIZE        (1UL << __OPTS_IOSIZE)
#define OPTS_SECTOR        (1UL << __OPTS_SECTOR)
#define OPTS_PARTITION     (1UL << __OPTS_PARTITION)
#define OPTS_RWPRI         (1UL << __OPTS_RWPRI)
#define OPTS_RWSEC         (1UL << __OPTS_RWSEC)
#define OPTS_LAUNCHER      (1UL << __OPTS_LAUNCHER)
#define OPTS_INO           (1UL << __OPTS_INO)
#define OPTS_FULLPATH      (1UL << __OPTS_FULLPATH)

struct output_option output_options[] = {
    {OPTS_DATETIME,  "datetime"},
    {OPTS_TIMESTAMP, "timestamp"},
    {OPTS_COMM,      "comm"},
    {OPTS_PID,       "pid"},
    {OPTS_TID,       "tid"},
    {OPTS_IOSIZE,    "iosize"},
    {OPTS_SECTOR,    "sector"},
    {OPTS_PARTITION, "partition"},
    {OPTS_RWPRI,     "rw"},
    {OPTS_RWSEC,     "rwsec"},
    {OPTS_LAUNCHER,  "launcher"},
    {OPTS_INO,       "ino"},
    {OPTS_FULLPATH,  "fullpath"}
};

const struct blocktrace blocktraces[] = {
    [66] = {BLK_BIO, "block_bio_bounce"},           // B
    [67] = {BLK_RQ,  "block_rq_complete"},          // C
    [68] = {BLK_RQ,  "block_rq_issue"},             // D
    [70] = {BLK_BIO, "block_bio_frontmerge"},       // F
    [71] = {BLK_BIO, "block_getrq"},                // G
    [73] = {BLK_RQ,  "block_rq_insert"},            // I
    [77] = {BLK_BIO, "block_bio_backmerge"},        // M
    [81] = {BLK_BIO, "block_bio_queue"},            // Q
    [82] = {BLK_RQ,  "block_rq_requeue"},           // R
    [83] = {BLK_BIO, "block_sleeprq"},              // S
    [88] = {BLK_BIO, "block_split"},                // X
};

const char *filename_base= "/sys/kernel/debug/os_health/kiodump/kiodump_trace";
volatile sig_atomic_t is_break = 0;

/**********************************************************************************
 *                                                                                *
 *                                function part                                   *
 *                                                                                *
 **********************************************************************************/

static int compare_output(const void *output_p1, const void *output_p2) {
    struct output_option *output1 = (struct output_option *) output_p1;
    struct output_option *output2 = (struct output_option *) output_p2;
    return strcmp(output1->option, output2->option); 
}

int parse_spec(char * option){
    unsigned long opts = 0;
    int i;

    qsort(output_options, ARRAY_NR(output_options), sizeof(struct output_option), compare_output);

    struct output_option key, *res;

    key.option = option;
    res = bsearch(&key, output_options, ARRAY_NR(output_options), sizeof(struct output_option), compare_output);

    if(res){
        opts = res->bit;
    }else{
        fprintf(stderr, "option %s is incorrect..\n", option);
        opts = -1;
    }

    return opts;
}

long get_opts_flag(char* opts_flag, char* output_opt)
{
    unsigned long opts;
    unsigned long opt;
    char         *sep_loc;
    char         * walk;
    walk = output_opt;
    opts = 0;
    while(1){
        sep_loc = strpbrk(walk, ",");
        if(sep_loc){
            *sep_loc = '\0';
        }else{
            opt = parse_spec(walk);
            if(opt == -1){
                return -1;
            }
            opts |= opt;
            break;
        }
        opt = parse_spec(walk);
        if(opt == -1){
            return -1;
        }
        opts |= opt;

        walk = sep_loc + 1;
    }

    sprintf(opts_flag, "%lu", opts);

    return opts;
}

int get_probe_point(char* probe_point_key, char* trace_action)
{
    int i;
    int point_key = 0;
    for(i = 0; i < ARRAY_NR(blocktraces); i++){
        if(!blocktraces[i].tracename){
            continue;
        }
        if(strlen(trace_action) == 1){
            if(trace_action[0] == i){
                point_key = i;
                break;
            }
        }else{
            if(!strcmp(trace_action, blocktraces[i].tracename)){
                point_key = i;
                break;
            }
        }
    }

    if(point_key){
        sprintf(probe_point_key, "%d", point_key);
    }

    return point_key;
}

int string_match(char *pattern, char *bematch)
{
    int ret;
    int nm = 1;
    regex_t reg;
    regmatch_t pmatch[nm];

    ret = regcomp(&reg, pattern, REG_EXTENDED|REG_NOSUB);
    if(0 == ret){
        ret = regexec(&reg, bematch, nm, pmatch, 0);
    }

    return ret;
}

void sign_handler(int sig, siginfo_t *siginfo, void *context){
    is_break = 1;
}

void print_usage(FILE *stream)
{
    fprintf(stream,"\n"
      "Usage: iodump [OPTIONS] \n"
       "\n"
       "Summary: this is a io tools. it can dump the details of struct request or struct bio.\n"
       "\n"
       "Options:\n"
       "  -h             Get the help information.\n"
       "  -H             Hiding header information.\n"
       "  -p <sda2>      Set partition parameter, this option is necessary.\n"
       "  -t <time>      Set tracing last time, default last time is %d second, default 300 seconds.\n"
       "  -s <filepath>  Set saving output to the file. if not set, it will output to standard output.\n"
       "  -S <number>    Set sample number, Only 1/number output is displayed, default 1.\n"
       "  -a <G>         Set blk tracepoint action which is fully compatible with blktrace, default G, See Actions.\n"
       "  -o <pid,comm>  Set the output field, such as datetime,comm,pid, See Formats.\n"
       "  -O <ino>       Set the extra output field, such as tid,ino, See Formats.\n"
       "  -c <comm>      Just output exact match comm string record.\n"
       "  -C <comm>      Just output record which comm contain the comm string.\n"
       "  -P <pid>       Just output exact match pid record. \n"
       "\n"
       "Major Actions:\n"
       "   ACTION  TRACEPOINT             UNIT\n"
       "   Q       block_bio_queue        bio\n"
       "   G       block_getrq            bio\n"
       "   I       block_rq_insert        request\n"
       "   D       block_rq_issue         request\n"
       "   C       block_rq_complete      request\n"
       "Minor Actions:\n"
       "   B       block_bio_bounce       bio\n"
       "   F       block_bio_frontmerge   bio\n"
       "   M       block_bio_backmerge    bio\n"
       "   S       block_sleeprq          bio\n"
       "   X       block_split            bio\n"
       "   R       block_rq_requeue       request\n"
       "\n"
       "Formats:\n"
       "   FIELD          DESCRIPTION\n"
       "   datetime       Such as 2022-03-23T16:42:05.315695, Precision millisecond.\n"
       "   timestamp      Such as 1648025082259168, Precision millisecond.\n"
       "   comm           Such as iodump, process short name.\n"
       "   pid            tgid \n"
       "   tid            task id\n"
       "   iosize         IO size, the unit is byte.\n"
       "   sector         Sector address on the disk.\n"
       "   partition      Such as sda5.\n"
       "   rw             The value list is R(READ),W(WRITE),D(DISCARD),E(SECURE_ERASE and DISCARD),F(FLUSH),N(Other).\n"
       "   rwsec          The value list is F(FUA:forced unit access),A(RAHEAD:read ahead),S(SYNC),M(META),E(SECURE),V(Vacant).\n"
       "   launcher       The bottom function of the call stack.\n"
       "   ino            Inode number.\n"
       "   fullpath       Read or Write file full path.\n"
       "\n",
       LAST_TIME
);
}

void print_header(long opts, int fd_output){
    char chan_buf[512] = {0};
    if(opts & OPTS_DATETIME){
        sprintf(chan_buf, "%-26s", "datetime");
    }
    if(opts & OPTS_TIMESTAMP){
        sprintf(chan_buf, "%s %16s ", chan_buf, "timestamp"); 
    }
    if(opts & OPTS_COMM){
        sprintf(chan_buf, "%s %-15s", chan_buf, "comm"); 
    }
    if(opts & OPTS_PID){
        sprintf(chan_buf, "%s %7s", chan_buf, "pid"); 
    }
    if(opts & OPTS_TID){
        sprintf(chan_buf, "%s %7s", chan_buf, "tid"); 
    }
    if(opts & OPTS_IOSIZE){
        sprintf(chan_buf, "%s %6s", chan_buf, "iosize"); 
    }
    if(opts & OPTS_SECTOR){
        sprintf(chan_buf, "%s %11s", chan_buf, "sector"); 
    }
    if(opts & OPTS_PARTITION){
        sprintf(chan_buf, "%s %-9s", chan_buf, "partition"); 
    }
    if(opts & OPTS_RWPRI){
        sprintf(chan_buf,"%s %2s", chan_buf, "rw"); 
    }
    if(opts & OPTS_RWSEC){
        sprintf(chan_buf,"%s %5s", chan_buf, "rwsec"); 
    }
    if(opts & OPTS_LAUNCHER){
        sprintf(chan_buf,"%s %-13s ", chan_buf, "launcher"); 
    }
    if(opts & OPTS_INO){
        sprintf(chan_buf,"%s %9s", chan_buf, "ino"); 
    }
    if(opts & OPTS_FULLPATH){
        sprintf(chan_buf,"%s %s", chan_buf, "fullpath");
    }
    sprintf(chan_buf,"%s\n", chan_buf);
    write(fd_output, chan_buf, strlen(chan_buf));
    fsync(fd_output);
}


/**********************************************************************************
 *                                                                                *
 *                                main part                                       *
 *                                                                                *
 **********************************************************************************/

int main(int argc, char *argv[])
{
    extern char *optarg;
    extern int optind, opterr, optopt;
    unsigned char opt;
    char output_file[1024]     = {0};
    char filename[512]         = {0};
    char partition_name[128]   = {0};
    char format_option[512]    = {0};
    char extra_option[512]     = {0};
    char trace_action[128]     = {0};
    char step_sampling[16]     = "1";
    char filter_comm[16]       = "";
    char match_comm[16]        = "";
    char filter_pid[16]        = "";
    char enable[2]             = {0};
    char probe_point_key[8]    = "71";
    char opts_flag[32]         = {0};
    char low_buf[LOW_BUFLEN]   = {0};
    char high_buf[HIGH_BUFLEN] = {0};
    char *action_pattern       = "^[A-z][A-z_]*$";
    char *option_pattern       = "^[a-z][a-z,]*$";
    char *partition_pattern    = "^[a-z][a-z0-9]*$";
    char *process_pattern      = "\\S+";
    char *filepath_pattern     = "\\S+";
    char *integer_pattern      = "^-?[1-9][0-9]*$|^0$";
    char *positive_integer     = "^[1-9][0-9]*$";
    int param_len;
    int relay_file[MAX_EVENTS];
    int fd_partition, fd_enable, fd_output;
    int i, bytesread, cputotal = 0;
    int saved_errno;
    int is_force_start, is_force_stop;
    int is_no_header;
    int last_time;
    int epfd, ready, j;
    int ret_val;
    int cpu_break, abnormal_cpu;
    unsigned long opts = 0;
    unsigned long extra_opts = 0;
    sigset_t block_set;
    struct statfs st;
    struct sigaction new_act;
    struct timespec interval;
    struct timespec remainder;
    struct epoll_event ev;
    struct epoll_event evlist[MAX_EVENTS];


    ///////////////////////////////// init option ////////////////////////////////////
    ret_val        = EXIT_SUCCESS;
    last_time      = LAST_TIME;
    is_force_start = 0;
    is_force_stop  = 0;
    is_no_header   = 0;
    opterr         = 0;
    abnormal_cpu   = 0;
    while(opt = getopt(argc, argv, ":fhHa:c:o:p:s:t:C:O:P:S:"))
    {
        if(opt == 255){        // 0xff
            break;
        }
        switch(opt)
        {
            case 'f':
                is_force_start = 1;
                break;
            case 'h':
                print_usage(stdout);
                exit(0);
                break;
            case 'H':
                is_no_header = 1;
                break;
            case 'a':
                if(string_match(action_pattern, optarg) != 0){
                    fprintf(stderr, "-o option's trace action %s is incorrect\n", optarg);
                    print_usage(stderr);
                    exit(PARAM_INCORRECT);
                }
                param_len = strlen(optarg) > 128 ? 128 : strlen(optarg);
                strncpy(trace_action, optarg, param_len);
                trace_action[param_len] = '\0';
                break;
            case 'c':
                if(string_match(process_pattern, optarg) != 0){
                    fprintf(stderr, "-c option's filter_comm %s is incorrect\n", optarg);
                    print_usage(stderr);
                    exit(PARAM_INCORRECT);
                }
                param_len = strlen(optarg) > 16 ? 16 : strlen(optarg);
                strncpy(filter_comm, optarg, param_len);
                filter_comm[param_len] = '\0';
                break;
            case 'o':
                if(string_match(option_pattern, optarg) != 0){
                    fprintf(stderr, "-o option's output option %s is incorrect\n", optarg);
                    print_usage(stderr);
                    exit(PARAM_INCORRECT);
                }
                param_len = strlen(optarg) > 512 ? 512 : strlen(optarg);
                strncpy(format_option, optarg, param_len);
                format_option[param_len] = '\0';
                break;
            case 'p':
                if(string_match(partition_pattern, optarg) != 0){
                    fprintf(stderr, "-p option's partition_name %s is incorrect\n", optarg);
                    print_usage(stderr);
                    exit(PARAM_INCORRECT);
                }
                param_len = strlen(optarg) > 128 ? 128 : strlen(optarg);
                strncpy(partition_name, optarg, param_len);
                partition_name[param_len] = '\0';
                break;
            case 's':
                if(string_match(filepath_pattern, optarg) != 0){
                    fprintf(stderr, "-s option's file path %s is incorrect\n", optarg);
                    print_usage(stderr);
                    exit(PARAM_INCORRECT);
                }
                param_len = strlen(optarg) > 1024 ? 1024 : strlen(optarg);
                strncpy(output_file, optarg, param_len);
                output_file[param_len] = '\0';
                break;
            case 't':
                if(string_match(integer_pattern, optarg) != 0){
                    fprintf(stderr, "-t option's last time %s is incorrect\n", optarg);
                    print_usage(stderr);
                    exit(PARAM_INCORRECT);
                }
                last_time = atoi(optarg);
                break;
            case 'C':
                if(string_match(process_pattern, optarg) != 0){
                    fprintf(stderr, "-C option's match_comm %s is incorrect\n", optarg);
                    print_usage(stderr);
                    exit(PARAM_INCORRECT);
                }
                param_len = strlen(optarg) > 16 ? 16 : strlen(optarg);
                strncpy(match_comm, optarg, param_len);
                match_comm[param_len] = '\0';
                break;
            case 'O':
                if(string_match(option_pattern, optarg) != 0){
                    fprintf(stderr, "-O option's extra output option %s is incorrect\n", optarg);
                    print_usage(stderr);
                    exit(PARAM_INCORRECT);
                }
                param_len = strlen(optarg) > 512 ? 512 : strlen(optarg);
                strncpy(extra_option, optarg, param_len);
                extra_option[param_len] = '\0';
                break;
            case 'P':
                if(string_match(positive_integer, optarg) != 0){
                    fprintf(stderr, "-P option's filter_pid %s is incorrect\n", optarg);
                    print_usage(stderr);
                    exit(PARAM_INCORRECT);
                }
                param_len = strlen(optarg) > 16 ? 16 : strlen(optarg);
                strncpy(filter_pid, optarg, param_len);
                filter_pid[param_len] = '\0';
                break;
            case 'S':
                if(string_match(positive_integer, optarg) != 0){
                    fprintf(stderr, "-S option's step sampling %s is incorrect\n", optarg);
                    print_usage(stderr);
                    exit(PARAM_INCORRECT);
                }
                param_len = strlen(optarg) > 16 ? 16 : strlen(optarg);
                strncpy(step_sampling, optarg, param_len);
                step_sampling[param_len] = '\0';
                break;
            case '?':
                fprintf(stderr, "undefined option: %s\n", argv[optind-1]);
                print_usage(stderr);
                exit(PARAM_INCORRECT);
                break;
            case ':':
                fprintf(stderr, "option %s missing argument\n", argv[optind-1]);
                print_usage(stderr);
                exit(PARAM_INCORRECT);
                break;
            default:
                fprintf(stderr, "parameter %s is incorrect.\n", argv[optind-1]);
                exit(PARAM_INCORRECT);
                break;
        }
    }
    if(strlen(partition_name) == 0){
        fprintf(stderr, "option -p not set, please use -h option to get the correct usage.\n");
        print_usage(stderr);
        exit(PARAM_INCORRECT);
    }
    if(strlen(format_option) > 0 && strlen(extra_option) > 0){
        fprintf(stderr, "option -o and -O cannot be used at the same time.\n");
        print_usage(stderr);
        exit(PARAM_INCORRECT);
    }
    if(((strlen(filter_pid) > 0) + (strlen(match_comm) > 0) + (strlen(filter_comm) > 0)) > 1){
        fprintf(stderr, "option -c, -C and -P cannot be used at the same time.\n");
        print_usage(stderr);
        exit(PARAM_INCORRECT);
    }


    ///////////////////////////////// check env ////////////////////////////////////
    if(geteuid()){
        printf("iodump command need root (or sudo) permission.\n");
        exit(1);
    }

    cputotal = sysconf(_SC_NPROCESSORS_ONLN);
    if(cputotal <= 0) {
        fprintf(stderr, "invalid cputotal value: %d\n", cputotal);
        ret_val = INVALID_CPU_TOTAL;
        goto release0;
    }
    if(access("/sys/module/kiodump", F_OK|R_OK|W_OK) == -1){
        saved_errno = errno;
        fprintf(stderr, "kiodump module is not load. reason is: %s", strerror(saved_errno)); 
        ret_val = saved_errno;
        goto release0;
    }

    if(!(statfs("/sys/kernel/debug", &st) == 0 && (int) st.f_type == (int) DEBUGFS_MAGIC)){
        saved_errno = errno;
        fprintf(stderr, "debugfs is not mount");
        ret_val = ENOENT;
        goto release0;
    } 

   
    ///////////////////////////////// init signal /////////////////////////////////

    sigfillset(&block_set);
    sigdelset(&block_set, SIGHUP);                               // terminal hup
    sigdelset(&block_set, SIGINT);                               // ctrl + c
    sigdelset(&block_set, SIGQUIT);                              // coredump
    sigdelset(&block_set, SIGTERM);                              // kill pid
    sigdelset(&block_set, SIGALRM);                              // alarm(time)
    // SIGQUIT SIGKILL SIGSTOP SIGHUP
    if(sigprocmask(SIG_BLOCK, &block_set, NULL) == -1){
        saved_errno =  errno;
        fprintf(stderr, "mask signal error. reason is: %s", strerror(saved_errno));
        ret_val = saved_errno;
        goto release1;
    }
    new_act.sa_mask = block_set;
    new_act.sa_flags = SA_SIGINFO;
    new_act.sa_sigaction = sign_handler;
    if((sigaction(SIGHUP,  &new_act, NULL) == -1) ||
       (sigaction(SIGINT,  &new_act, NULL) == -1) ||
       (sigaction(SIGQUIT, &new_act, NULL) == -1) ||
       (sigaction(SIGTERM, &new_act, NULL) == -1) ||
       (sigaction(SIGALRM, &new_act, NULL) == -1)){
        saved_errno = errno;
        fprintf(stderr, "register singal error. reason is: %s", strerror(saved_errno));
        ret_val = saved_errno;
        goto release1;
    }

    alarm(last_time);


    ///////////////////////////////// pid lock ////////////////////////////////////

    char pid_buf[32];
    int lockpid_fd;
    if((lockpid_fd = open("/sys/module/kiodump/parameters/lockpid", O_RDWR)) == -1){
        saved_errno = errno;
        fprintf(stderr, "open lockpid failure.");
        ret_val = saved_errno;
        goto release1;
    }
    if(flock(lockpid_fd, LOCK_EX|LOCK_NB) < 0){
        saved_errno = errno;
        if(EWOULDBLOCK == saved_errno){
            if(read(lockpid_fd, pid_buf, 32) == -1){
                saved_errno = errno;
                fprintf(stderr, "read lockpid failure.");
                ret_val = saved_errno;
                goto release1;
            }
            printf("iodump is running now, pid is %s\n", pid_buf);
            ret_val = EEXIST;
            goto release1;
        }else{
            fprintf(stderr, "lock lockpid failure.");
            ret_val = saved_errno;
            goto release1;
        }
    }
    snprintf(pid_buf, sizeof(pid_buf), "%d", getpid());
    if(write(lockpid_fd, pid_buf, strlen(pid_buf)) == -1){
        saved_errno = errno;
        fprintf(stderr, "fail to write lock file lockpid.");
        ret_val = saved_errno;
        goto release1;
    }


    ///////////////////////////////// init env ////////////////////////////////////

    // output_file
    if(strlen(output_file) > 0){
        fd_output = open(output_file, O_WRONLY|O_CREAT|O_APPEND, 0666);
        if(-1 == fd_output){
            saved_errno =  errno;
            fprintf(stderr, "param -s output filepath is incorrect. reason is: %s", strerror(saved_errno));
            ret_val = saved_errno;
            goto release2;
        }
    }else{
        fd_output = STDOUT_FILENO;
    }

    // disk_partition
    if(access("/sys/module/kiodump/parameters/disk_partition", F_OK | R_OK | W_OK) == -1){
        saved_errno = errno;
        if(saved_errno == ENOENT){
            fprintf(stderr, "file is not exist"); 
        }else{
            fprintf(stderr, "access fail. reason is: %s", strerror(saved_errno)); 
        }
        ret_val = saved_errno;
        goto release2;
    }
    fd_partition = open("/sys/module/kiodump/parameters/disk_partition", O_RDWR, 0766);
    if(fd_partition == -1){
        saved_errno = errno;
        fprintf(stderr, "param file disk_partition is not exist. reason is: %s", strerror(saved_errno)); 
        ret_val = saved_errno;
        goto release2;
    }
    if(write(fd_partition, partition_name, strlen(partition_name)) == -1){
        saved_errno = errno;
        fprintf(stderr, "write file disk_partition false. reason is: %s", strerror(saved_errno)); 
        ret_val = saved_errno;
        close(fd_partition);
        goto release2;
    }
    close(fd_partition);

    // step_sampling
    if(strlen(step_sampling) > 0){
        if(access("/sys/module/kiodump/parameters/step_sampling", F_OK | R_OK | W_OK) == -1){
            saved_errno = errno;
            if(saved_errno == ENOENT){
                fprintf(stderr, "file is not exist"); 
            }else{
                fprintf(stderr, "access fail. reason is: %s", strerror(saved_errno)); 
            }
            ret_val = saved_errno;
            goto release2;
        }
        int fd_sampling = open("/sys/module/kiodump/parameters/step_sampling", O_RDWR, 0766);
        if(fd_sampling == -1){
            saved_errno = errno;
            fprintf(stderr, "param file disk_partition is not exist. reason is: %s", strerror(saved_errno)); 
            ret_val = saved_errno;
            goto release2;
        }
        if(write(fd_sampling, step_sampling, strlen(step_sampling)) == -1){
            saved_errno = errno;
            fprintf(stderr, "write file step_sampling false. reason is: %s", strerror(saved_errno));
            ret_val = saved_errno;
            close(fd_sampling);
            goto release2;
        }
        close(fd_sampling);
    }

    // format_option
    if(strlen(format_option) > 0){
        if((opts = get_opts_flag(opts_flag, format_option)) == -1){
            saved_errno = errno;
            ret_val = saved_errno;
            goto release2;
        }
    }else{
        opts = OPTS_DATETIME|OPTS_COMM|OPTS_PID|OPTS_IOSIZE|OPTS_SECTOR|OPTS_RWPRI|OPTS_RWSEC|OPTS_LAUNCHER|OPTS_FULLPATH;
        if(strlen(extra_option) > 0){
            if((extra_opts = get_opts_flag(opts_flag, extra_option)) == -1){
                saved_errno = errno;
                ret_val = saved_errno;
                goto release2;
            }
        }
        opts |= extra_opts;
        sprintf(opts_flag, "%lu", opts);
    }
    if(strlen(opts_flag) > 0){
        if(access("/sys/module/kiodump/parameters/opts_flag", F_OK | R_OK | W_OK) == -1){
            saved_errno = errno;
            if(saved_errno == ENOENT){
                fprintf(stderr, "file is not exist"); 
            }else{
                fprintf(stderr, "access fail. reason is: %s", strerror(saved_errno)); 
            }
            ret_val = saved_errno;
            goto release2;
        }
        int fd_opts_flag = open("/sys/module/kiodump/parameters/opts_flag", O_RDWR, 0766);
        if(fd_opts_flag == -1){
            saved_errno = errno;
            fprintf(stderr, "param file opts_flag is not exist. reason is: %s", strerror(saved_errno)); 
            ret_val = saved_errno;
            goto release2;
        }
        if(write(fd_opts_flag, opts_flag, strlen(opts_flag)) == -1){
            saved_errno = errno;
            fprintf(stderr, "write file opts_flag false. reason is: %s", strerror(saved_errno));
            ret_val = saved_errno;
            close(fd_opts_flag);
            goto release2;
        }
        close(fd_opts_flag);
    }

    // trace_action
    if(strlen(trace_action) > 0){
        if(!get_probe_point(probe_point_key, trace_action)){
            saved_errno = errno;
            fprintf(stderr, "probe point %s is not supported.\n", trace_action);
            ret_val = saved_errno;
            goto release2;
        }
    }
    if(strlen(probe_point_key) > 0){
        if(access("/sys/module/kiodump/parameters/probe_point_key", F_OK | R_OK | W_OK) == -1){
            saved_errno = errno;
            if(saved_errno == ENOENT){
                fprintf(stderr, "file is not exist"); 
            }else{
                fprintf(stderr, "access fail. reason is: %s", strerror(saved_errno)); 
            }
            ret_val = saved_errno;
            goto release2;
        }
        int fd_probe_point = open("/sys/module/kiodump/parameters/probe_point_key", O_RDWR, 0766);
        if(fd_probe_point == -1){
            saved_errno = errno;
            fprintf(stderr, "param file probe_point_key is not exist. reason is: %s", strerror(saved_errno)); 
            ret_val = saved_errno;
            goto release2;
        }
        if(write(fd_probe_point, probe_point_key, strlen(probe_point_key)) == -1){
            saved_errno = errno;
            fprintf(stderr, "write file probe_point_key false. reason is: %s", strerror(saved_errno));
            ret_val = saved_errno;
            close(fd_probe_point);
            goto release2;
        }
        close(fd_probe_point);
    }

    // filter_pid
    int filter_pid_fd;
    if((filter_pid_fd = open("/sys/module/kiodump/parameters/filter_pid", O_RDWR)) == -1){
        saved_errno = errno;
        fprintf(stderr, "open filter_pid failure.");
        ret_val = saved_errno;
        goto release2;
    }
    if(strlen(filter_pid) == 0){
        strncpy(filter_pid, "0", 1);
        filter_pid[1] = '\0';
    }
    if(write(filter_pid_fd, filter_pid, strlen(filter_pid)) == -1){
        saved_errno = errno;
        fprintf(stderr, "fail to write filter_pid.");
        ret_val = saved_errno;
        goto release2;
    }

    // filter_comm
    int filter_comm_fd;
    if((filter_comm_fd = open("/sys/module/kiodump/parameters/filter_comm", O_RDWR)) == -1){
        saved_errno = errno;
        fprintf(stderr, "open filter_comm failure.");
        ret_val = saved_errno;
        goto release2;
    }
    if(strlen(filter_comm) == 0){
        strncpy(filter_comm, "\n", 1);
        filter_comm[1] = '\0';
    }
    if(write(filter_comm_fd, filter_comm, strlen(filter_comm)) == -1){
        saved_errno = errno;
        fprintf(stderr, "fail to write filter_comm.");
        ret_val = saved_errno;
        goto release2;
    }

    // match_comm
    int match_comm_fd;
    if((match_comm_fd = open("/sys/module/kiodump/parameters/match_comm", O_RDWR)) == -1){
        saved_errno = errno;
        fprintf(stderr, "open match_comm failure.");
        ret_val = saved_errno;
        goto release2;
    }
    if(strlen(match_comm) == 0){
        strncpy(match_comm, "\x0a", 1);
        match_comm[1] = '\0';
    }
    if(write(match_comm_fd, match_comm, strlen(match_comm)) == -1){
        saved_errno = errno;
        fprintf(stderr, "fail to write match_comm.");
        ret_val = saved_errno;
        goto release2;
    }

    /////////////////////////////// set epoll_event ////////////////////////////////

    epfd = epoll_create(MAX_EVENTS);
    if (epfd == -1){
        saved_errno = errno;
        fprintf(stderr, "epoll_create is failed. reason is: %s", strerror(saved_errno)); 
        ret_val = saved_errno;
        goto release3;
    }
    ev.events = EPOLLIN|EPOLLET;
//    ev.events = EPOLLIN;

    cpu_break = cputotal;
    for(i = 0; i < cputotal; i++) {
        sprintf(filename, "%s%d", filename_base, i);
        relay_file[i] = open(filename, O_RDONLY|O_NONBLOCK);                // open per-cpu file
        if(relay_file[i] == -1){
            abnormal_cpu++;
            if(abnormal_cpu > MAX_ABNORMAL_CPU){
                fprintf(stderr, "abnormal cpu core %d is too large.", abnormal_cpu);
                ret_val =  ABNORMAL_CPU_LARGE;
                cpu_break = i + 1;
                goto release4;
            }else{
                continue;
            }
        }
        ev.data.fd = relay_file[i];
        if(epoll_ctl(epfd, EPOLL_CTL_ADD, relay_file[i], &ev) == -1){
            saved_errno =  errno;
            fprintf(stderr, "relay_file[%d] EPOLL_CTL_ADD is failed. reason is: %s", i, strerror(saved_errno)); 
            ret_val = saved_errno;
            cpu_break = i + 1;
            goto release4;
        }
    }


    //////////////////////////////// open clean_trace ////////////////////////////////

    int fd_clean_trace = open("/sys/kernel/debug/os_health/kiodump/clean_trace", O_RDONLY|O_NONBLOCK);
    if(fd_clean_trace == -1){
        saved_errno = errno;
        fprintf(stderr, "open clean_trace file is failed. reason is: %s", strerror(saved_errno)); 
        ret_val = saved_errno;
        goto release5;
    }

    //////////////////////////////// enable tracing //////////////////////////////////

    if(access("/sys/module/kiodump/parameters/enable", F_OK|R_OK|W_OK) == -1){
        saved_errno = errno;
        if(saved_errno == ENOENT){
            fprintf(stderr, "file is not exist"); 
        }else if(saved_errno == EACCES){
            fprintf(stderr, "%s, iodump command need root (or sudo) permission.\n", strerror(saved_errno)); 
        }else{
            fprintf(stderr, "access fail. reason is: %s", strerror(saved_errno)); 
        }
        ret_val = saved_errno;
        goto release5;
    }
    fd_enable = open("/sys/module/kiodump/parameters/enable", O_RDWR, 0766);
    if(fd_enable == -1){
        saved_errno = errno;
        fprintf(stderr, "param enable file is not exist. reason is: %s", strerror(saved_errno)); 
        ret_val = saved_errno;
        goto release6;
    }
    if(read(fd_enable,enable,1) == -1){
        saved_errno =  errno;
        fprintf(stderr, "file enable is empty. reason is: %s", strerror(saved_errno)); 
        ret_val = saved_errno;
        goto release6;
    }
    if(strcmp(enable,"N") != 0){
        if(is_force_start){
            if(write(fd_enable, "N", 1) == -1){
                saved_errno =  errno;
                fprintf(stderr, "write file false. reason is: %s", strerror(saved_errno)); 
                ret_val = saved_errno;
                goto release6;
            }
            interval.tv_sec  = 0;
            interval.tv_nsec = 100000000;                                // sleep 100ms to start new iodump process
            nanosleep(&interval, &remainder);
        }else{
            fprintf(stderr, "iodump is running %s ", enable);
            ret_val = IODUMP_RUNNING;
            goto release6;
        }
    }
    if(write(fd_enable, "Y", 1) == -1){
        saved_errno = errno;
        fprintf(stderr, "write file false. reason is: %s", strerror(saved_errno)); 
        ret_val = saved_errno;
        goto release6;
    }


    //////////////////////////////// consume data //////////////////////////////////

    if(!is_no_header){
        print_header(opts, fd_output);
    }

    while(1){
        ready = epoll_wait(epfd, evlist, MAX_EVENTS, 10);               // epoll_wait 10 ms to expire.
        if(ready < -1){
            saved_errno =  errno;
            fprintf(stderr, "epoll_wait is failed. reason is: %s", strerror(saved_errno)); 
            ret_val = saved_errno;
            break;
        }else if(ready == -1) {
            saved_errno = errno;
            if(saved_errno == EINTR){
                if(is_break == 1){                                      // expected interrupt,   need break
                    break;
                }else{
                    continue;                                           // unexpected interrupt, need retry
                }
            }else{                                                      // other unidentified errer
                fprintf(stderr, "epoll_wait is failed. reason is: %s", strerror(saved_errno)); 
                ret_val = saved_errno;
                break;
            }
        }else if(ready == 0){                                            //do nothing, need retry
            for(i = 0; i < cputotal; i++) {
                if(relay_file[i] == -1){
                    continue;
                }
                bytesread = read(relay_file[i], low_buf, sizeof(low_buf));
                while(bytesread > 0) {
                    low_buf[bytesread] = '\0';
                    write(fd_output, low_buf, bytesread);
                    bytesread = read(relay_file[i], low_buf, sizeof(low_buf));
                };
            }
        }else if(ready > 0){
            for(j = 0; j < ready; j++){
                if(evlist[j].events & EPOLLIN){
                    bytesread = read(evlist[j].data.fd, high_buf, sizeof(high_buf));       // read ready per-cpu file
                    while(bytesread > 0){
                        high_buf[bytesread] = '\0';
                        write(fd_output, high_buf, bytesread);
                        if(is_break == 1){
                            break;
                        }
                        bytesread = read(evlist[j].data.fd, high_buf, sizeof(high_buf));
                    };
                    if(is_break == 1){
                        break;
                    }
                }else if(evlist[j].events & EPOLLERR){
                    if(close(evlist[j].data.fd) == -1){
                        saved_errno = errno;
                        fprintf(stderr, "EPOLLERR occur, close evlist fd error. reason is: %s", strerror(saved_errno));
                        ret_val = saved_errno;
                        break;
                    }
                }
            }
        }

        if(is_break == 1){
            break;
        }

        if(lseek(fd_enable, 0, SEEK_SET) == -1){
            saved_errno = errno;
            fprintf(stderr, "lseek enable fail. reason is: %s", strerror(saved_errno));
            is_force_stop = 1;
            break;
        }
        if(read(fd_enable, enable, 1) == -1){
            is_force_stop = 1;
            break;
        }
        if(0 != strcmp(enable, "Y")){
            is_force_stop = 1;
            break;
        }
    }

    if(write(fd_enable, "N", 1) == -1){
        saved_errno = errno;
        fprintf(stderr, "close iodump false: %s", strerror(errno));
        ret_val = saved_errno;
        close(fd_clean_trace);                   // double action
    }

    // empty relayfs buffer
    for(i = 0; i < cputotal; i++) {
        if(relay_file[i] == -1){
            continue;
        }
        bytesread = read(relay_file[i], high_buf, sizeof(high_buf));
        while(bytesread > 0) {
            high_buf[bytesread] = '\0';
            write(fd_output, high_buf, bytesread);
            bytesread = read(relay_file[i], high_buf, sizeof(high_buf));
        };
    }

    if(!is_no_header){
        print_header(opts, fd_output);
    }
    if(is_force_stop){
        fprintf(stderr, "some exception happened.");
    }

release6:
    close(fd_enable);
release5:
    close(fd_clean_trace);
release4:
    for(i = 0; i < cpu_break; i++) {
        close(relay_file[i]);                    // close per-cpu file
    }
release3:
    close(epfd);
release2:
    if(strlen(output_file) > 0){
        close(fd_output);
    }
release1:
    flock(lockpid_fd, LOCK_UN);
    close(lockpid_fd);
release0:

    return ret_val;
}

