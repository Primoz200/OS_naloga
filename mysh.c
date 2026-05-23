#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <ctype.h>
#include <signal.h>

#define SHELL_NAME  "mysh"
#define MAX_LINE    1024
#define MAX_TOKENS  32
#define MAX_PROC_PATH 4096
#define MAX_VAR_CNT 256

char line[MAX_LINE];
char* tokens[MAX_TOKENS];
int cnt;
int debugLevel = 0;
char promptStr[9] = SHELL_NAME;
int lastStatus = 0;
int background = 0;
char *input_redir = NULL;
char *output_redir = NULL;
char proc_path[MAX_PROC_PATH] = "/proc";
char injected[MAX_LINE] = "";


int var_cnt = 0;
typedef struct var {
    char name[32];
    char value[32];
} var;

var variables[MAX_VAR_CNT];

char history[100][MAX_LINE];
int history_cnt = 0;

int tokenize(char* line);
typedef int (*builtin_fn)(void); //builtin_fun = ptr na funckijo brez arg, vraca int


//================BUILTIN DECLARATIONS================================
typedef struct builtin_entry {
    char* name;
    builtin_fn fn;
    char *help;
} Builtin;

int builtin_debug(void);
int builtin_prompt(void);
int builtin_status(void);
int builtin_exit(void);
int builtin_help(void);
int builtin_print(void);
int builtin_print(void);
int builtin_echo(void);
int builtin_len(void);
int builtin_sum(void);
int builtin_calc(void);
int builtin_basename(void);
int builtin_dirname(void);
int builtin_dirch(void);
int builtin_dirwd(void);
int builtin_dirmk(void);
int builtin_dirrm(void);
int builtin_dirls(void);
int builtin_rename(void);
int builtin_unlink(void);
int builtin_remove(void);
int builtin_linkhard(void);
int builtin_linksoft(void);
int builtin_linkread(void);
int builtin_linklist(void);
int builtin_cpcat(void);
int builtin_pid(void);
int builtin_pid(void);
int builtin_uid(void);
int builtin_euid(void);
int builtin_gid(void);
int builtin_egid(void);
int builtin_sysinfo(void);
int builtin_proc(void);
int builtin_pids(void);
int builtin_pinfo(void);
int builtin_waitone(void);
int builtin_waitall(void);
int builtin_pipes(void);
int builtin_var(void);
int builtin_varlist(void);
int builtin_history(void);

Builtin builtins[] = {
    {"debug", builtin_debug, "debug [level] nastavi/pokaze debug level"},
    {"prompt", builtin_prompt, "prompt [name] nastavi poziv, ali ga izpise ce ni argumenta"},
    {"status", builtin_status, "status izpise zadnji exit status"},
    {"exit", builtin_exit, "exit [status]"},
    {"help", builtin_help, "help lists builtins"},
    {"print", builtin_print, "prints, no newline"},
    {"echo", builtin_echo, "print with newline"},
    {"len", builtin_len, "izpise skupno dolzino args"},
    {"sum", builtin_sum, "vsota args"},
    {"calc", builtin_calc, "arg1 OP arg2, op: +, -, *, /, %%"},
    {"basename", builtin_basename, "izpise ime file v dani poti"},
    {"dirname", builtin_dirname, "izpise dano pot, brez koncne datoteke"},
    {"dirch", builtin_dirch, "dirch imenik - zamenjava trenutnega delovnega imenika"},
    {"dirwd", builtin_dirwd, "dirwd [mode] - izpis trenutnega delovnega imenika"},
    {"dirmk", builtin_dirmk, "dirmk imenik - ustvari nov imenik"},
    {"dirrm", builtin_dirrm, "dirrm imenik - zbrise imenik"},
    {"dirls", builtin_dirls, "dirls [imenik] - izpis vsebine imenika"},
    {"rename", builtin_rename, "preimenuje"},
    {"unlink", builtin_unlink, "unlink"},
    {"remove", builtin_remove, "odstrani file"},
    {"linkhard", builtin_linkhard, "linkhard cilj ime - ustvari trdo povezavo"},
    {"linksoft", builtin_linksoft, "linksoft cilj ime - ustvari simbolicno povezavo"},
    {"linkread", builtin_linkread, "linkread ime - izpise cilj simbolicne povezave"},
    {"linklist", builtin_linklist, "izpise vse trde povezave v trenutnem imeniku na podano datoteko"},
    {"cpcat", builtin_cpcat, "kopira iz src v dest, in naredi cat na izvoru"},
    {"pid", builtin_pid, ""},
    {"ppid", builtin_pid, ""},
    {"uid", builtin_uid, ""},
    {"euid", builtin_euid, ""},
    {"gid", builtin_gid, ""},
    {"egid", builtin_egid, ""},
    {"sysinfo", builtin_sysinfo, ""},
    {"proc", builtin_proc, ""},
    {"pids", builtin_pids, ""},
    {"pinfo", builtin_pinfo, ""},
    {"waitone", builtin_waitone, "waitone [pid] - pocaka na otroka (ali kateregakoli)"},
    {"waitall", builtin_waitall, "waitall - pocaka na vse otroke"},
    {"pipes", builtin_pipes, ""},
    {"var", builtin_var, "var ime value new (ce je prvic deklarirana), max len imena in value = 31"},
    {"varlist", builtin_varlist, "izpise definirane spremenljivke"},
    {"history", builtin_history, "history [nr] optional nr, za izvedbo nr-tega ukaza (0-99), 0 je ponovitev trenutnega ukaza"}
};
int nr_builtins = sizeof(builtins) / sizeof(builtins[0]);


//======================================HELPERS============================================
Builtin* find_builtin(char *cmd){               //vrne kazalec na structuro, ki opisuje zahtevan builtin command
    for (int i = 0; i < nr_builtins; i++){
        if(strcmp(builtins[i].name, cmd)==0){
            return &builtins[i];
        }
    }

    return NULL;
}

int is_pid_like(char* name){
    if(*name == '\0') return 0; //false

    for(int i=0; name[i]!='\0';i++){
        if(!isdigit((char)name[i])){
            return 0;
        }
    }
    return 1;
}

int compare_ints(const void *a, const void *b) {
    int x = *(const int *)a;
    int y = *(const int *)b;

    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

void sigchld_handler(int sig){
    (void)sig;

    while(waitpid(-1, NULL, WNOHANG)>0);
}

void manageRedirects(){                    
    if(input_redir){
        int fd = open(input_redir, O_RDONLY);
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if(output_redir){
        int fd = open(output_redir, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

int dolzina_do_narekovaja(char* s){
    int res = 0;
    while(s[res+1] != '"'){
        res++;
    }
    return res;
}

int find_var(char* var){
    for(int i = 0; i < var_cnt; i++){
        if(strcmp(var, variables[i].name) == 0){
            return i;
        }
    }
    printf("No such variable: %s \n", var);
    return -1;
}


//============================BUILTINS===================================
int builtin_var(){
    if (cnt < 3) return 1;


    if(cnt == 4 && strcmp(tokens[3], "new") == 0){
        for(int i = 0; i < 32; i++){
            variables[var_cnt].name[i] = '\0';
            variables[var_cnt].value[i] = '\0';
        }

        strncpy(variables[var_cnt].name, tokens[1], 31);
        strncpy(variables[var_cnt].value, tokens[2], 31);
        var_cnt++;
        return 0;
    }

    for (int i = 0; i < var_cnt; i++){
        if (strcmp(variables[i].name, tokens[1]) == 0){
            strncpy(variables[i].value, tokens[2], 31);
            variables[i].value[31] = '\0';
            return 0;
        }
    }

    printf("Pozor: nova spremenljivka brez new keyworda\n");
    return 1;
}

int builtin_varlist(){
    for (int i = 0; i < var_cnt; i++){
        printf("%s = %s\n", variables[i].name, variables[i].value);
    }
    return 0;
}

int builtin_history(){
    if(cnt == 2){
        int i = atoi(tokens[1]);
        if(i >= history_cnt || i > 99){
            printf("Neveljavno stevilo");
            return 1;
        }
        strncpy(injected, history[(history_cnt % 100)-1-i], MAX_LINE);
        return 0;
    }


    for(int i = 0; i < history_cnt; i++){
        printf("%s", history[i]);
    }
    return 0;
}

int builtin_debug(){
    if (cnt < 2){
        printf("%d\n", debugLevel);
    }else{
        debugLevel = atoi(tokens[1]);
    }

    return 0;
}

int builtin_prompt(){
    if(cnt < 2){
        printf("%s\n", promptStr);
        return 0;
    }
    if (strlen(tokens[1]) > 8) return 1;
    strncpy(promptStr, tokens[1], 8);
    promptStr[8] = '\0';
    return 0;
}

int builtin_status(){
    int save = lastStatus;
    printf("%d\n", lastStatus);
    lastStatus = save;
    return 0;
}

int builtin_print(){
    for(int i = 1; i < cnt; i++){
        if (i>1) printf(" ");
        printf("%s", tokens[i]);
    }

    return 0;
}

int builtin_echo(){
    builtin_print();
    printf("\n");

    return 0;
}

int builtin_len(){
    int len = 0;
    for (int i = 1; i < cnt; i++){
        len += strlen(tokens[i]);
    }
    printf("%d\n", len);

    return 0;
}

int builtin_sum(){
    int sum = 0;
    for (int i = 1; i < cnt; i++){
        sum += atoi(tokens[i]);
    }
    printf("%d\n", sum);  
    
    return 0;
}

int builtin_calc(){
    int res = 0;
    if (cnt != 4){
        printf("Napacni argumenti\n");
        return 1;
    } 
    int arg1 = atoi(tokens[1]);
    int arg2 = atoi(tokens[3]);
    
    if (strcmp(tokens[2], "+")==0) printf("%d\n", arg1 + arg2);
    else if (strcmp(tokens[2], "-")==0) printf("%d\n", arg1 - arg2);
    else if (strcmp(tokens[2], "*")==0) printf("%d\n", arg1 * arg2);
    else if (strcmp(tokens[2], "/")==0) printf("%d\n", arg1 / arg2);
    else if (strcmp(tokens[2], "%")==0) printf("%d\n", arg1 % arg2);

    return 0;   
}

int builtin_basename(){
    if (cnt < 2 ) return 1;

    char* last = strrchr(tokens[1], '/');
    printf("%s\n", (last ? last + 1 : tokens[1]));

    return 0;
}

int builtin_dirname(){
    if (cnt < 2) return 1;

    char* last = strrchr(tokens[1], '/');
    printf("%.*s\n", (int)(last - tokens[1]), tokens[1]);
    
    return 0;
}

int builtin_dirch(){
    char* dir = cnt < 2 ? "/" : tokens[1];

    if(chdir(dir) != 0){
        int err = errno;
        perror("dirch");
        return err;
    }
    return 0;
}

int builtin_dirwd(){
    int mode = cnt < 2 ? 0 : (strcmp(tokens[1], "base")==0 ? 0 : 1); // 0-base, 1-full
    
    char buf[MAX_LINE];
    if (getcwd(buf, sizeof(buf))==NULL) return errno;

    if (mode == 0){
        char* last = strrchr(buf, '/');
        if(strcmp(last, "/")==0){
            printf("%s\n", last ? last : buf);
        }else {
            printf("%s\n", last ? last+1 : buf);
        }
    }else {
        printf("%s\n", buf);
    }
    return 0;
}

int builtin_dirmk(){
    if (cnt < 2) return 1;
    
    if (mkdir(tokens[1], 0755) != 0) {
        int err = errno;
        perror("dirmk");
        return err;
    }   
    return 0;
}

int builtin_dirrm(){
    if (cnt < 2) return 1;

    if (rmdir(tokens[1]) != 0) {
        int err = errno;
        perror("dirrm");
        return err;
    }
    return 0;
}

int builtin_dirls(){
    char* path = cnt < 2 ? "." : tokens[1];
    DIR* dir = opendir(path);
    if(dir == NULL) return 1;

    struct dirent *entry; 
    while((entry = readdir(dir)) != NULL){
        printf("%s  ", entry->d_name);
    }
    printf("\n"); 
    closedir(dir);
    return 0;
}

int builtin_rename(){
    if(cnt < 3) return 1;

    if (rename(tokens[1], tokens[2]) != 0){
        int err = errno;
        perror("rename");
        return err;
    }
    return 0;
}

int builtin_unlink(){
    if (cnt < 2) return 1;
    
    if (unlink(tokens[1]) != 0){
        int err = errno;
        perror("unlink");
        return err;
    }
    return 0;
}

int builtin_remove(){
    if (cnt < 2) return 1;
    
    if (remove(tokens[1]) != 0){
        int err = errno;
        perror("remove");
        return err;
    }
    return 0;
}

int builtin_linkhard(void) {
    if (cnt < 3) return 1;

    if (link(tokens[1], tokens[2]) != 0) {
        int err = errno;
        perror("linkhard");
        return err;
    }
    return 0;
}

int builtin_linksoft(void) {
    if (cnt < 3) return 1;

    if (symlink(tokens[1], tokens[2]) != 0) {
        int err = errno;
        perror("linksoft");
        return err;
    }
    return 0;
}

int builtin_linkread(void) {
    if (cnt < 2) return 1;

    char buf[MAX_LINE];
    ssize_t len = readlink(tokens[1], buf, sizeof(buf) - 1);
    if (len == -1) {
        int err = errno;
        perror("linkread");
        return err;
    }
    buf[len] = '\0';
    printf("%s\n", buf);
    return 0;
}

int builtin_linklist(void) {
    if (cnt < 2) return 1;

    struct stat target;
    if (stat(tokens[1], &target) != 0) {
        int err = errno;
        perror("linklist");
        return err;
    }

    DIR *dir = opendir(".");
    if (dir == NULL) return errno;

    struct dirent *entry;
    int first = 1;

    while ((entry = readdir(dir)) != NULL) {
        struct stat es;
        if (stat(entry->d_name, &es) != 0) continue;

        if (es.st_ino == target.st_ino && es.st_dev == target.st_dev) {
            if (!first) printf("  ");
            printf("%s", entry->d_name);
            first = 0;
        }
    }
    if (!first) printf("\n");

    closedir(dir);
    return 0;
}

int builtin_cpcat(void) {
    int src = STDIN_FILENO;
    int dst = STDOUT_FILENO;

    // source
    if (cnt > 1) {
        if (strcmp(tokens[1], "-") != 0) {
            src = open(tokens[1], O_RDONLY);
            if (src < 0) {
                int err = errno;
                perror("cpcat");
                return err;
            }
        }
    }

    // destination
    if (cnt > 2) {
        dst = open(tokens[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (dst < 0) {
            int err = errno;
            perror("cpcat");
            if (src != STDIN_FILENO) close(src);
            return err;
        }
    }

    char *buf = malloc(4096);
    if (buf == NULL) {
        int err = errno;
        if (src != STDIN_FILENO) close(src);
        if (dst != STDOUT_FILENO) close(dst);
        return err;
    }

    ssize_t n;
    while ((n = read(src, buf, 4096)) > 0) {
        write(dst, buf, n);
    }

    free(buf);
    if (src != STDIN_FILENO) close(src);
    if (dst != STDOUT_FILENO) close(dst);
    return 0;
}

int builtin_pid(){
    int pid = getpid();
    printf("%d\n", pid);
    return 0;
}

int builtin_ppid(){
    int pid = getppid();
    printf("%d\n", pid);
    return 0;
}

int builtin_uid(){
    printf("%d\n", getuid());
    return 0;
}

int builtin_euid(){
    printf("%d\n", geteuid());
    return 0;
}

int builtin_gid(){
    printf("%d\n", getgid());
    return 0;
}

int builtin_egid(){
    printf("%d\n", getegid());
    return 0;
}

int builtin_sysinfo(){
    struct utsname u;
    if(uname(&u) != 0){
        int err = errno;
        perror("uname");
        return err;
    }

    printf("Sysname: %s\n", u.sysname);
    printf("Nodename: %s\n", u.nodename);
    printf("Release: %s\n", u.release);
    printf("Version: %s\n", u.version);
    printf("Machine: %s\n", u.machine);

    return 0;
}

int builtin_proc() {
    if (cnt == 1) {
        printf("%s\n", proc_path);
        return 0;
    }

    if (cnt == 2) {
        if (access(tokens[1], F_OK | R_OK) != 0) {
            return 1;
        }

        strncpy(proc_path, tokens[1], MAX_PROC_PATH - 1);
        proc_path[MAX_PROC_PATH - 1] = '\0';
        return 0;
    }

    return 1;
}

int builtin_pids() {
    int pids[4096];
    int pid_cnt = 0;

    DIR *dir = opendir(proc_path);
    if (dir == NULL) return 1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (is_pid_like(entry->d_name)) {
            if (pid_cnt < 4096) {
                pids[pid_cnt] = atoi(entry->d_name);
                pid_cnt++;
            }
        }
    }
    closedir(dir);
    qsort(pids, pid_cnt, sizeof(int), compare_ints);

    for (int i = 0; i < pid_cnt; i++) {
        printf("%d\n", pids[i]);
    }

    return 0;
}

int builtin_pinfo(){
    printf("%5s %5s %6s %s\n", "PID", "PPID", "STANJE", "IME");
    int pids[4096];
    DIR* dir = opendir(proc_path);
    if(dir == NULL){
        return 1;
    }
    struct dirent* entry;
    int pid_cnt = 0;
    while((entry = readdir(dir))!=NULL){
        if (is_pid_like(entry->d_name)){
            pids[pid_cnt] = atoi(entry->d_name);
            pid_cnt++;
        }
    }
    closedir(dir);
    qsort(pids, pid_cnt, sizeof(int), compare_ints);

    for(int i=0; i<pid_cnt; i++){
        char path[MAX_PROC_PATH+32];
        snprintf(path, sizeof(path), "%s/%d/stat", proc_path, pids[i]);

        FILE* f = fopen(path, "r");
        if(f==NULL) continue;

        char line[4096];
        if (fgets(line, sizeof(line), f) == NULL) {
            fclose(f);
            continue;
        }

        fclose(f);
        int pid;
        int ppid;
        char state;
        char name[256];

        char *open_paren = strchr(line, '(');
        char *close_paren = strrchr(line, ')');

        if (open_paren == NULL || close_paren == NULL || close_paren <= open_paren) {
            continue;
        }

        sscanf(line, "%d", &pid);

        int name_len = close_paren - open_paren - 1;
        if (name_len >= (int)sizeof(name)) {
            name_len = sizeof(name) - 1;
        }

        strncpy(name, open_paren + 1, name_len);
        name[name_len] = '\0';

        sscanf(close_paren + 2, "%c %d", &state, &ppid);

        printf("%5d %5d %6c %s\n", pid, ppid, state, name);
    }

    return 0;
}

int builtin_exit(){
    // int code = (cnt >= 2) ? atoi(tokens[1]) : lastStatus;
    // exit(code);

    int status = lastStatus;

    if (cnt > 1) {
        char* end = NULL;
        errno = 0;

        long value = strtol((const char*)tokens[1], &end, 10);

        if (end == (char*)tokens[1] || *end != '\0' || errno == ERANGE) {
            status = -1;
        } else {
            status = (int)value;
        }
    }

    return status;
}

int builtin_help(void) {
    printf("Built-in commands:\n");
    for (int i = 0; i < nr_builtins; i++) {
        printf("  %s\n", builtins[i].help);
    }
    return 0;
}

int builtin_waitone(){
    pid_t target = -1; //katerikoli otrok
    if(cnt >= 2){
        target = (pid_t)strtol(tokens[1], NULL, 10);
    }

    sigset_t block, old;
    sigemptyset(&block);
    sigaddset(&block, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block, &old);

    int wstatus;
    if(waitpid(target, &wstatus, 0) < 0){
        int err = errno;
        if(err == ECHILD){
            lastStatus = 0;
            return 0;
        }
        perror("waitone");
        return err;
    }

    int ret;
    if (WIFEXITED(wstatus))
        ret = WEXITSTATUS(wstatus);
    else if (WIFSIGNALED(wstatus))
        ret = 128 + WTERMSIG(wstatus); 
    else
        ret = 1;

    lastStatus = ret;
    return ret;
}

int builtin_waitall(){
    int wstatus;
    while(1){
        if(waitpid(-1, &wstatus, 0) < 0){
            int err = errno;
            if(err == ECHILD){
                break;
            }
            perror("waitall");
            return err;
        }
    }
    lastStatus = 0;
    return 0;
}

int builtin_pipes(){
    int n = cnt - 1;
    if (n < 2 ) return 1;

    int pfds[n-1][2];
    for(int i = 0; i < n-1; i++){
        if(pipe(pfds[i]) < 0){
            perror("pipe");
            return errno;
        }
    }

    pid_t pids[n];

    for(int i = 0; i < n; i++){
        char stage[MAX_LINE];
        strncpy(stage, tokens[i+1], MAX_LINE);
        stage[MAX_LINE-1] = '\0';

        if((pids[i] = fork()) < 0 ){perror("fork"); return errno;}

        if(pids[i] == 0){
            if (i == 0){
                if (input_redir){
                    int fd = open(input_redir, O_RDONLY);
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
            }else{
                dup2(pfds[i-1][0], STDIN_FILENO);
            } 

            if(i == n-1){
                if (output_redir) {
                    int fd = open(output_redir, O_WRONLY | O_TRUNC | O_CREAT, 0644);
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
            }else{
                dup2(pfds[i][1], STDOUT_FILENO);
            }

            
            for(int j = 0; j < n-1; j++){
                close(pfds[j][0]);
                close(pfds[j][1]);
            }

            tokenize(stage);
            tokens[cnt] = NULL;

            Builtin* b = find_builtin(tokens[0]);
            if(b){
                _exit(b->fn());
            }else {
                execvp(tokens[0], tokens);
                perror("exec");
                exit(127);
            }
        }
    }

    for (int i = 0; i < n-1; i++) {
        close(pfds[i][0]);
        close(pfds[i][1]);
    }

    int status = 0;
    if (!background) {
        for (int i = 0; i < n; i++) {
            waitpid(pids[i], &status, 0);
        }
        lastStatus = WEXITSTATUS(status);
    } else {
        lastStatus = 0;
    }

    return lastStatus;
}

int execute_builtin(Builtin *b){
    if (debugLevel > 0){
        printf("Executing builtin '%s' in %s\n", b->name, background ? "background" : "foreground" );
    }
    if(!background){
        int saved_in = dup(STDIN_FILENO);
        int saved_out = dup(STDOUT_FILENO);

        manageRedirects();
        fflush(stdout);
        int ret = b->fn();

        fflush(stdout);
        dup2(saved_in, STDIN_FILENO);
        dup2(saved_out, STDOUT_FILENO);
        close(saved_in);
        close(saved_out);

        if(strcmp(b->name, "status") != 0){
            lastStatus = ret;
        }

        return ret;
    }

    pid_t pid = fork();

    if(pid < 0){
        int ret = errno;
        perror("execute_builtin");
        return ret;
    }else if(pid == 0){
        manageRedirects();
        int status = b->fn();
        _exit(status);
    }else {
        lastStatus = 0;
        return 0;
    }
}

int execute_external(void){
    fflush(stdin);
    pid_t pid = fork();

    if(pid == 0){
        manageRedirects();
        execvp(tokens[0], tokens);
        perror("exec");
        exit(127);
    }else if(pid > 0){
        if(background){
            lastStatus = 0;
        }
        else {
            int status;     
            waitpid(pid, &status, 0);
            lastStatus = WEXITSTATUS(status);
        }
    }else {
        perror("fork");
        return 1;
    }

    return lastStatus;
}

int tokenize(char *line){
    cnt = 0;
    char *p = line;

    while(*p != '\0'){

        if(isspace(*p)){
            p++;
            continue;
        }

        if (*p=='#') break;

        if (*p == '"'){
            p++;
            tokens[cnt] = p;
            cnt++;
            while(*p != '\0' && *p != '"'){
                p++;
            }
            *p = '\0';
            p++;
            continue;
        }

        tokens[cnt] = p;
        cnt++;
        while(*p != '\0' && !isspace(*p)) p++;
        *p = '\0';
        p++;

        
        if (tokens[cnt-1][0] == '$') {
            int a = find_var(tokens[cnt-1] + 1);
            if (a != -1) tokens[cnt-1] = variables[a].value;
        }   
    }
    return cnt;
}

void parse(){
    input_redir = NULL;
    output_redir = NULL;
    if (cnt > 0 && tokens[cnt-1][0] == '&'){
        background = 1;
        cnt--;
    }

    if (cnt > 0 && tokens[cnt-1][0] == '>'){
        output_redir = tokens[cnt-1]+1;
        cnt--;
    }
    if (cnt > 0 && tokens[cnt-1][0] == '<'){
        input_redir = tokens[cnt-1]+1;
        cnt--;
    }


    //if (input_redir) printf("Input redirect: '%s'\n", input_redir);
    //if (output_redir) printf("Output redirect: '%s'\n", output_redir);
}

void repl(int interactive){
    while (1){
        background = 0;
        if (interactive){
            printf(SHELL_NAME "> ");
            fflush(stdout);     //da se takoj pojavi
        }

        if(injected[0] != '\0'){
            strncpy(line, injected, MAX_LINE);
            printf("%s", injected);
            injected[0] = '\0';
        }  else{
            if (fgets(line, sizeof(line), stdin) == NULL){  //EOF
                if(interactive) printf("\n");
                break;
            }
        }
        
        strncpy(history[history_cnt%100], line, MAX_LINE);
        history_cnt++;

        uint32_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

        int only_ws = 1;
        for (int i = 0; line[i] != '\0'; i++){
            if (!isspace(line[i])){
                only_ws = 0;
                break;
            }
        }
        
        if (debugLevel > 0) printf("Input line: '%s'\n", line);
        if (only_ws) continue;

        tokenize(line);
        if (cnt == 0) continue;
        
        if (debugLevel > 0){
            for(int i = 0; i < cnt; i++){
                printf("Token %d: '%s'\n", i, tokens[i]);
            }
        }
        parse();
        tokens[cnt] = NULL;

        Builtin *b = find_builtin(tokens[0]);
        if (b) execute_builtin(b);
        else execute_external();
    
    }
    exit(lastStatus);
}

int main(){
    setvbuf(stdout, NULL, _IONBF, 0);

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  // da fgets ne bo prekinjen
    sigaction(SIGCHLD, &sa, NULL);


    int interactive = isatty(STDIN_FILENO);

    repl(interactive);
}