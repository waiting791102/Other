#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define PROC_PATH "/proc"
#define PROC_STAT "/proc/stat"
#define PROC_MEMINFO "/proc/meminfo"
#define PROC_PID_STAT "/proc/%s/stat"

#define LINE_BUF_SIZE 128
#define PROCPS_BUFSIZE 2048
#define MAX_CPU_NUMBER 65
#define MAX_PROC_NAME 80
#define MAX_DES_NAME 80
#define MAX_USER_NAME 80
#define MAX_MEM_STR 16

#define SZ_ENVIRON_FLEN 1024
#define DEFAULT_DES_NAME "sys_proc"
#define SYSTEM_INFO_DIR    "/tmp/.qsys.info"
#define SYSTEM_INFO_RESOURCE    SYSTEM_INFO_DIR"/resource"
#define SYSTEM_INFO_RESOURCE_TMP    SYSTEM_INFO_DIR"/resource.tmp"
#define SRES_PROC_INFO    1
#define SRES_CPU_USAGE    2
#define SRES_MEM_INFO    4
#define SRES_ALL            0xffffffff
#define SYSTEM_INFO_EXPIRE 3

#ifndef PAGE_SIZE
#define PAGE_SIZE ( sysconf(_SC_PAGESIZE) )
#endif

/* from /proc/stat */
typedef struct _CPU_DATA {
    /* Linux 2.4.x has only first four */
    unsigned long long usr, nic, sys, idle;
    unsigned long long iowait, irq, softirq, steal;
} CPU_DATA;

typedef struct _CPU_INFO {
    /* Linux 2.4.x has only first four */
    double usr, nic, sys, idle;
    double iowait, irq, softirq, steal;
    double real, other;
} CPU_INFO;

/* from /proc/meminfo */
typedef struct _MEM_DATA {
    unsigned long mem_total, mem_free, buffers, cached;
    unsigned long swap_total, swap_free, sreclaimable, mem_share;
} MEM_DATA;

/* Support up to 4TiB (2^42) system memory (kB) */
typedef struct _MEM_INFO {
    uint mem_total;
    uint mem_used;
    uint mem_buffer;
    uint mem_cache;
    uint mem_mfree;
    uint mem_free;
    uint swap_total;
    uint swap_used;
    uint swap_free;
} MEM_INFO;

/* ref: man /proc(5) */
typedef struct _PROC_DATA {
    unsigned int uid;
    /* /proc/<pid>/stat (1) */
    pid_t pid;
    char comm[MAX_PROC_NAME];
    char state;
    pid_t ppid;
    int pgrp;
    /* /proc/<pid>/stat (6) */
    int session;
    int tty_nr;
    int tpgid;
    unsigned int flags;
    unsigned long int minflt;
    /* /proc/<pid>/stat (11) */
    unsigned long int cminflt;
    unsigned long int majflt;
    unsigned long int cmajflt;
    unsigned long int utime;
    unsigned long int stime;
    /* /proc/<pid>/stat (16) */
    long int cutime;
    long int cstime;
    long int priority;
    long int nice;
    long int num_threads;
    /* /proc/<pid>/stat (21) */
    long int itrealvalue;
    unsigned long long starttime;
    unsigned long int vsize;
    long int rss;
    unsigned long int rsslim;
} PROC_DATA;

typedef struct _PROC_INFO {
    uint pid;
    char user[MAX_USER_NAME];
    char state;
    double cpu_us;
    uint mem_us; /* (kB) */
    char mem_us_str[MAX_MEM_STR]; /* legacy (KB or MB) */
    unsigned long long proc_tx;
    unsigned long long proc_rx;
    char proc_des[MAX_DES_NAME];
    char proc_name[MAX_PROC_NAME];
} PROC_INFO;

typedef struct _SYS_RES {
    /* cpu */
    int cpu_phy_count;
    int cpu_phyid[MAX_CPU_NUMBER];
    int cpu_coreid[MAX_CPU_NUMBER];
    int cpu_array_count;
    CPU_INFO *cpu_info;
    double cpu_usage[MAX_CPU_NUMBER]; /* legacy */
    /* memory */
    MEM_INFO mem;
    /* process */
    int proc_count;
    PROC_INFO *proc_info;
} SYS_RES;

int Get_CPU_Data(CPU_DATA **data, int *number)
{
    FILE *fp = NULL;
    char line_buf[LINE_BUF_SIZE] = {0};

    if ((data == NULL) || (number == NULL) || ((fp = fopen(PROC_STAT, "r")) == NULL))
        return -1;

    (*number) = 0;
    while (fgets(line_buf, LINE_BUF_SIZE, fp) && line_buf[0] == 'c') /* "cpu" */
    {
        (*data) = (CPU_DATA *)realloc(*data, sizeof(CPU_DATA) * ((*number) + 1));
        memset(&(*data)[*number], 0, sizeof(CPU_DATA));
        sscanf(line_buf, "cp%*s %llu %llu %llu %llu %llu %llu %llu %llu",
                &(*data)[*number].usr, &(*data)[*number].nic, &(*data)[*number].sys,
                &(*data)[*number].idle, &(*data)[*number].iowait, &(*data)[*number].irq,
                &(*data)[*number].softirq, &(*data)[*number].steal);
        (*number)++;
    }
    if (fp != NULL)
        fclose(fp);
    return 0;
}

int Cal_CPU_Info(CPU_DATA *data_t1, CPU_DATA *data_t2, int number, CPU_INFO **info)
{
    int index = 0;
    unsigned long long total_t1 = 0, total_t2 = 0, total_diff = 0;

    if ((data_t1 == NULL) || (data_t2 == NULL) || (info == NULL) || (number <= 0))
        return -1;

    (*info) = (CPU_INFO *)realloc(*info, sizeof(CPU_INFO) * number);
    memset((*info), 0, sizeof(CPU_INFO) * number);
    for (index = 0; index < number; index++)
    {
        total_t1 = data_t1[index].usr + data_t1[index].nic + data_t1[index].sys + data_t1[index].idle
            + data_t1[index].iowait + data_t1[index].irq + data_t1[index].softirq + data_t1[index].steal;
        total_t2 = data_t2[index].usr + data_t2[index].nic + data_t2[index].sys + data_t2[index].idle
            + data_t2[index].iowait + data_t2[index].irq + data_t2[index].softirq + data_t2[index].steal;
        total_diff = (total_t2 -total_t1) == 0 ? 1 : (total_t2 -total_t1);

        /*xxx = xxx_diff / total_diff */
        (*info)[index].usr = 100 * (double)(data_t2[index].usr - data_t1[index].usr) / total_diff;
        (*info)[index].sys = 100 * (double)(data_t2[index].sys - data_t1[index].sys) / total_diff;
        (*info)[index].nic = 100 * (double)(data_t2[index].nic - data_t1[index].nic) / total_diff;
        (*info)[index].idle = 100 * (double)(data_t2[index].idle - data_t1[index].idle) / total_diff;
        (*info)[index].iowait = 100 * (double)(data_t2[index].iowait - data_t1[index].iowait) / total_diff;
        (*info)[index].irq = 100 * (double)(data_t2[index].irq - data_t1[index].irq) / total_diff;
        (*info)[index].softirq = 100 * (double)(data_t2[index].softirq - data_t1[index].softirq) / total_diff;
        (*info)[index].steal = 100 * (double)(data_t2[index].steal - data_t1[index].steal) / total_diff;
        /* other = (nic_diff + irq_diff + sirq_diff + steal_diff) / total_diff */
        (*info)[index].other = 100 * ((double)(data_t2[index].nic - data_t1[index].nic)
            + (data_t2[index].irq - data_t1[index].irq) + (data_t2[index].softirq - data_t1[index].softirq)
            + (data_t2[index].steal - data_t1[index].steal)) / total_diff;
        /* real = (total_diff - idle_diff - iowait_diff) / total_diff */
        (*info)[index].real = 100 * ((double)(total_diff - (data_t2[index].idle - data_t1[index].idle)
            - (data_t2[index].iowait - data_t1[index].iowait)) / total_diff);
    }
    return 0;
}

int Get_Mem_Data(MEM_DATA *data)
{
    FILE *fp = NULL;
    char *index = NULL;
    char line_buf[LINE_BUF_SIZE] = {0};

    if ((data == NULL) || ((fp = fopen(PROC_MEMINFO, "r")) == NULL))
        return -1;

    while (fgets(line_buf, LINE_BUF_SIZE, fp))
    {
        if (!(index = strchr(line_buf, ':')))
            continue;
        *index = '\0';
        if (!strcmp("MemTotal", line_buf))
            data->mem_total = strtoul(index+1, NULL, 10);
        else if (!strcmp("MemFree", line_buf))
            data->mem_free = strtoul(index+1, NULL, 10);
        else if (!strcmp("Buffers", line_buf))
            data->buffers = strtoul(index+1, NULL, 10);
        else if (!strcmp("Cached", line_buf))
            data->cached = strtoul(index+1, NULL, 10);
        else if (!strcmp("SwapTotal", line_buf))
            data->swap_total = strtoul(index+1, NULL, 10);
        else if (!strcmp("SwapFree", line_buf))
            data->swap_free = strtoul(index+1, NULL, 10);
        else if (!strcmp("SReclaimable", line_buf))
            data->sreclaimable = strtoul(index+1, NULL, 10);
        else if (!strcmp("Shmem", line_buf))
            data->mem_share = strtoul(index+1, NULL, 10);
    }

    if (fp != NULL)
        fclose(fp);
    return 0;
}

int Cal_Mem_Info(MEM_DATA *data, MEM_INFO *info)
{
    if ((data == NULL) || (info == NULL) || (data->mem_total <= 0))
        return -1;

    memset(info, 0, sizeof(MEM_INFO));

    /* memory */
    info->mem_total = data->mem_total;
    info->mem_mfree = data->mem_free;
    info->mem_buffer = data->buffers;
    info->mem_cache = data->cached;
    info->mem_free = data->mem_free + data->buffers + data->cached + data->sreclaimable;
    if (info->mem_free >= data->mem_share)
        info->mem_free -= data->mem_share;
    info->mem_used = info->mem_total - info->mem_free;

    /* swap */
    info->swap_free = data->swap_free;
    if (data->swap_total > 0)
    {
        info->swap_total = data->swap_total;
        info->swap_used = data->swap_total - data->swap_free;
    }
    else
    {
        info->swap_total = 0;
        info->swap_used = 0;
    }
    return 0;
}

int Get_Proc_Data(PROC_DATA **data, int *count)
{
    int ret = 0;
    int fd = -1;
    char *index = NULL;
    DIR *dp = NULL;
    struct dirent *entry = NULL;
    struct stat data_stat = {0};
    char line_buf[PROCPS_BUFSIZE] = {0};

    if ((data == NULL) || (count == NULL) || ((dp = opendir(PROC_PATH)) == NULL))
        return -1;

    (*count) = 0;
    while ((entry = readdir(dp)))
    {
        /* only for /proc/<pid>/stat */
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
            continue;

        snprintf(line_buf, sizeof(line_buf), PROC_PID_STAT, entry->d_name);
        /* process probably exited */
        if ((stat(line_buf, &data_stat) == 0) && ((fd = open(line_buf, O_RDONLY)) >= 0))
        {
            if ((ret = read(fd, line_buf, sizeof(line_buf)-1)) < 0)
            {
                close(fd);
                continue;
            }
            line_buf[ret > 0 ? ret : 0] = '\0';
            (*data) = (PROC_DATA *)realloc(*data, sizeof(PROC_DATA) * ((*count) + 1));
            memset(&(*data)[*count], 0, sizeof(PROC_DATA));
            (*data)[*count].uid = data_stat.st_uid;
            /* proc/<pid>/stat (1)~(2) */
            (*data)[*count].pid = atoi(line_buf);
            index = strrchr(line_buf, ')');
            *index = '\0';
            snprintf((*data)[*count].comm, MAX_PROC_NAME, "%s", (strchr(line_buf, '(') + 1));
            /* proc/<pid>/stat (3)~(25) */
            sscanf(index + 2,
                    "%c %d %d"
                    "%d %d %d"
                    "%u %lu %lu"
                    "%lu %lu %lu"
                    "%lu %ld %ld"
                    "%ld %ld %ld"
                    "%ld %llu %lu"
                    "%ld %lu",
                &(*data)[*count].state, &(*data)[*count].ppid, &(*data)[*count].pgrp,
                &(*data)[*count].session, &(*data)[*count].tty_nr, &(*data)[*count].tpgid,
                &(*data)[*count].flags, &(*data)[*count].minflt, &(*data)[*count].cminflt,
                &(*data)[*count].majflt, &(*data)[*count].cmajflt, &(*data)[*count].utime,
                &(*data)[*count].stime, &(*data)[*count].cutime, &(*data)[*count].cstime,
                &(*data)[*count].priority, &(*data)[*count].nice, &(*data)[*count].num_threads,
                &(*data)[*count].itrealvalue, &(*data)[*count].starttime, &(*data)[*count].vsize,
                &(*data)[*count].rss, &(*data)[*count].rsslim);
            (*count)++;
        }
        if (fd >= 0)
            close(fd);
    }
    if (dp != NULL)
        closedir(dp);
    return 0;
}

int get_user_name(uint uid, char *user)
{
    char *index = NULL, *end = NULL;
    FILE *fp = NULL;
    char line_buf[PROCPS_BUFSIZE] = {0};

    if ((uid < 0) || (user == NULL))
        return -1;

    if ((fp = fopen("/etc/passwd", "r")) != NULL)
    {
        while (fgets(line_buf, LINE_BUF_SIZE, fp))
        {
            /* user name: */
            if ((index = strchr(line_buf, ':')) != NULL)
                *index = '\0';
            /* encryption passwd:uid: */
            if (((index = strchr(index + 1, ':')) != NULL) &&
                ((end = strchr(index + 1, ':')) != NULL))
                *end = '\0';
            if (uid == atoi(index+1))
                snprintf(user, MAX_USER_NAME, "%s", line_buf);
        }
    }
    if (fp != NULL)
        fclose(fp);
    return 0;
}

void get_process_description(uint pid, char *proc_des)
{
#define ENVIRON_FMT    "/proc/%d/environ"
    FILE *fp;
    char buf[SZ_ENVIRON_FLEN];
    int sz;
    char *ptr_s, *ptr_e;

    memset(proc_des, 0, MAX_DES_NAME);
    snprintf(proc_des, MAX_DES_NAME, DEFAULT_DES_NAME);
    snprintf(buf, SZ_ENVIRON_FLEN, ENVIRON_FMT, pid);
    fp = fopen(buf, "r");
    if (fp) {
        if ((sz = fread(buf, 1, SZ_ENVIRON_FLEN - 1, fp)) > 0) {
            // replace EOF to EOL
            while (sz >= 0) {
                if ((unsigned char)(buf[sz]) < ' ')
                    buf[sz] = '\n';
                sz--;
            }

            // QNAP_QPKG=xxxx
            ptr_s = strstr(buf, "QNAP_QPKG=");
            if (ptr_s) {
                ptr_s += 10;
                ptr_e = strchr(ptr_s, '\n');
                sz = ptr_e - ptr_s;
                if (ptr_e && sz < MAX_DES_NAME) {
                    strncpy(proc_des, ptr_s, sz);
                    proc_des[sz] = '\0';
                }
            }
        }
        fclose(fp);
    }
#undef ENVIRON_FMT
}

int Cal_Proc_Info(CPU_DATA *cpu_t1, CPU_DATA *cpu_t2, int cpu_number, PROC_DATA *proc_t1, int count_t1,
                PROC_DATA *proc_t2, int count_t2, PROC_INFO **info, int *count)
{
    int index_t1 = 0, index_t2 = 0;
    unsigned long long total_t1 = 0, total_t2 = 0, total_diff = 0;
    double period = 0;

    if ((cpu_t1 == NULL) || (cpu_t2== NULL) || (proc_t1 == NULL) || (count_t1 == 0) ||
        (proc_t2 == NULL) || (count_t2 == 0) || (info == NULL) || (count == NULL))
        return -1;

    total_t1 = cpu_t1[0].usr + cpu_t1[0].nic + cpu_t1[0].sys + cpu_t1[0].idle
        + cpu_t1[0].iowait + cpu_t1[0].irq + cpu_t1[0].softirq + cpu_t1[0].steal;
    total_t2 = cpu_t2[0].usr + cpu_t2[0].nic + cpu_t2[0].sys + cpu_t2[0].idle
        + cpu_t2[0].iowait + cpu_t2[0].irq + cpu_t2[0].softirq + cpu_t2[0].steal;
    total_diff = (total_t2 -total_t1) == 0 ? 1 : (total_t2 -total_t1);
    period = (double)total_diff / (cpu_number - 1);

    (*count) = 0;
    for (index_t1 = 0, index_t2 = 0; (index_t1 < count_t1) && (index_t2 < count_t2); index_t1++, index_t2++)
    {
        if ((proc_t1[index_t1].pid == proc_t2[index_t2].pid) && /* pid and command match */
            !(strcmp(proc_t1[index_t1].comm, proc_t2[index_t2].comm)))
        {
            (*info) = (PROC_INFO *)realloc(*info, sizeof(PROC_INFO) * ((*count) + 1));
            memset(&(*info)[*count], 0, sizeof(PROC_INFO));
            (*info)[*count].pid = proc_t2[index_t2].pid;
            get_user_name(proc_t2[index_t2].uid, (*info)[*count].user);
            (*info)[*count].state = proc_t2[index_t2].state;
            (*info)[*count].cpu_us = 100 * (double)((proc_t2[index_t2].utime + proc_t2[index_t2].stime) -
                                    (proc_t1[index_t1].utime + proc_t1[index_t1].stime)) / period;
            (*info)[*count].mem_us = proc_t2[index_t2].rss * PAGE_SIZE / 1024;
            /* legacy memory field */
            if ((*info)[*count].mem_us < 1024 * 10)
                snprintf((*info)[*count].mem_us_str, MAX_MEM_STR, "%dK", (*info)[*count].mem_us);
            else
                snprintf((*info)[*count].mem_us_str, MAX_MEM_STR, "%dM", (*info)[*count].mem_us / 1024);
            get_process_description(proc_t2[index_t2].pid, (*info)[*count].proc_des);
            snprintf((*info)[*count].proc_name, MAX_PROC_NAME, "%s", proc_t2[index_t2].comm);
            (*count)++;
        }
        else if ((proc_t1[index_t1].pid == proc_t2[index_t2].pid) &&  /* pid match but command not match */
            (strcmp(proc_t1[index_t1].comm, proc_t2[index_t2].comm)))
        {
            continue;
        }
        else if (proc_t1[index_t1].pid > proc_t2[index_t2].pid) /* waiting proc_t2 */
        {
            index_t1--;
        }
        else if (proc_t1[index_t1].pid < proc_t2[index_t2].pid) /* waiting proc_t1 */
        {
            index_t2--;
        }
     }
    return 0;
}

int Write_CPU_Info(int fd, CPU_INFO *info, int number)
{
    int index = 0;
    char line_buf[PROCPS_BUFSIZE] = {0};

    if ((fd < 0) || (info == NULL) || (number <= 0))
        return -1;

    if (write(fd,line_buf, strlen(line_buf)) < 0)
        return -1;
    for (index = 0; index < number; index++)
    {
        snprintf(line_buf, sizeof(line_buf),
                "CPU_INFO %f %f %f %f %f\n",
                info[index].real, info[index].other, info[index].usr,
                info[index].sys, info[index].iowait);
        if (write(fd, line_buf, strlen(line_buf)) < 0)
            return -1;
    }
    return 0;
}

int Write_Mem_Info(int fd, MEM_INFO *info)
{
    char line_buf[PROCPS_BUFSIZE] = {0};

    if ((fd < 0) || (info == NULL))
        return -1;

    snprintf(line_buf, sizeof(line_buf),
            "MEM_INFO %u %u %u %u %u %u %u %u %u\n",
            info->mem_total, info->mem_used, info->mem_mfree,
            info->mem_free, info->mem_buffer,info->mem_cache,
            info->swap_total, info->swap_used, info->swap_free);
    if (write(fd, line_buf, strlen(line_buf)) < 0)
        return -1;
    return 0;
}

int Write_Proc_Info(int fd, PROC_INFO *info, int count)
{
    int index = 0;
    char line_buf[PROCPS_BUFSIZE] = {0};

    if ((fd < 0) || (info == NULL) || (count <= 0))
        return -1;

    for (index = 0; index < count; index++)
    {
        snprintf(line_buf, sizeof(line_buf),
                "PROC_INFO %d %s %c %f %d %s %s %s\n",
                info[index].pid, info[index].user, info[index].state,
                info[index].cpu_us, info[index].mem_us, info[index].mem_us_str,
                info[index].proc_des, info[index].proc_name);
        if (write(fd, line_buf, strlen(line_buf)) < 0)
            return -1;
    }
    return 0;
}

void create_system_info_dir()
{
    struct stat st;
    if (stat(SYSTEM_INFO_DIR, &st) != 0)
        mkdir(SYSTEM_INFO_DIR, 755);
    else if(!S_ISDIR(st.st_mode)){
        unlink(SYSTEM_INFO_DIR);
        mkdir(SYSTEM_INFO_DIR, 755);
    }
}

int Generate_System_Resource_File(void)
{
    /* declare */
    struct stat st = {0};
    int fd = -1;
    time_t now_time = time(&now_time);
    int interval = 2120000;
    /* CPU */
    int cpu_number = 0;
    CPU_DATA *cpu_data_t1 = NULL, *cpu_data_t2 = NULL;
    CPU_INFO *cpu_info = NULL;
    /* Memory */
    MEM_DATA *mem_data = NULL;
    MEM_INFO *mem_info = NULL;
    /* Process */
    int proc_count_t1 = 0, proc_count_t2 = 0;
    PROC_DATA *proc_data_t1 = NULL, *proc_data_t2 = NULL;
    int proc_count = 0;
    PROC_INFO *proc_info = NULL;

    create_system_info_dir();
    if ((stat(SYSTEM_INFO_RESOURCE, &st) != 0))
    {
        /* cache file isn't exist */
        interval = 100000;
    }
    else if ((now_time - st.st_mtime) < (SYSTEM_INFO_EXPIRE - interval))
    {
        /* cache file is exist and new */
        return 0;
    }
    else if ((stat(SYSTEM_INFO_RESOURCE_TMP, &st) == 0) &&
        ((now_time - st.st_mtime) >= SYSTEM_INFO_EXPIRE * 3))
    {
        /* cache file too old and temp file too old */
        unlink(SYSTEM_INFO_RESOURCE_TMP);
    }

    if ((fd = open(SYSTEM_INFO_RESOURCE_TMP,
            O_RDWR | O_CREAT | O_EXCL | O_TRUNC, 0777)) < 0)
        return 0;

    /* initialize */
    /* CPU */
    cpu_data_t1 = (CPU_DATA *)calloc(0, sizeof(CPU_DATA));
    cpu_data_t2 = (CPU_DATA *)calloc(0, sizeof(CPU_DATA));
    cpu_info = (CPU_INFO *)calloc(0, sizeof(CPU_INFO));
    /* Memory */
    mem_data = (MEM_DATA *)calloc(1, sizeof(MEM_DATA));
    mem_info = (MEM_INFO *)calloc(1, sizeof(MEM_INFO));
    /* Process */
    proc_data_t1 = (PROC_DATA *)calloc(0, sizeof(PROC_DATA));
    proc_data_t2 = (PROC_DATA *)calloc(0, sizeof(PROC_DATA));
    proc_info = (PROC_INFO *)calloc(0, sizeof(PROC_INFO));

    /* get sample data t1 */
    if (Get_Mem_Data(mem_data) ||
        Get_CPU_Data(&cpu_data_t1, &cpu_number) ||
        Get_Proc_Data(&proc_data_t1, &proc_count_t1))
        return -1;
    /* sampling interval */
    usleep(interval); //  usleep(2120000);
    /* get sample data t2 */
    if (Get_CPU_Data(&cpu_data_t2, &cpu_number) ||
        Get_Proc_Data(&proc_data_t2, &proc_count_t2))
        return -2;
    /* calculate sample data to information */
    if (Cal_Mem_Info(mem_data, mem_info) ||
        Cal_CPU_Info(cpu_data_t1, cpu_data_t2, cpu_number, &cpu_info) ||
        Cal_Proc_Info(cpu_data_t1, cpu_data_t2, cpu_number,
            proc_data_t1, proc_count_t1, proc_data_t2, proc_count_t2,
            &proc_info, &proc_count))
        return -3;

    /* write temp cache file */
    if (Write_CPU_Info(fd, cpu_info, cpu_number) ||
        Write_Mem_Info(fd, mem_info) ||
        Write_Proc_Info(fd, proc_info, proc_count))
        return -4;
    /* overwrite cache file */
    if (rename(SYSTEM_INFO_RESOURCE_TMP, SYSTEM_INFO_RESOURCE) == -1)
        return -5;
/*
    Print_CPU_Data(cpu_data_t1, cpu_number);
    Print_CPU_Data(cpu_data_t2, cpu_number);
    Print_CPU_Info(cpu_info, cpu_number);
    Print_Mem_Data(mem_data);
    Print_Mem_Info(mem_info);
    Print_Proc_Data(proc_data_t1, proc_count_t1);
    Print_Proc_Data(proc_data_t2, proc_count_t2);
    Print_Proc_Info(proc_info, proc_count);
*/
    /* free */
    if (fd >= 0)
        close(fd);
    /* CPU */
    free(cpu_data_t1);
    free(cpu_data_t2);
    free(cpu_info);
    /* Memory */
    free(mem_data);
    free(mem_info);
    /* Process */
    free(proc_data_t1);
    free(proc_data_t2);
    free(proc_info);
    return 0;
}

 int get_phy_cpu_index_list(int *phy_cpu_index_list, int *core_cpu_index_list, int *cpu_phy_count)
{
    int count = MAX_CPU_NUMBER;
    int phy_count = *cpu_phy_count = 0;
    if (!phy_cpu_index_list || !core_cpu_index_list)
        return -1;

    FILE *fp = fopen("/proc/cpuinfo", "r");
    char buffer[1024];
    int cur_core = -1;
    int list_count = 0;
    int max_cpu_index = -1;

    if (fp == NULL)
        return -1;
    memset(phy_cpu_index_list, 0, sizeof(int)*count);
    memset(core_cpu_index_list, 0, sizeof(int)*count);
    phy_cpu_index_list[0] = -1;
    core_cpu_index_list[0] = -1;
    while (fgets(buffer, sizeof(buffer), fp)) {
        if (strncmp("physical id", buffer, 11) == 0) {
            if (cur_core > 0 && cur_core < count) {
                char *ptr = strchr(buffer, ':');
                if (ptr) {
                    int phy_index = atoi(ptr+1);
                    phy_cpu_index_list[cur_core] = phy_index;
                    list_count ++;
                    if (phy_index > max_cpu_index) {
                        max_cpu_index = phy_index;
                        phy_count ++;
                    }
                }
            }
        }
        else if (strncmp("core id", buffer, 7) == 0) {
            if (cur_core > 0 && cur_core < count) {
                char *ptr = strchr(buffer, ':');
                if (ptr) {
                    core_cpu_index_list[cur_core] = atoi(ptr+1);
                }
            }
        }
        else if (strncmp("processor", buffer, 9) == 0) {
            char *ptr = strchr(buffer, ':');
            if (ptr) {
                cur_core = atoi(ptr+1)+1;    //the first 1 (index 0) in array is total average
            }
        }
    }
    fclose(fp);
    if (-1 == max_cpu_index) {
        phy_count = 1;
        if (-1 == cur_core) {
            //assume this is single CPU with single core
            phy_cpu_index_list[1] = 0;
        }
        else {
            //assume this is single CPU with multi cores
            int i;
            for (i=1; i<=cur_core; i++) {
                phy_cpu_index_list[i] = 0;
            }
        }
    }
    *cpu_phy_count = phy_count;
    return list_count;
}

int Get_System_Resource(SYS_RES *sys_res, int res_mask)
{
    FILE *fp = NULL;
    char pid_buf[PROCPS_BUFSIZE] = {0};
    char line_buf[PROCPS_BUFSIZE] = {0};

    if ((sys_res == NULL) || (Generate_System_Resource_File() < 0) ||
        ((fp = fopen(SYSTEM_INFO_RESOURCE, "r")) == NULL))
        return -1;

    /* init */
    memset(sys_res, 0, sizeof(SYS_RES));
    sys_res->cpu_array_count = 0;
    get_phy_cpu_index_list(sys_res->cpu_phyid, sys_res->cpu_coreid, &sys_res->cpu_phy_count);
    sys_res->proc_count = 0;
    /* parse */
    while (fgets(line_buf, LINE_BUF_SIZE, fp))
    {
        if ((res_mask & SRES_CPU_USAGE) && (strstr(line_buf, "CPU_INFO")))
        {
            sys_res->cpu_info = (CPU_INFO *)realloc(sys_res->cpu_info, sizeof(CPU_INFO) * (sys_res->cpu_array_count + 1));
            memset(&sys_res->cpu_info[sys_res->cpu_array_count], 0, sizeof(CPU_DATA));
            sscanf(line_buf, "%*s %lf %lf %lf %lf %lf",
                    &sys_res->cpu_info[sys_res->cpu_array_count].real, &sys_res->cpu_info[sys_res->cpu_array_count].other,
                    &sys_res->cpu_info[sys_res->cpu_array_count].usr, &sys_res->cpu_info[sys_res->cpu_array_count].sys,
                    &sys_res->cpu_info[sys_res->cpu_array_count].iowait);
            sys_res->cpu_usage[sys_res->cpu_array_count] = sys_res->cpu_info[sys_res->cpu_array_count].real; /* legacy cpu usage */
            sys_res->cpu_array_count++;
            if (sys_res->cpu_array_count > MAX_CPU_NUMBER)
                sys_res->cpu_array_count = MAX_CPU_NUMBER;
        }
        else if ((res_mask & SRES_MEM_INFO) && (strstr(line_buf, "MEM_INFO")))
        {
            sscanf(line_buf, "%*s %u %u %u %u %u %u %u %u %u\n",
                    &sys_res->mem.mem_total, &sys_res->mem.mem_used, &sys_res->mem.mem_mfree,
                    &sys_res->mem.mem_free, &sys_res->mem.mem_buffer, &sys_res->mem.mem_cache,
                    &sys_res->mem.swap_total, &sys_res->mem.swap_used, &sys_res->mem.swap_free);
        }
        else if ((res_mask & SRES_PROC_INFO) && (strstr(line_buf, "PROC_INFO")))
        {
            sys_res->proc_info = (PROC_INFO *)realloc(sys_res->proc_info, sizeof(PROC_INFO) * (sys_res->proc_count + 1));
            memset(&sys_res->proc_info[sys_res->proc_count], 0, sizeof(PROC_INFO));
            sscanf(line_buf, "%*s %d %s %c %lf %u %s %s %s\n",
                &sys_res->proc_info[sys_res->proc_count].pid, sys_res->proc_info[sys_res->proc_count].user,
                &sys_res->proc_info[sys_res->proc_count].state, &sys_res->proc_info[sys_res->proc_count].cpu_us,
                &sys_res->proc_info[sys_res->proc_count].mem_us, sys_res->proc_info[sys_res->proc_count].mem_us_str,
                sys_res->proc_info[sys_res->proc_count].proc_des, sys_res->proc_info[sys_res->proc_count].proc_name);
#ifdef QNAP_HAL_SUPPORT
            snprintf(pid_buf, sizeof(pid_buf), "%d", sys_res->proc_info[sys_res->proc_count].pid);
            memset(line_buf, 0, sizeof(line_buf));
//            Conf_Get_Field(CONF_QNET_PATH, pid_buf, CONF_KEY_TX, line_buf, sizeof(line_buf));
            sys_res->proc_info[sys_res->proc_count].proc_tx = strtoull(line_buf, NULL, 10);
//            Conf_Get_Field(CONF_QNET_PATH, pid_buf, CONF_KEY_RX, line_buf, sizeof(line_buf));
            sys_res->proc_info[sys_res->proc_count].proc_rx = strtoull(line_buf, NULL, 10);
#endif
            sys_res->proc_count++;
        }
    }
    if (fp != NULL)
        fclose(fp);
    if (((res_mask & SRES_CPU_USAGE) && (sys_res->cpu_info == NULL)) ||
        ((res_mask & SRES_PROC_INFO) && (sys_res->proc_info == NULL)))
        return -1;
    return 0;
}

int Print_CPU_Data(CPU_DATA *data, int number)
{
    int index = 0;

    if ((data == NULL) || (number <= 0))
        return -1;

    for (index = 0; index < number; index++)
    {
        printf("usr: %llu nic: %llu sys: %llu idle: %llu iowait: %llu irq: %llu softirq: %llu steal: %llu\n",
                data[index].usr, data[index].nic, data[index].sys, data[index].idle,
                data[index].iowait, data[index].irq, data[index].softirq, data[index].steal);
    }
    return 0;
}

int Print_CPU_Info(CPU_INFO *info, int number)
{
    int index = 0;

    if ((info == NULL) || (number <= 0))
        return -1;

    for (index = 0; index < number; index++)
    {
        printf("real: %8.4f%% other: %8.4f%% usr: %8.4f%% sys: %8.4f%% iowait: %8.4f%% "
                "idle: %8.4f%% nic: %8.4f%% irq: %8.4f%% softirq: %8.4f%% steal: %8.4f%%\n",
                info[index].real, info[index].other, info[index].usr, info[index].sys,
                info[index].iowait, info[index].idle, info[index].nic, info[index].irq,
                info[index].softirq, info[index].steal);
    }
    return 0;
}

int Print_Mem_Data(MEM_DATA *data)
{
    if (data == NULL)
        return -1;

    printf("mem_total: %lukB mem_free: %lukB buffers: %lukB cached: %lukB "
            "swap_total: %lukB swap_free: %lukB sreclaimable: %lukB mem_share: %lukB\n",
            data->mem_total, data->mem_free, data->buffers, data->cached,
            data->swap_total, data->swap_free, data->sreclaimable, data->mem_share);
    return 0;
}

int Print_Mem_Info(MEM_INFO *info)
{
    if (info == NULL)
        return -1;

    printf("mem_total: %ukB mem_used: %ukB mem_mfree: %ukB mem_free: %ukB buffer: %ukB "
            "cache: %ukB swap_total: %ukB swap_used: %ukB swap_free: %ukB\n",
            info->mem_total, info->mem_used, info->mem_mfree, info->mem_free, info->mem_buffer,
            info->mem_cache, info->swap_total, info->swap_used, info->swap_free);
    return 0;
}

int Print_Proc_Data(PROC_DATA *data, int count)
{
    int index = 0;

    if ((data == NULL) || (count <= 0))
        return -1;

    printf("proc_count: %d\n", count);
    for (index = 0; index < count; index++)
    {
        printf("uid: %4d pid: %5d comm: %20s state: %c ppid: %5d pgrp: %5d session: %5d tty_nr: %5d "
                "tpgid: %5d flags: %10u minflt: %7lu cminflt: %10lu majflt: %3lu cmajflt: %5lu "
                "utime: %9lu stime: %6lu cutime: %9ld cstime: %6ld priority: %5ld nice: %5ld "
                "num_threads: %5ld itrealvalue: %5ld starttime: %10llu vsize: %9lu rss: %5ld "
                "rsslim: %5lu\n",
                data[index].uid, data[index].pid, data[index].comm, data[index].state, data[index].ppid,
                data[index].pgrp, data[index].session, data[index].tty_nr, data[index].tpgid,
                data[index].flags, data[index].minflt, data[index].cminflt, data[index].majflt,
                data[index].cmajflt, data[index].utime, data[index].stime, data[index]. cutime,
                data[index].cstime, data[index].priority, data[index].nice, data[index].num_threads,
                data[index].itrealvalue, data[index].starttime, data[index].vsize, data[index].rss,
                data[index].rsslim);
    }
    return 0;
}

int Print_Proc_Info(PROC_INFO *info, int count)
{
    int index = 0;

    if ((info == NULL) || (count <= 0))
        return -1;

    printf("proc_count: %d\n", count);
    for (index = 0; index < count; index++)
    {
        printf("pid: %5d user: %10s state: %1c cpu: %8.4f%% mem: %10dK = %5s des: %15s name: %20s\n",
                info[index].pid, info[index].user, info[index].state, info[index].cpu_us,
                info[index].mem_us, info[index].mem_us_str, info[index].proc_des, info[index].proc_name);
    }
    return 0;
}

int main()
{
    SYS_RES sys_info = {0};

    Get_System_Resource(&sys_info, SRES_ALL);
    Print_CPU_Info(sys_info.cpu_info, sys_info.cpu_array_count);
    Print_Mem_Info(&sys_info.mem);
    Print_Proc_Info(sys_info.proc_info, sys_info.proc_count);
    free(sys_info.cpu_info);
    free(sys_info.proc_info);
    return 0;
}
