#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

// buffer size.
#define MAX_SIZE_BUFF           512
// clear pid inof.
#define MAX_CLEAR_TIME          300
// number of pid (default).
#define MAX_PID_NUM             32768
// close kernel log via daemon.
#define DEBUG_CLOSE_STREAM      "/sbin/daemon_mgr klogd.sh stop \"/etc/init.d/klogd.sh start &\""
// kill existing dd process.
#define DEBUG_KILL_KLOG_PROCESS "/bin/kill -9 `pidof dd`"
// open block dump.
#define DEBUG_OPEN_BLOCK_DUMP   "/bin/echo 1 > /proc/sys/vm/block_dump"
// redirect block dump.
#define DEBUG_TRACE_DISK_ACCESS "/bin/dd if=/proc/kmsg | /bin/grep -v tmpfs | /bin/grep -e dm -e md -e sd"
// get all of disk device.
#define GET_DISK_PATH           "/bin/cat /proc/mounts | /bin/grep -e /dev/md -e /dev/mapper -e /dev/sd | /bin/awk '{print $2}'"
// proc path.
#define PROC_PATH               "/proc"
// kernel messag path.
#define KMSG_PATH               "/proc/kmsg"
// backup proc path.
#define BACKUP_PROC_PATH        "/tmp/proc"
// get process information.
#define GET_PROC_INFO           "/bin/cat %s/%s | /bin/grep -e Name -e PPid | /bin/awk '{print $2}'"

int help_tool(void)
{
    int ret = 0;

    ret = printf("[option]:                          (Version 1.52 - 20160519)\n\
            --info [file]          Show information via lstat(file).\n\
            --file [seconds]       Search for related file (R/W within specific seconds).\n\
            --proc [pid]           Track parent process by specific pid history.\n\
            --replace [exe]        Replace path of executable file and execute result in /tmp/proc/spec.\n\
            --debug                Enter debug mode, it can show block dump continually.\n\
            --monitor [seconds]    Combine option [--file seconds] with [--proc pid] at [--debug].\n");
    return ret;
}

int print_stat_info(char *file)
{
    int ret = 0;
    struct stat stat_info = {0};

    if ((ret = lstat(file, &stat_info) < 0))
        return -1;
    printf("File type:                ");
    switch (stat_info.st_mode & S_IFMT)
    {
        case S_IFBLK:  printf("block device\n");            break;
        case S_IFCHR:  printf("character device\n");        break;
        case S_IFDIR:  printf("directory\n");               break;
        case S_IFIFO:  printf("FIFO/pipe\n");               break;
        case S_IFLNK:  printf("symlink\n");                 break;
        case S_IFREG:  printf("regular file\n");            break;
        case S_IFSOCK: printf("socket\n");                  break;
        default:       printf("unknown?\n");                break;
    }
    printf("I-node number:            %ld\n", (long) stat_info.st_ino);
    printf("Mode:                     %lo (octal)\n", (unsigned long) stat_info.st_mode);
    printf("Link count:               %ld\n", (long) stat_info.st_nlink);
    printf("Ownership:                UID=%ld   GID=%ld\n", (long) stat_info.st_uid, (long) stat_info.st_gid);
    printf("Preferred I/O block size: %ld bytes\n",(long) stat_info.st_blksize);
    printf("File size:                %lld bytes\n", (long long) stat_info.st_size);
    printf("Blocks allocated:         %lld\n", (long long) stat_info.st_blocks);
    printf("Last status change:       %s", ctime(&stat_info.st_ctime));
    printf("Last file access:         %s", ctime(&stat_info.st_atime));
    printf("Last file modification:   %s", ctime(&stat_info.st_mtime));
    return ret;
}

static int check_file_info(char *file, int seconds)
{
    int ret = 0;
    struct stat stat_info = {0};
    char clock[MAX_SIZE_BUFF] = {0};
    time_t now_time = time(&now_time);

    memset(&stat_info, 0, sizeof(struct stat));
    if ((ret = lstat(file, &stat_info) < 0))
        return -1;

    // compare ctime, atime, mtime.
    if (abs((long)now_time - (long)stat_info.st_mtime) <= seconds)
    {
        snprintf(clock, MAX_SIZE_BUFF, "%s", ctime(&stat_info.st_mtime));
        if (strrchr(clock, '\n') != NULL)
            *(strrchr(clock, '\n')) = '\0';
        printf("Last file modification:   %s    %s\n", clock, file);
    }
    else if (abs((long)now_time - (long)stat_info.st_atime) <= seconds)
    {
        snprintf(clock, MAX_SIZE_BUFF, "%s", ctime(&stat_info.st_atime));
        if (strrchr(clock, '\n') != NULL)
            *(strrchr(clock, '\n')) = '\0';
        printf("Last file access:         %s    %s\n", clock, file);
    }
    else if (abs((long)now_time - (long)stat_info.st_ctime) <= seconds)
    {
        snprintf(clock, MAX_SIZE_BUFF, "%s", ctime(&stat_info.st_ctime));
        if (strrchr(clock, '\n') != NULL)
            *(strrchr(clock, '\n')) = '\0';
        printf("Last status change:       %s    %s\n", clock, file);
    }
    return ret;
}

static int list_all_file(char *path, int seconds)
{
    int ret = 0;
    int fd = 0;
    DIR *dp = NULL;
    struct dirent *entry = NULL;
    char glue='/';

    check_file_info(path, seconds);
    // symbolic link, regular file
    if (((fd = open(path, O_NOATIME | O_NOFOLLOW)) < 0) ||
        ((dp = fdopendir(fd)) == NULL))
    {
        goto list_all_file_end;
    }

    // directory
    while((entry = readdir(dp)))
    {
        if(!strcmp(entry->d_name,"..") || !strcmp(entry->d_name,"."))
           continue;
        int pathLength = strlen(path) + strlen(entry->d_name) + 2;
        char *pathStr = (char*)malloc(sizeof(char) * pathLength);
        strcpy(pathStr, path);
        int i = strlen(pathStr);
        if(pathStr[i - 1] != glue)
        {
           pathStr[i] = glue;
           pathStr[i + 1] = '\0';
        }
        strcat(pathStr, entry->d_name);
        list_all_file(pathStr, seconds);
        free(pathStr);
    }
list_all_file_end:
    if (fd >= 0)
        close(fd);
    if (dp)
        closedir(dp);
    return ret;
}

int list_effect_file(int seconds)
{
    int ret = 0;
    FILE *fd = NULL;
    char buff[MAX_SIZE_BUFF] = {0};
    char now_clock[MAX_SIZE_BUFF] = {0};
    char old_clock[MAX_SIZE_BUFF] = {0};
    time_t now_time = time(&now_time);
    time_t old_time = ((long)now_time - (long)seconds);

    // get md, sd, mapper
    if ((fd = popen(GET_DISK_PATH, "r")) != NULL)
    {
        while (fgets(buff, MAX_SIZE_BUFF, fd))
        {
            if (strrchr(buff, '\n') != NULL)
                *(strrchr(buff, '\n')) = '\0';
            snprintf(now_clock, MAX_SIZE_BUFF, "%s", ctime(&now_time));
            if (strrchr(now_clock, '\n') != NULL)
                *(strrchr(now_clock, '\n')) = '\0';
            snprintf(old_clock, MAX_SIZE_BUFF, "%s", ctime(&old_time));

            if (strrchr(old_clock, '\n') != NULL)
                *(strrchr(old_clock, '\n')) = '\0';
            printf("** Search file ** between %s and %s at %s\n", old_clock, now_clock, buff);
            if ((ret = list_all_file(buff, seconds)) < 0)
                goto list_effect_file_end;
        }
    }
list_effect_file_end:
    if (fd)
        pclose(fd);
    return ret;
}

static int clear_pid_info(int pid_max)
{
    int ret = 0;
    int count = 0;
    char buff[MAX_SIZE_BUFF] = {0};

    for (count = 1; count <= pid_max; count++)
    {
        // /proc/#/status
        snprintf(buff, MAX_SIZE_BUFF, BACKUP_PROC_PATH"/%d", count);
        ret = open(buff, O_TRUNC);
        if (ret < 0)
            goto clear_pid_info_end;
        close(ret);
    }
    ret = 0;
clear_pid_info_end:
    return ret;

}

static int create_pid_info(int pid_max)
{
    int ret = 0;
    int count = 0;
    struct stat stat_info = {0};
    char buff[MAX_SIZE_BUFF] = {0};

    // /proc
    if (stat(BACKUP_PROC_PATH, &stat_info) == -1)
        mkdir(BACKUP_PROC_PATH, S_IRWXU | S_IRWXG | S_IRWXO);

    for (count = 1; count <= pid_max; count++)
    {
        // /proc/#/status
        snprintf(buff, MAX_SIZE_BUFF, BACKUP_PROC_PATH"/%d", count);
        ret = creat(buff, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (ret < 0)
            goto create_pid_info_end;
        close(ret);
    }
    ret = 0;
create_pid_info_end:
    return ret;
}

static int maintain_pid_info(void)
{
    DIR *dp = NULL;
    int fd_s = 0, fd_d = 0;
    struct dirent *entry = NULL;
    char buff[MAX_SIZE_BUFF/4] = {0};

    if (!(dp = opendir(PROC_PATH)))
        goto maintain_pid_info_end;

    // directory
    while((entry = readdir(dp)))
    {
        if (!isdigit(*entry->d_name))
            continue;
        strcpy(buff, PROC_PATH"/");
        strcat(buff, entry->d_name);
        strcat(buff, "/status\0");
        if ((fd_s = open(buff, O_RDONLY)) < 0)
            continue;
        strcpy(buff, BACKUP_PROC_PATH"/");
        strcat(buff, entry->d_name);
        strcat(buff, "\0");
        fd_d = open(buff, O_WRONLY);
        read(fd_s, buff, MAX_SIZE_BUFF/4);
        write(fd_d, buff, MAX_SIZE_BUFF/4);
        close(fd_s);
        close(fd_d);
    }
maintain_pid_info_end:
    if (dp)
        closedir(dp);
    return 0;
}

static int track_parent_proc(char* pid)
{
    static int flag = 0;
    int ret = -1;
    FILE *fd = NULL;
    char buff[MAX_SIZE_BUFF] = {0};

    if ((flag++) == 0)
        printf("** Track  proc ** ");

    // base
    if (atoi(pid) == 0)
    {
        printf("END\n");
        flag = 0;
        return 0;
    }

    // get current process information
    snprintf(buff, MAX_SIZE_BUFF, GET_PROC_INFO, BACKUP_PROC_PATH, pid);
    if (((fd = popen(buff, "r")) != NULL) &&
        (fgets(buff, MAX_SIZE_BUFF, fd) != NULL))
    {
        // process name
        if (strchr(buff, '\n') != NULL)
            *(strchr(buff, '\n')) = '\0';
        printf("%s (%s) -> ", buff, pid);

        // process parent
        fgets(buff, MAX_SIZE_BUFF, fd);
        if (strchr(buff, '\n') != NULL)
            *(strchr(buff, '\n')) = '\0';
        ret = track_parent_proc(buff);
    }
    else
    {
        printf("Can't get process information.\n");
        flag = 0;
    }
    if (fd)
        pclose(fd);
    return ret;
}

int track_history_proc(char *pid)
{
    int ret = 0;

    if (((ret = create_pid_info(MAX_PID_NUM)) < 0) ||
        ((ret = maintain_pid_info()) < 0) ||
        ((ret = track_parent_proc(pid)) < 0));
    return ret;
}

int replace_exe_file(char *exe_path)
{
    int ret = 0;
    struct stat stat_info = {0};
    char buff[MAX_SIZE_BUFF] = {0};
    char sh_title[] = "#!/bin/sh\n\n";
    char sh_value[] = "DEBUG_TOOL=\"/tmp/Disk_Standby_Debug\"\n";
    char sh_path[] = "DEBUG_PATH=\"/tmp/proc/spec\"\n\n";
    char sh_proc[] = "echo \"`$DEBUG_TOOL --proc $$`\" >> $DEBUG_PATH\n";
    char sh_else[] = "echo \"*** Parameter ***\" opt1= $1 opt2= $2 opt3= $3 opt4= $4 opt5= $5 opt6= $6 opt7= $7 opt8= $8 opt9= $9 >> $DEBUG_PATH\n";
    char sh_file[] = "echo \"`$DEBUG_TOOL --file 2`\" >> $DEBUG_PATH\n";
    char sh_null[] = "echo >> $DEBUG_PATH\n";

    if (lstat(exe_path, &stat_info) == -1)
    {
        printf("Non-Existed file: %s\n", exe_path);
        goto replace_exe_file_end;
    }

    snprintf(buff, MAX_SIZE_BUFF, "/tmp/%s", strrchr(exe_path, '/') + 1);
    if (stat(buff, &stat_info) >= 0)
    {
        printf("Existed file: %s\n", buff);
        goto replace_exe_file_end;
    }

    snprintf(buff, MAX_SIZE_BUFF, "/bin/cp %s /tmp", exe_path);
    system(buff);
    snprintf(buff, MAX_SIZE_BUFF, "/bin/rm %s", exe_path);
    system(buff);

    if (stat(BACKUP_PROC_PATH, &stat_info) == -1)
        mkdir(BACKUP_PROC_PATH, S_IRWXU | S_IRWXG | S_IRWXO);

    if ((ret = creat(exe_path, O_WRONLY | O_TRUNC)) >= 0)
    {
        write(ret, sh_title, strlen(sh_title));
        write(ret, sh_value, strlen(sh_value));
        write(ret, sh_path, strlen(sh_path));
        snprintf(buff, MAX_SIZE_BUFF, "/tmp/%s $1 $2 $3 $4 $5 $6 $7 $8 $9 $10 $11 $12 $13 $14 $15\n", strrchr(exe_path, '/') + 1);
        write(ret, buff, strlen(buff));
        write(ret, sh_proc, strlen(sh_proc));
        write(ret, sh_else, strlen(sh_else));
        write(ret, sh_file, strlen(sh_file));
        write(ret, sh_null, strlen(sh_null));
        close(ret);
        chmod(exe_path, S_IRWXU | S_IRWXG | S_IRWXO);
    }
replace_exe_file_end:
    return ret;
}

int open_debug_mode(void)
{
    int ret = 0;

    if (((ret = system(DEBUG_CLOSE_STREAM)) < 0) ||
        ((ret = system(DEBUG_KILL_KLOG_PROCESS)) < 0) ||
        ((ret = system(DEBUG_OPEN_BLOCK_DUMP)) < 0) ||
        ((ret = system(DEBUG_TRACE_DISK_ACCESS)) < 0))
        return ret;
    return ret;
}

int open_monitor_mode(int seconds)
{
    int ret = 0;
    FILE *fd = NULL;
    pid_t pid = getpid();
    char buff[MAX_SIZE_BUFF] = {0};
    time_t old_time = time(&old_time);
    time_t new_time = time(&new_time);
    int fstdin = 0, fstdout = 0, fstderr = 0;

    if (((ret = system(DEBUG_CLOSE_STREAM)) < 0) ||
        ((ret = system(DEBUG_KILL_KLOG_PROCESS)) < 0) ||
        ((ret = system(DEBUG_OPEN_BLOCK_DUMP)) < 0) ||
        ((ret = create_pid_info(MAX_PID_NUM)) < 0) ||
        ((ret = maintain_pid_info()) < 0) ||
        ((fd = fopen(KMSG_PATH, "r")) == NULL))
        return ret;

    if (fork())
    {
        while (1)
        {
            while (fgets(buff, MAX_SIZE_BUFF, fd) != NULL)
            {
                if ((strstr(buff, "tmpfs") == NULL) &&
                    ((strstr(buff, "md") != NULL) ||
                    (strstr(buff, "dm") != NULL) ||
                    (strstr(buff, "sd") != NULL)))
                {
                    // --deubg message
                    printf("%s", buff);

                    // --proc command
                    if (strchr(buff, ')') != NULL)
                        *(strchr(buff, ')')) = '\0';
                    track_parent_proc(strchr(buff, '(') + 1);

                    // --file command
                    list_effect_file(seconds);
                    printf("**************************------------------------*****------------------------******************************\n");
                }
                new_time = time(&new_time);
                if (((long)new_time - (long)old_time) >= MAX_CLEAR_TIME)
                {
                    clear_pid_info(MAX_PID_NUM);
                    old_time = new_time;
                }
            }
        }
    }
    else
    {
        // redirection stdin/stdout/stderr
        close(0);
        close(1);
        close(2);
        if (((fstdin = open("/dev/null", O_RDWR)) < 0) ||
            ((fstdout = open("/dev/null", O_RDWR)) < 0) ||
            ((fstderr = open("/dev/null", O_RDWR)) < 0))
                goto open_monitor_mode_end;
        while (!kill(pid, 0))
            maintain_pid_info();
open_monitor_mode_end:
        if (fstdin >= 0)
            close(fstdin);
        if (fstdout >= 0)
            close(fstdout);
        if (fstderr >= 0)
            close(fstderr);
        exit(0);
    }
    if (fd)
        pclose(fd);
    return ret;
}

/** main
 *                                                    __----~~~~~~~~~~~------___
 *                                   .  .   ~~//====......          __--~ ~~
 *                   -.            \_|//     |||\\  ~~~~~~::::... /~
 *                ___-==_       _-~o~  \/    |||  \\            _/~~-
 *        __---~~~.==~||\=_    -_--~/_-~|-   |\\   \\        _/~
 *    _-~~     .=~    |  \\-_    '-~7  /-   /  ||    \      /
 *  .~       .~       |   \\ -_    /  /-   /   ||      \   /
 * /  ____  /         |     \\ ~-_/  /|- _/   .||       \ /
 * |~~    ~~|--~~~~--_ \     ~==-/   | \~--===~~        .\
 *          '         ~-|      /|2016|-~\~~       __--~~
 *                      |-~~-_/ |QNAP|   ~\_   _-~            /\
 *                           /  \ BUG \__   \/~                \__
 *                       _--~ _/ | .-~~____--~-/                  ~~==.
 *                      ((->/~   '.|||' -_|    ~~-/ ,              . _||
 *                                 -_     ~\      ~~---l__i__i__i--~~_/
 *                                 _-~-__   ~)  \--______________--~~
 *                               //.-~~~-~_--~- |-------~~~~~~~~
 *                                      //.-~~~--\
 */
int main(int argc, char *argv[])
{
    int ret = 0;

    if (!strcmp("--help", argv[1]))
        ret = help_tool();
    else if (!strcmp("--info", argv[1]))
        ret = print_stat_info(argv[2]);
    else if (!strcmp("--file", argv[1]))
        ret = list_effect_file(atoi(argv[2]));
    else if (!strcmp("--proc", argv[1]))
        ret = track_history_proc(argv[2]);
    else if (!strcmp("--replace", argv[1]))
        ret = replace_exe_file(argv[2]);
    else if (!strcmp("--debug", argv[1]))
        ret = open_debug_mode();
    else if (!strcmp("--monitor", argv[1]))
        ret = open_monitor_mode(atoi(argv[2]));
    return ret;
}
