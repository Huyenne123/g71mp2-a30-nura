#!/usr/bin/env python3
import sys
import subprocess
import shlex

def run_remote(command, sudo=False):
    if sudo:
        remote_cmd = f"doas sh -c {shlex.quote(command)}"
    else:
        remote_cmd = command

    ssh_args = [
        "ssh",
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=/dev/null",
        "-o", "LogLevel=ERROR",
        "user@172.16.42.1",
        remote_cmd
    ]
    proc = subprocess.run(ssh_args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if proc.stdout:
        print(proc.stdout.strip())
    return proc.returncode

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: remote_exec.py [--sudo] <command>", file=sys.stderr)
        sys.exit(1)
    
    sudo = False
    args = sys.argv[1:]
    if args[0] == "--sudo":
        sudo = True
        args = args[1:]
    
    cmd = " ".join(args)
    rc = run_remote(cmd, sudo=sudo)
    sys.exit(rc)
