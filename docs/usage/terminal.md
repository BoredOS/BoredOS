# Terminal & Command Line

The BoredOS Terminal provides a powerful command-line interface (CLI) for advanced users and developers. It supports standard Unix-like features and provides direct access to the kernel's system calls.

## The Shell

The default shell in BoredOS is **BoredShell (Bsh)**, a userspace shell with a dedicated terminal app. It features:
-   **ANSI Color Support**: Rich text output with colors and styles.
-   **Command History**: Use the **Up** and **Down** arrow keys to navigate through your previous commands (up to 64 history entries).
-   **Redirection**:
    -   `command > file`: Write output to a new file (or overwrite existing).
    -   `command >> file`: Append output to an existing file.
    -   `command < file`: Use a file as command input.
-   **Piping**:
    -   `command1 | command2`: Pass the output of the first command as input to the second.
-   **Command Chaining**:
    -   `cmd1 ; cmd2`: Run `cmd2` after `cmd1`.
    -   `cmd1 && cmd2`: Run `cmd2` only if `cmd1` succeeds.
    -   `cmd1 || cmd2`: Run `cmd2` only if `cmd1` fails.
-   **Background Launch**:
    -   `command &`: Start command without blocking the prompt.

## Shell Operators & Scripting

Bsh provides a suite of operators for managing I/O and controlling execution flow.

### I/O Redirection

Redirection allows you to change where a command reads its input from or writes its output to by manipulating kernel-level file descriptors (FDs).

| Operator | Action | Description |
| :--- | :--- | :--- |
| `>` | **Overwrite** | Redirects standard output (FD 1) to a file, creating it if it doesn't exist or truncating it if it does. |
| `>>` | **Append** | Redirects standard output (FD 1) to a file, appending new data to the end without clearing existing content. |
| `<` | **Input** | Redirects standard input (FD 0) to read from a file instead of the terminal. |

**Example:**
```bash
echo "Hello World" > greetings.txt
echo "Second line" >> greetings.txt
cat < greetings.txt
```

**How it works:**
The shell uses `sys_open` to obtain a handle for the target file, then uses `sys_dup2` to replace the process's standard FD (0 or 1) with the new file handle. This ensures the command's standard library calls (like `printf` or `scanf`) interact with the file instead of the terminal.

### Pipelines (`|`)

Pipelines connect the output of one command directly to the input of another.

**Usage:** `cmd1 | cmd2 | cmd3`

**Example:**
```bash
ls /bin | cat | cat
```

**How it works:**
The shell creates an anonymous pipe using `sys_pipe`, which returns two FDs: a read end and a write end.
- The shell duplicates the **write end** to FD 1 for the first command.
- The shell duplicates the **read end** to FD 0 for the second command.
- Both commands run in parallel, and the kernel manages the data buffer between them.

### Execution Control

| Operator | Name | Description |
| :--- | :--- | :--- |
| `;` | **Semicolon** | Executes commands sequentially. `cmd2` runs only after `cmd1` finishes. |
| `&&` | **Logical AND** | Executes `cmd2` only if `cmd1` returns a success status (exit code 0). |
| `||` | **Logical OR** | Executes `cmd2` only if `cmd1` fails (returns a non-zero exit code). |

**Example:**
```bash
# Compile and run only on success
make && ./boredos.elf

# Recover from a missing command
missing_tool || echo "Tool not found, using fallback"
```

### Background Execution (`&`)

Appending `&` to a command tells the shell not to wait for the process to complete before returning to the prompt.

**How it works:**
Normally, the shell calls `sys_waitpid` to block until a child process exits. With `&`, the shell skips this wait, allowing the process to run asynchronously while you continue using the terminal.

Operator precedence follows common POSIX shell rules:
1. Redirection and pipelines (`<`, `>`, `>>`, `|`)
2. Conditionals (`&&`, `||`)
3. List separators (`;`, `&`)

### Bsh Configuration

Bsh loads its configuration from:

`/Library/bsh/bshrc`

This file is similar to `.zshrc` or `.bashrc` and can define:
- `PATH` for command lookup
- `STARTUP` for interactive shell startup scripts
- `BOOT_SCRIPT` for a once-per-boot script
- prompt templates (`PROMPT_LEFT`, `PROMPT_RIGHT`)

Prompt tokens:
- `%n` username
- `%h` hostname
- `%~` cwd ("~" for `/root`)
- `%T` time (HH:MM)

Example:

```
PATH=/bin:/root/Apps
PROMPT_LEFT=%n@%h:%~$ 
STARTUP=/Library/bsh/startup.bsh
BOOT_SCRIPT=/Library/bsh/boot.bsh
```

## Common Commands

Below are some of the most used commands available in `/bin`:

| Command | Description |
| :--- | :--- |
| `ls` | List files and directories in the current path. |
| `cd` | Change the current working directory. |
| `cat` | Display the contents of a file. |
| `rm` | Remove a file. |
| `mkdir` | Create a new directory. |
| `man` | View the manual for a specific command (e.g., `man ls`). |
| `lsblk` | List block devices and partitions. |
| `du` | Report disk usage for files and directories. |
| `sysfetch` | Display system information and BoredOS branding. |
| `time` |  Measure command execution time. |


---
[Return to Documentation Index](../README.md)
