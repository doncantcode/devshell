"""Drives DevShell through a real pseudo-terminal: banner, prompt, readline
history, Ctrl+C. Needs Linux/WSL/MSYS2 with python3 and a readline build.

Run from the project root:   python3 tests/pty_test.py
"""
import os
import pty
import re
import select
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

home = tempfile.mkdtemp()
os.makedirs(os.path.join(home, "repo", ".git"))
with open(os.path.join(home, "repo", ".git", "HEAD"), "w") as f:
    f.write("ref: refs/heads/main\n")


def run(keys, cwd=home, delay=0.4):
    env = dict(os.environ, HOME=home, TERM="xterm",
               DEVSHELL_TEMPLATES=os.path.join(ROOT, "templates"))
    env.pop("NO_COLOR", None)
    env.pop("DEVSHELL_COLOR", None)
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(cwd)
        os.execve(os.path.join(ROOT, "devshell"), ["devshell"], env)
    out = b""

    def drain(t):
        nonlocal out
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try:
                    d = os.read(fd, 4096)
                except OSError:
                    return
                if not d:
                    return
                out += d

    drain(0.8)
    for k in keys:
        os.write(fd, k)
        drain(delay)
    drain(0.5)
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return out.decode(errors="replace")


ok = True


def check(name, cond, detail=""):
    global ok
    print(("PASS " if cond else "FAIL ") + name)
    if not cond:
        ok = False
        print(detail)


# 1. banner, coloured prompt, Up-arrow recall, history builtin and file
out = run([b"echo one\n", b"echo two\n", b"\x1b[A\n", b"history\n", b"exit\n"])
check("banner", "DevShell" in out and "small developer shell" in out, out)
check("coloured prompt", "\x1b[1;36mdevshell" in out, repr(out[:300]))
check("up-arrow recalls last command", out.count("two") >= 4, out)
check("history builtin lists commands", re.search(r"\s1\s+echo one", out) is not None, out)
with open(os.path.join(home, ".devshell_history")) as f:
    hist = f.read()
check("history file saved", "echo one" in hist and "echo two" in hist, hist)

# 2. a second session loads the saved history (last entries: echo two, history, exit)
out = run([b"\x1b[A\x1b[A\x1b[A\n", b"exit\n"])
check("history persists across sessions", out.count("two") >= 2, out)

# 3. failure status and git branch in the prompt
out = run([b"nosuchcmd_zz\n", b"exit\n"])
check("prompt shows [127]", "[127]" in out, out)
out = run([b"exit\n"], cwd=os.path.join(home, "repo"))
check("prompt shows git branch", "(main)" in out, out)

# 4. Ctrl+C at the prompt keeps the shell alive; Ctrl+D leaves it
out = run([b"echo before\n", b"\x03", b"echo after\n", b"\x04"])
check("Ctrl+C keeps shell alive", "before" in out and "after" in out, out)

print("ALL OK" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
