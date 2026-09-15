/*
 * shell.c — main shell loop
 *
 * Features implemented
 * --------------------
 *  • Custom prompt (myshell>)
 *  • Built-ins: cd, pwd, exit, help, jobs, fg
 *  • External commands via fork + execvp
 *  • Foreground / background execution
 *  • I/O redirection  (< > >>)
 *  • Pipelines        (cmd1 | cmd2 | ...)
 *  • SIGINT  — Ctrl+C kills foreground child, not the shell
 *  • SIGCHLD — WNOHANG reaping prevents zombie processes
 *  • Basic job table  (jobs, fg)
 *
 * System calls used
 * -----------------
 *  fork, execvp, waitpid, pipe, dup2, open, close,
 *  chdir, getcwd, kill, sigaction, sigemptyset
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include "parser.h"

/* ================================================================== */
/*  Job table                                                          */
/* ================================================================== */

#define MAX_JOBS 64

typedef enum { JOB_RUNNING, JOB_DONE, JOB_STOPPED } JobState;

typedef struct {
    int      id;
    pid_t    pid;
    JobState state;
    char     cmd[MAX_CMD_LEN];
} Job;

static Job   job_table[MAX_JOBS];
static int   njobs         = 0;
static int   next_job_id   = 1;

/* PID of the currently-running foreground child (0 = none) */
static volatile sig_atomic_t fg_pid = 0;

/* ================================================================== */
/*  Job helpers                                                        */
/* ================================================================== */

static Job *job_add(pid_t pid, const char *cmd)
{
    if (njobs >= MAX_JOBS) return NULL;
    Job *j     = &job_table[njobs++];
    j->id      = next_job_id++;
    j->pid     = pid;
    j->state   = JOB_RUNNING;
    strncpy(j->cmd, cmd, MAX_CMD_LEN - 1);
    j->cmd[MAX_CMD_LEN - 1] = '\0';
    return j;
}

static void job_remove(int idx)
{
    for (int i = idx; i < njobs - 1; i++)
        job_table[i] = job_table[i + 1];
    njobs--;
}

static Job *job_by_pid(pid_t pid)
{
    for (int i = 0; i < njobs; i++)
        if (job_table[i].pid == pid) return &job_table[i];
    return NULL;
}

static Job *job_by_id(int id)
{
    for (int i = 0; i < njobs; i++)
        if (job_table[i].id == id) return &job_table[i];
    return NULL;
}

/* Reap any completed background jobs (called from SIGCHLD handler
 * and before printing the prompt). */
static void reap_children(void)
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < njobs; i++) {
            if (job_table[i].pid == pid) {
                printf("[%d]+ Done\t%s\n", job_table[i].id, job_table[i].cmd);
                fflush(stdout);
                job_remove(i);
                break;
            }
        }
    }
}

/* ================================================================== */
/*  Signal handlers                                                    */
/* ================================================================== */

static void sigchld_handler(int sig)
{
    (void)sig;
    /* Only waitpid here — printf is not async-signal-safe.
     * reap_children() in the main loop prints the "Done" notice. */
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) { /* reap, don't print */ }
}

static void sigint_handler(int sig)
{
    (void)sig;
    /* Forward SIGINT to the foreground child only */
    if (fg_pid > 0)
        kill(fg_pid, SIGINT);
    /* Shell stays alive; the prompt will be reprinted in main loop */
}

static void install_signal_handlers(void)
{
    struct sigaction sa_chld, sa_int, sa_ign;

    /* SIGCHLD — reap background children */
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
    sa_chld.sa_handler = sigchld_handler;
    sigaction(SIGCHLD, &sa_chld, NULL);

    /* SIGINT — forward to foreground child */
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags   = SA_RESTART;
    sa_int.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa_int, NULL);

    /* SIGQUIT / SIGTSTP — ignore in shell process */
    sigemptyset(&sa_ign.sa_mask);
    sa_ign.sa_flags   = 0;
    sa_ign.sa_handler = SIG_IGN;
    sigaction(SIGQUIT, &sa_ign, NULL);
    sigaction(SIGTSTP, &sa_ign, NULL);
}

/* ================================================================== */
/*  I/O redirection helper (called inside child)                      */
/* ================================================================== */

static int setup_redirection(const Cmd *c)
{
    if (c->in_file) {
        int fd = open(c->in_file, O_RDONLY);
        if (fd < 0) { perror(c->in_file); return -1; }
        if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
        close(fd);
    }
    if (c->out_file) {
        int flags = O_WRONLY | O_CREAT | (c->append ? O_APPEND : O_TRUNC);
        int fd    = open(c->out_file, flags, 0644);
        if (fd < 0) { perror(c->out_file); return -1; }
        if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
        close(fd);
    }
    return 0;
}

/* ================================================================== */
/*  Execute a pipeline                                                 */
/* ================================================================== */

/*
 * Execute a single-command pipeline (no pipes).
 * Returns the child PID (caller waits if foreground).
 */
static pid_t exec_single(const Cmd *c, int background)
{
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }

    if (pid == 0) {
        /* ---- child ---- */
        /* Restore default signal disposition for children */
        signal(SIGINT,  SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        if (background) {
            /* Detach stdin so child doesn't compete for terminal */
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0 && c->in_file == NULL) {
                dup2(devnull, STDIN_FILENO);
                close(devnull);
            }
        }

        if (setup_redirection(c) < 0) _exit(1);

        execvp(c->argv[0], c->argv);
        /* execvp only returns on error */
        fprintf(stderr, "myshell: %s: %s\n", c->argv[0], strerror(errno));
        _exit(127);
    }

    return pid;
}

/*
 * Execute a multi-stage pipeline.
 * pipes[]  — array of pipe(2) fd pairs, length = (ncmds-1)
 * Returns PID of last child (the one whose exit status we care about).
 */
static pid_t exec_pipeline(const Pipeline *pl)
{
    int ncmds = pl->ncmds;
    /* pipes[i][0] = read end, pipes[i][1] = write end */
    int pipes[MAX_PIPES - 1][2];

    for (int i = 0; i < ncmds - 1; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); return -1; }
    }

    pid_t last_pid = -1;

    for (int i = 0; i < ncmds; i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return -1; }

        if (pid == 0) {
            /* ---- child i ---- */
            signal(SIGINT,  SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGCHLD, SIG_DFL);

            /* Connect stdin to previous pipe read end */
            if (i > 0) {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0) { perror("dup2"); _exit(1); }
            }
            /* Connect stdout to next pipe write end */
            if (i < ncmds - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) { perror("dup2"); _exit(1); }
            }

            /* Close all pipe fds in child */
            for (int j = 0; j < ncmds - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            /* I/O redirection (first and last stage only for < and >) */
            if (setup_redirection(&pl->cmds[i]) < 0) _exit(1);

            execvp(pl->cmds[i].argv[0], pl->cmds[i].argv);
            fprintf(stderr, "myshell: %s: %s\n", pl->cmds[i].argv[0], strerror(errno));
            _exit(127);
        }

        last_pid = pid;
    }

    /* Parent closes all pipe fds */
    for (int i = 0; i < ncmds - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    return last_pid;
}

/* ================================================================== */
/*  Built-in commands                                                  */
/* ================================================================== */

static void builtin_help(void)
{
    puts(
        "myshell — built-in commands\n"
        "  cd [dir]    Change directory (default: $HOME)\n"
        "  pwd         Print working directory\n"
        "  jobs        List background jobs\n"
        "  fg [%id]    Bring job to foreground\n"
        "  exit [n]    Exit with optional status\n"
        "  help        Show this message\n"
        "\n"
        "Operators: |  <  >  >>  &"
    );
}

static void builtin_jobs(void)
{
    reap_children();   /* flush completed jobs first */
    for (int i = 0; i < njobs; i++) {
        const char *state = (job_table[i].state == JOB_RUNNING) ? "Running" : "Done";
        printf("[%d]  %s\t%s\n", job_table[i].id, state, job_table[i].cmd);
    }
}

/* Bring job to foreground */
static void builtin_fg(const Cmd *c)
{
    Job *j = NULL;

    if (c->argc == 1) {
        /* last job */
        if (njobs > 0) j = &job_table[njobs - 1];
    } else {
        /* %id or plain id */
        char *arg = c->argv[1];
        if (*arg == '%') arg++;
        int id = atoi(arg);
        j = job_by_id(id);
    }

    if (!j) { fprintf(stderr, "myshell: fg: no such job\n"); return; }

    pid_t pid = j->pid;
    printf("%s\n", j->cmd);
    fflush(stdout);

    /* Remove from job table — it becomes foreground */
    for (int i = 0; i < njobs; i++) {
        if (job_table[i].pid == pid) { job_remove(i); break; }
    }

    fg_pid = pid;
    kill(pid, SIGCONT);

    int status;
    waitpid(pid, &status, 0);
    fg_pid = 0;
}

/*
 * Returns:
 *   1  — handled as built-in (continue the loop)
 *   0  — not a built-in
 *  -1  — exit requested
 */
static int run_builtin(const Pipeline *pl)
{
    const Cmd *c = &pl->cmds[0];
    if (c->argc == 0 || c->argv[0] == NULL) return 1;

    const char *name = c->argv[0];

    if (strcmp(name, "exit") == 0) {
        int code = (c->argc > 1) ? atoi(c->argv[1]) : 0;
        exit(code);
    }

    if (strcmp(name, "cd") == 0) {
        const char *dir = (c->argc > 1) ? c->argv[1] : getenv("HOME");
        if (!dir) dir = "/";
        if (chdir(dir) < 0) perror("cd");
        return 1;
    }

    if (strcmp(name, "pwd") == 0) {
        char buf[4096];
        if (getcwd(buf, sizeof(buf))) puts(buf);
        else perror("pwd");
        return 1;
    }

    if (strcmp(name, "help") == 0) { builtin_help(); return 1; }
    if (strcmp(name, "jobs") == 0) { builtin_jobs(); return 1; }
    if (strcmp(name, "fg")   == 0) { builtin_fg(c); return 1; }

    return 0;   /* not a built-in */
}

/* ================================================================== */
/*  Prompt helper                                                      */
/* ================================================================== */

static void print_prompt(void)
{
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) == NULL) strcpy(cwd, "?");
    printf("myshell:%s> ", cwd);
    fflush(stdout);
}

/* ================================================================== */
/*  Build a printable summary of the pipeline for job table           */
/* ================================================================== */

static void pipeline_to_str(const Pipeline *pl, char *buf, size_t sz)
{
    buf[0] = '\0';
    for (int i = 0; i < pl->ncmds; i++) {
        if (i > 0) strncat(buf, " | ", sz - strlen(buf) - 1);
        for (int j = 0; j < pl->cmds[i].argc; j++) {
            if (j > 0) strncat(buf, " ", sz - strlen(buf) - 1);
            strncat(buf, pl->cmds[i].argv[j], sz - strlen(buf) - 1);
        }
    }
}

/* ================================================================== */
/*  Main shell loop                                                    */
/* ================================================================== */

int main(void)
{
    install_signal_handlers();

    char     line[MAX_CMD_LEN];
    Pipeline pl;

    while (1) {
        reap_children();      /* flush any finished background jobs */
        print_prompt();

        if (fgets(line, sizeof(line), stdin) == NULL) {
            /* EOF (Ctrl+D) */
            printf("\n");
            break;
        }

        if (parse_line(line, &pl) < 0)
            continue;          /* empty / comment */

        /* Built-ins are only recognised for single-command (no-pipe) lines */
        if (pl.ncmds == 1 && !pl.background) {
            int r = run_builtin(&pl);
            if (r != 0) continue;   /* handled */
        }

        /* ---- external command / pipeline ---- */
        pid_t pid;
        if (pl.ncmds == 1)
            pid = exec_single(&pl.cmds[0], pl.background);
        else
            pid = exec_pipeline(&pl);

        if (pid < 0) continue;   /* fork failed */

        if (pl.background) {
            char summary[MAX_CMD_LEN];
            pipeline_to_str(&pl, summary, sizeof(summary));
            Job *j = job_add(pid, summary);
            if (j) printf("[%d] %d\n", j->id, (int)pid);
        } else {
            /* Foreground: wait for the last child */
            fg_pid = pid;
            int status;
            waitpid(pid, &status, 0);
            fg_pid = 0;

            if (WIFSIGNALED(status))
                printf("\n");   /* newline after Ctrl+C */
        }
    }

    return 0;
}
