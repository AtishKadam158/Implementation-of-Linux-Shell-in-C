# myshell — Linux Shell in C

A Unix shell implementation written in C that demonstrates process management,
signal handling, I/O redirection, pipelines, and job control.

---

## Architecture

```
stdin ──► fgets() ──► parse_line() ──► Pipeline
                                          │
                          ┌───────────────┤
                          ▼               ▼
                     built-ins     fork + execvp
                  (cd pwd jobs     (external cmds)
                   fg exit help)
                                          │
                          ┌───────────────┤
                          ▼               ▼
                     foreground      background
                     waitpid()       job table
```

### Source files

| File | Purpose |
|---|---|
| `shell.c` | Main loop, signal handlers, job table, built-ins, fork/exec |
| `parser.c` | Tokenises input into `Pipeline` / `Cmd` structs |
| `parser.h` | Shared type definitions and `parse_line()` prototype |
| `Makefile` | Build rules |

---

## System Calls Reference

| Call | Where used |
|---|---|
| `fork()` | Create child process for every external command |
| `execvp()` | Replace child image with the requested program |
| `waitpid()` | Block on foreground child; WNOHANG-reap background children |
| `pipe()` | Create unidirectional byte channels between pipeline stages |
| `dup2()` | Wire stdin/stdout to pipe ends or redirect files |
| `open()` | Open files for `<`, `>`, `>>` redirection |
| `close()` | Close unused pipe / file descriptors |
| `chdir()` | Implement `cd` built-in |
| `getcwd()` | Implement `pwd` built-in and display in prompt |
| `kill()` | Forward SIGINT to foreground child; resume stopped jobs |
| `sigaction()` | Register SIGINT and SIGCHLD handlers |
| `sigemptyset()` | Initialise signal mask for `sigaction` |

---

## Signal Flow

```
Ctrl+C → SIGINT → sigint_handler()
                       │
                  fg_pid != 0?
                  yes → kill(fg_pid, SIGINT)   # child dies
                  no  → nothing                # shell stays alive

child exits → SIGCHLD → sigchld_handler()
                              │
                        waitpid(-1, WNOHANG)   # reap zombie
                              │
                        remove from job table, print "Done"
```

---

## How to Run

> This project requires a **Linux environment**.
> On Windows, use **WSL (Windows Subsystem for Linux)** with Ubuntu.

---

### Case 1 — Fresh Start (first time ever)

#### Step 1 — Install WSL + Ubuntu (Windows only, run in PowerShell as Admin)

```powershell
wsl --install
```

Restart Windows when prompted. Then open **Ubuntu** from the Start Menu.

#### Step 2 — Install build tools (inside Ubuntu, one-time only)

```bash
sudo apt update && sudo apt install -y build-essential
```

#### Step 3 — Navigate to the project

```bash
cd "/mnt/d/Project - System/Implementation-of-Linux-Shell-in-C"
```

> Replace `d` with your actual drive letter if different.

#### Step 4 — Compile

```bash
make
```

Expected output:
```
gcc -Wall -Wextra -pedantic -std=c11 -g -c -o shell.o shell.c
gcc -Wall -Wextra -pedantic -std=c11 -g -c -o parser.o parser.c
gcc -Wall -Wextra -pedantic -std=c11 -g -o myshell shell.o parser.o
```

#### Step 5 — Run

```bash
./myshell
```

You will see:
```
myshell:/mnt/d/Project - System/Implementation-of-Linux-Shell-in-C>
```

---

### Case 2 — Already Installed (WSL + build-essential done before)

Open **Ubuntu** from the Start Menu, then:

```bash
cd "/mnt/d/Project - System/Implementation-of-Linux-Shell-in-C"
make
./myshell
```

That's it — no setup needed.

---

### Rebuild from scratch

```bash
make clean   # removes myshell, *.o files
make         # recompiles everything
./myshell
```

---

## Usage

```bash
./myshell
```

### Built-in commands

| Command | Description |
|---|---|
| `cd [dir]` | Change directory (defaults to `$HOME`) |
| `pwd` | Print current directory |
| `jobs` | List background jobs |
| `fg [%id]` | Bring a job to the foreground |
| `exit [n]` | Exit with optional status code |
| `help` | Print built-in command reference |

### Operators

| Operator | Meaning |
|---|---|
| `cmd &` | Run in background |
| `cmd1 \| cmd2` | Pipe stdout of cmd1 into stdin of cmd2 |
| `cmd > file` | Redirect stdout (truncate) |
| `cmd >> file` | Redirect stdout (append) |
| `cmd < file` | Redirect stdin |

---

## Test Sequence

```bash
ls
ls -l
pwd
cd ..
sleep 5
sleep 10 &        # background — note printed job ID
# press Ctrl+C    # kills foreground only
ls > output.txt
cat < output.txt
ls | grep .c
jobs
exit
```

---

## Resume-level Scope

```
fork()  ·  execvp()  ·  waitpid()  ·  signal handling (SIGINT, SIGCHLD)
foreground/background processes  ·  I/O redirection  ·  pipes
built-in commands  ·  basic job management (jobs, fg)
```
