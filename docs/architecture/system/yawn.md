# YAWN Init and Service Management

YAWN stands for "Yet Another Workload Navigator" officially, and "You Awake? Well, Now what?" unofficially (or officially, depending on who you ask). It is the init system for BoredOS, running as PID 1.

YAWN bootstraps userspace, manages login terminals defined in `/etc/ttys`, executes the `/etc/rc` boot scripts, and handles system shutdown through `/etc/rc.shutdown`. The service structure is inspired by BSD rc systems, using shell scripts in `/etc/rc.d` configured through `/etc/rc.conf`.

---

## How It Works

### Boot and Spawning

The bootloader passes the init binary path on the kernel command line:

```text
init=/bin/yawn.elf
```

When the kernel finishes hardware setup, it spawns `/bin/yawn.elf` as PID 1 with stdin, stdout, and stderr connected to `/dev/console`.

If `/bin/yawn.elf` fails to spawn, the kernel attempts to spawn `/bin/bsh.elf` as an emergency shell.

### Startup Sequence

When PID 1 starts:

1. It creates standard runtime directories if they do not exist: `/tmp`, `/var`, `/var/run`, `/var/log`, and `/etc/rc.d`.
2. It opens `/var/log/yawn.log` and prints the startup banner:
   ```text
   [yawn] BoredOS YAWN (PID 1) started. You Awake? Well, Now what?
   ```
3. It loads `/etc/rc.conf` for service settings and `/etc/ttys` for terminal definitions.
4. It runs `/etc/rc` synchronously via `/bin/bsh.elf` to execute startup scripts and launch services.
5. It captures the console boot log and redirects its stdout and stderr to `/var/log/yawn.log` so all init and service output is recorded.
6. It clears the screen and starts configured login terminals.

### Terminal Management

YAWN parses `/etc/ttys` and handles terminals based on their configured mode:

- `respawn`: YAWN starts the terminal immediately. When the child process exits, YAWN reaps it and starts a new one.
- `lazy`: Used for virtual consoles `tty2` through `tty10`. YAWN does not spawn the shell until the user switches to that console on the screen.
- `off`: The terminal entry is ignored.


### Process Reaping

YAWN continuously calls `sys_waitpid(-1, &status, WNOHANG)` in its main loop. Any child process orphaned by its parent is adopted by PID 1 and reaped when it exits, preventing zombie processes.

### Shutdown Sequence

When a shutdown or reboot is triggered:

1. YAWN sets an internal shutdown flag and ignores termination signals.
2. It redirects its output back to `/dev/console`.
3. It runs `/etc/rc.shutdown` to stop running daemons, unmount filesystems, and flush disk caches.
4. It sends `SIGTERM` to all remaining processes and waits up to 1.5 seconds for them to exit.
5. It calls the kernel reboot or poweroff syscall.

---

## Configuration Files

### /etc/rc.conf

This file contains system-wide configuration variables read by `/etc/rc` and `/etc/rc.subr`. It uses standard shell assignment syntax:

```sh
# System identity
hostname="boredos"
hostname_enable="YES"
timezone="Europe/Amsterdam"
cleanvar_enable="YES"
motd_enable="YES"

# Network
ifconfig_auto="DHCP"
ifconfig_lo0="inet 127.0.0.1"
resolv_enable="YES"
nameserver="1.1.1.1"

# Daemons
klogd_enable="YES"
ntp_enable="YES"
ntp_server="pool.ntp.org"
httpd_enable="NO"
httpd_port="80"
httpd_root="/Library/WebServer/Documents"
nova_enable="NO"
```

Variables ending in `_enable` should be set to `"YES"` or `"NO"`.

### /etc/rc.subr

This is a shell script sourced by service scripts in `/etc/rc.d`. It loads default values and reads `/etc/rc.conf` so scripts do not need to parse configuration manually.

### /etc/ttys

This file defines the terminals YAWN should manage. Lines starting with `#` are comments. Each entry has five fields:

```text
# name    command                 type      status       mode
tty1      "/bin/bsh.elf 1"        ansi      on           respawn
tty2      "/bin/bsh.elf 2"        ansi      on           lazy
tty3      "/bin/bsh.elf 3"        ansi      on           lazy
tty4      "/bin/bsh.elf 4"        ansi      on           lazy
tty5      "/bin/bsh.elf 5"        ansi      off          off
tty6      "/bin/bsh.elf 6"        ansi      off          off
tty7      "/bin/bsh.elf 7"        ansi      off          off
tty8      "/bin/bsh.elf 8"        ansi      off          off
tty9      "/bin/bsh.elf 9"        ansi      off          off
tty10     "/bin/bsh.elf 10"       ansi      off          off
ttyS0     "/bin/bsh.elf 11"       vt100     off          off
ttyS1     "/bin/bsh.elf 12"       vt100     off          off
ttyS2     "/bin/bsh.elf 13"       vt100     off          off
ttyS3     "/bin/bsh.elf 14"       vt100     off          off
```

Field meanings:
- `name`: Terminal device name (`tty1` through `tty10` for virtual consoles, `ttyS0` through `ttyS3` for serial ports). The numeric `tty_id` is automatically derived from the device name.
- `command`: Command to run for that terminal (enclosed in quotes if it contains arguments).
- `type`: Terminal type string passed to the session (`ansi`, `vt100`, etc.).
- `status`: `on` to enable, `off` to disable.
- `mode`: `respawn`, `lazy`, or `off`.

---

## Writing Services

Services live in `/etc/rc.d/` as executable scripts.

### Requirements

A service script should:
1. Source `/etc/rc.subr`.
2. Check `$1` for the action (`start`, `stop`, `restart`, `status`, `forcestart`). Default to `status` if empty.
3. Check `<name>_enable` before starting under normal `start`.
4. Ignore `<name>_enable` under `forcestart`.
5. Store its background PID in `/var/run/<name>.pid`.
6. Remove its PID file when stopped.

### Example Service: /etc/rc.d/example

```sh
. /etc/rc.subr

action="$1"
if [ -z "$action" ]; then
    action="status"
fi

if [ "$action" = "start" ]; then
    if [ "$example_enable" = "YES" ] || [ "$example_enable" = "yes" ]; then
        if [ -f /var/run/example.pid ]; then
            read pid < /var/run/example.pid
            if [ -n "$pid" ] && [ -d /proc/$pid ]; then
                echo "example is already running (PID $pid)."
                exit 0
            fi
        fi

        echo "Starting example daemon..."
        /bin/example.elf &
        echo $! > /var/run/example.pid
        echo "example started with PID $!."
    fi
elif [ "$action" = "forcestart" ]; then
    echo "Starting example daemon..."
    /bin/example.elf &
    echo $! > /var/run/example.pid
    echo "example started with PID $!."
elif [ "$action" = "stop" ]; then
    if [ -f /var/run/example.pid ]; then
        read pid < /var/run/example.pid
        if [ -n "$pid" ]; then
            echo "Stopping example (PID $pid)..."
            kill "$pid"
            rm -f /var/run/example.pid
            echo "example stopped."
        fi
    else
        echo "example is not running."
    fi
elif [ "$action" = "restart" ]; then
    /etc/rc.d/example stop
    /etc/rc.d/example start
elif [ "$action" = "status" ]; then
    if [ -f /var/run/example.pid ]; then
        read pid < /var/run/example.pid
        if [ -n "$pid" ] && [ -d /proc/$pid ]; then
            echo "example is running (PID $pid)."
            exit 0
        fi
    fi
    echo "example is not running."
fi
```

To enable this service at boot, add this line to `/etc/rc.conf`:

```sh
example_enable="YES"
```

And call `/etc/rc.d/example start` in `/etc/rc` or `/etc/rc.local`.

---

## Runtime Management

### Using the service Command

The `service` command controls scripts in `/etc/rc.d`:

```sh
service httpd status
service httpd start
service httpd forcestart
service httpd stop
service httpd restart
service -l
service -e
```

Flags:
- `-l`: Lists all service scripts in `/etc/rc.d`.
- `-e`: Lists services that are currently enabled in `/etc/rc.conf`.

### Using the yawn Command

When run with a PID other than 1, `/bin/yawn.elf` is a client tool that sends signals to PID 1:

```sh
yawn status    # Prints whether PID 1 is active
yawn reload    # Sends SIGHUP to reload /etc/ttys and /etc/rc.conf
yawn q         # Alias for reload
yawn reboot    # Sends SIGUSR1 to reboot
yawn 6         # Alias for reboot
yawn shutdown  # Sends SIGUSR2 to power off
yawn 0         # Alias for shutdown
```

---

## Signals Handled by PID 1

| Signal | Number | Action |
| :--- | :--- | :--- |
| SIGHUP | 1 | Reloads `/etc/ttys` and `/etc/rc.conf`. Starts new terminals and stops disabled ones. |
| SIGUSR1 | 10 | Runs `/etc/rc.shutdown` and reboots the machine. |
| SIGUSR2 | 12 | Runs `/etc/rc.shutdown` and powers off the machine. |
| SIGWINCH | 28 | Checks whether active virtual console changed, spawning lazy terminals if needed. |
| SIGTERM | 15 | Ignored to protect PID 1. |
| SIGINT | 2 | Ignored to protect PID 1. |
