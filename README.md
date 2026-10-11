# DevShell

A small developer-oriented Unix shell written in C. It demonstrates lexical
and syntactic analysis, process creation (`fork`/`execvp`/`waitpid`), pipes,
I/O redirection and background jobs, plus a file-operations API and project
scaffolding.

## Build and run

Requires Linux or WSL2 (Ubuntu), `gcc` and `make`:

```bash
sudo apt install build-essential gdb valgrind git make
make
./devshell
```

`make test` runs the unit tests and the end-to-end script.
`make valgrind` checks for leaks. See [docs/grammar.md](docs/grammar.md).

### Command history and line editing (optional)

History, Up/Down recall and line editing use GNU readline. The Makefile
detects it automatically:

```bash
sudo apt install libreadline-dev        # Ubuntu / WSL
pacman -S libreadline-devel             # MSYS2
make clean && make                      # rebuild so it is picked up
```

Without it DevShell still builds and works, just without history.
`python3 tests/pty_test.py` exercises the interactive parts (prompt, history,
Ctrl+C) through a real pseudo-terminal.

### Startup options

```
./devshell        quick-start banner, prompt, history
./devshell -q     no banner  (or DEVSHELL_QUIET=1)
./devshell -i     act interactively even when input is piped
```

New to shells? Type `tour`: it explains one idea per step (commands, pipes,
redirection, variables, `&&`/`||`, trace mode), waits for you to type the
suggested command, and really runs it. It works in a temporary folder that
is deleted afterwards, and restores the directory, variables and trace
setting when it ends. Say `skip` to move on or `quit` to leave.

### Appearance

```
devshell ~/projects/app (main) [1]>
         │              │       └ exit status of the last command, when not 0
         │              └ git branch (read from .git/HEAD; no git needed)
         └ working directory, ~ for home
```

Colour is only used on a terminal. `NO_COLOR=1` turns it off;
`DEVSHELL_COLOR=always|never` forces it either way. History is kept in
`~/.devshell_history`.

## Features

| Feature            | Example                                   |
|--------------------|-------------------------------------------|
| External programs  | `gcc -o hello hello.c`, `git status`      |
| Pipes              | `ls \| wc -l`                             |
| Redirection        | `sort < in.txt > out.txt`, `echo hi >> log` |
| Background jobs    | `sleep 5 &`                               |
| Command lists      | `make ; ./app`, `make && ./app`, `cmd \|\| echo failed` |
| Variables          | `NAME=value`, `export NAME`, `unset`, `$NAME`, `${NAME}`, `$?`, `$$` |
| Trace mode         | `trace on` shows lex, parse, expand, fork/exec/wait steps |
| Built-ins          | `cd`, `pwd`, `echo`, `clear`, `help`, `exit`, `export`, `unset`, `set`, `trace` |
| File operations    | `ls [-a] [-l] [-h]` (coloured by type), `dir`, `mkdir [-p]`, `touch`, `cat`, `rm [-r] [-f]` |
| Help               | `help` (grouped), `help <command>` (usage + example), `history` |
| Learning           | `tour` (guided, 11 steps, runs in a scratch folder), `welcome` (quick-start screen) |
| Scaffolding        | `new c-project hello`, `new python-project tool` |

Templates live in `templates/` and are found next to the executable, or via
`$DEVSHELL_TEMPLATES`. `{{NAME}}` in template files is replaced by the
project name.

## Layout

```
include/   headers          src/        implementation
templates/ scaffold files   tests/      unit + end-to-end tests
docs/      grammar          build/      objects (git-ignored)
```

## Try it

```
devshell> X=hello
devshell> echo $X
hello
devshell> env | grep ^X=            # shell variable: children cannot see it
devshell> export X
devshell> env | grep ^X=
X=hello
devshell> false && echo no || echo yes
yes
devshell> trace on
devshell> echo hi | tr a-z A-Z
```

Variables, the `&&`/`||` rules and the trace format are described in
[docs/grammar.md](docs/grammar.md).

## Not (yet) supported

Word splitting and globbing (`*.c`), `A=1 cmd` per-command environment,
`2>` / `2>&1`, backgrounding an `&&`/`||` chain, subshells `( ... )`,
here-docs, job control (`fg`/`bg`), history/line editing (`libreadline`
would add this).
