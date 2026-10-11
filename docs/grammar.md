# DevShell grammar and phases

Every input line goes through these phases:

```
 line ─► LEXER ─► PARSER ─► ┌─ for each pipeline, in order ─┐
         tokens   CommandList│  EXPAND ─► EXECUTE            │
                             └───────────────────────────────┘
```

Expansion runs **per pipeline, just before it executes**, not once at the
start of the line. That is what makes `X=1; echo $X` print `1`: the
assignment has already run when the second pipeline is expanded.

Run `trace on` to watch each phase print what it did.

## 1. Lexical analysis (src/lexer.c)

Input is split into tokens:

| Token        | Text | Notes                                         |
|--------------|------|-----------------------------------------------|
| `TOK_WORD`   | any  | runs of non-space, non-operator characters    |
| `TOK_PIPE`   | `\|` |                                               |
| `TOK_IN`     | `<`  |                                               |
| `TOK_OUT`    | `>`  |                                               |
| `TOK_APPEND` | `>>` |                                               |
| `TOK_BG`     | `&`  |                                               |
| `TOK_SEMI`   | `;`  |                                               |
| `TOK_AND`    | `&&` | longest match: `&&` is one token, not two `&` |
| `TOK_OR`     | `\|\|` | longest match, as above                     |

Quoting decides where a word ends: inside `'...'` and `"..."` whitespace and
operators are ordinary characters, and `\x` makes `x` ordinary. Quoted
sections glue onto adjacent text (`a"b c"d` is one word). An unmatched quote
is a syntax error.

**Words are kept raw.** The lexer does not remove quotes or touch `$`; that
is the expansion phase's job, so it can see variables set earlier on the
same line.

## 2. Syntactic analysis (src/parser.c)

```
list       → and_or ( ( ";" | "&" ) and_or )* [ ";" | "&" ]
and_or     → pipeline ( ( "&&" | "||" ) pipeline )*
pipeline   → command ( "|" command )*
command    → WORD ( WORD | redirect )*
redirect   → ( "<" | ">" | ">>" ) WORD
```

Precedence, loosest to tightest: `;` and `&`, then `&&` and `||`, then `|`.
So `a | b && c | d ; e` is `((a | b) && (c | d)) ; e`.

`&&` and `||` have equal precedence and group left to right, as in POSIX
shells:

```
false && echo a || echo b      →  (false && echo a) || echo b   → prints b
true  || echo a && echo b      →  (true  || echo a) && echo b   → prints b
```

A pipeline that is skipped leaves the previous exit status in place, which
is what lets the second example reach `echo b`.

`&` ends a pipeline and starts it in the background; the shell continues
with the next one. A `&` directly after an `&&`/`||` chain is a syntax
error, because backgrounding the whole chain needs a sub-shell, which
DevShell does not have.

Syntax errors: missing command before or after `|`, `&&`, `||`, `;`, `&`;
missing file after a redirect; `&` after `&&`/`||`; unmatched quote; more
than 62 arguments, 16 pipeline stages, 64 pipelines or 256 tokens.

## 3. Expansion (src/expand.c, src/vars.c)

Each raw word becomes one final argument:

| Form                   | Result                                            |
|------------------------|---------------------------------------------------|
| `$NAME`, `${NAME}`     | the variable's value, or empty if unset           |
| `$?`                   | exit status of the previous command               |
| `$$`                   | the shell's process id                            |
| `'text'`               | literal; nothing is expanded                      |
| `"text"`               | literal, but `$` is still expanded                |
| `\c`                   | the character `c`, literally (`\$` gives `$`)     |

Rules worth knowing:

* There is **no word splitting and no globbing**: one raw word is always one
  argument, even if its value contains spaces.
* An *unquoted* word that expands to nothing is removed
  (`echo $UNSET x` prints `x`); a quoted one stays as an empty argument.
* A redirect target that expands to nothing is an `ambiguous redirect` error.
* A `$` that cannot start a name (`$`, `$-`, `5$`) is left as it is.

### Assignment and scoping

A command made only of `NAME=value` words sets variables:

```
X=1            shell variable   $X works, programs started later do not see it
export X       now also placed in the environment, so children inherit it
export Y=2     assign and export in one step
unset X        remove it (also from the environment)
set            list every variable;  export  lists the exported ones
```

Implementation: all variables live in one table (`vars.c`) with an
*exported* flag. Only exported variables are mirrored into the process
environment, which is what `execvp` hands to child processes. Updating an
exported variable updates the environment too, so `export PATH=$PATH:/x`
changes where the *next* command is searched for.

Not supported: `A=1 cmd` (per-command environment); use `export`.

## 4. Execution (src/executor.c)

```
 lone built-in  → runs in the shell process (so cd and exit work)
 anything else  → fork() each stage, pipe() + dup2() between stages,
                  apply redirects, execvp(), waitpid()
```

* Redirections around a lone built-in are applied and then undone.
* The exit status of a pipeline is that of its last stage.
* `&` leaves the job running: stdin comes from `/dev/null`, SIGINT is
  ignored, and finished jobs are reaped with `waitpid(-1, WNOHANG)` before
  each prompt.
* Status codes: 127 command not found, 126 found but not executable,
  128+N killed by signal N, 2 syntax error.

## 5. Trace mode

`trace on` prints one line per step to **stderr**, so it does not disturb
pipes or redirected output. `trace off` turns it back off.

```
devshell> X=hello
devshell> trace on
devshell> /bin/echo $X | tr a-z A-Z > out.txt && cat out.txt
[lex]    WORD(/bin/echo) WORD($X) PIPE WORD(tr) WORD(a-z) WORD(A-Z) OUT WORD(out.txt) AND WORD(cat) WORD(out.txt)
[parse]  2 pipelines
[parse]    #1: 2 stages
[parse]        stage 0: argv=[/bin/echo, $X]
[parse]        stage 1: argv=[tr, a-z, A-Z] out(truncate)=out.txt
[parse]    then '&&' decides whether #2 runs
[parse]    #2: 1 stage
[parse]        stage 0: argv=[cat, out.txt]
[exec]   -- pipeline #1 --
[expand] $X -> hello
[exec]   pipe() -> read fd 3, write fd 4
[exec]   fork() -> pid 4121  (stage 0: /bin/echo)
[exec]       child 4121: dup2(pipe fd 4, 1); execvp("/bin/echo")
[exec]   fork() -> pid 4122  (stage 1: tr)
[exec]       child 4122: dup2(pipe fd 3, 0); open("out.txt", truncate) -> stdout; execvp("tr")
[wait]   waitpid(4121) -> exited with status 0
[wait]   waitpid(4122) -> exited with status 0
[exec]   exit status = 0
[exec]   '&&' after status 0: pipeline #2 runs
[exec]   -- pipeline #2 --
[exec]   'cat' is a built-in: runs inside the shell process, no fork()
HELLO
[exec]   exit status = 0
```

| Tag        | Phase                                                         |
|------------|---------------------------------------------------------------|
| `[lex]`    | the token stream                                              |
| `[parse]`  | the pipelines, stages, redirects and connectors               |
| `[expand]` | each word that changed (variables, quote removal)             |
| `[var]`    | assignments and exports, and whether children can see them    |
| `[exec]`   | `pipe`, `fork`, what each child does, `&&` / `\|\|` decisions |
| `[wait]`   | `waitpid` results                                             |
| `[reap]`   | background jobs collected between prompts                     |

What each child does is printed by the parent: output written by the child
after `dup2()` would land in the redirected file instead of on screen.
