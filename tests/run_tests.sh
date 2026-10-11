#!/bin/sh
# End-to-end tests: feed commands to ./devshell on stdin and compare output.
# Run from the project root (make test does this).

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SHELL_BIN="$ROOT/devshell"
export DEVSHELL_TEMPLATES="$ROOT/templates"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK" || exit 1

pass=0
fail=0

# check NAME INPUT EXPECTED   (stdout and stderr merged, exact match)
check() {
    actual=$(printf '%s\n' "$2" | "$SHELL_BIN" 2>&1)
    if [ "$actual" = "$3" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        printf 'FAIL: %s\n  input:    %s\n  expected: %s\n  actual:   %s\n' "$1" "$2" "$3" "$actual"
    fi
}

# contains NAME INPUT SUBSTRING
contains() {
    actual=$(printf '%s\n' "$2" | "$SHELL_BIN" 2>&1)
    case "$actual" in
        *"$3"*) pass=$((pass + 1)) ;;
        *) fail=$((fail + 1))
           printf 'FAIL: %s\n  input:    %s\n  wanted:   %s\n  actual:   %s\n' "$1" "$2" "$3" "$actual" ;;
    esac
}

# lacks NAME INPUT SUBSTRING   (output must NOT contain SUBSTRING)
lacks() {
    actual=$(printf '%s\n' "$2" | "$SHELL_BIN" 2>&1)
    case "$actual" in
        *"$3"*) fail=$((fail + 1))
                printf 'FAIL: %s\n  input:    %s\n  unwanted: %s\n  actual:   %s\n' "$1" "$2" "$3" "$actual" ;;
        *) pass=$((pass + 1)) ;;
    esac
}

# --- built-ins ---------------------------------------------------------
check "echo"            'echo hi'                       'hi'
check "echo quotes"     'echo "a   b" '"'"'c|d'"'"      'a   b c|d'
check "echo -n"         'echo -n hi'                    'hi'
check "empty input"     ''                              ''
check "blank input"     '   '                           ''
check "cd + pwd"        'cd /tmp
pwd'                                                    '/tmp'
contains "cd missing"   'cd /no/such/dir'               'No such file or directory'
contains "help"         'help'                          'Variables'
contains "help cmd"     'help export'                   'Usage'
contains "help example" 'help cd'                       'Example'
contains "help unknown" 'help nosuchthing'              'no help for'
out=$(printf 'history\n' | "$SHELL_BIN" 2>&1)      # piped input is never recorded
case "$out" in
    ""|*"not available"*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: history on piped input"; echo "$out" ;;
esac

# --- external commands, pipes, redirection ------------------------------
check "external"        '/bin/echo ext'                 'ext'
check "pipe"            'echo hello | tr a-z A-Z'       'HELLO'
check "3-stage pipe"    'echo hello | tr a-z A-Z | tr L l' 'HEllO'
check "redirect out"    'echo one > f.txt
cat f.txt'                                              'one'
check "redirect append" 'echo a > g.txt
echo b >> g.txt
cat g.txt'                                              'a
b'
check "redirect in"     'echo data > h.txt
tr a-z A-Z < h.txt'                                     'DATA'
check "redirect+pipe"   'echo abc > i.txt
cat < i.txt | tr a-z A-Z > j.txt
cat j.txt'                                              'ABC'
check "ext redirect"    '/bin/echo viaexec > k.txt
cat k.txt'                                              'viaexec'

# --- errors -------------------------------------------------------------
contains "unknown cmd"  'nosuchcommand_xyz'             'command not found'
contains "missing in"   'cat < nosuchfile.txt'          'nosuchfile.txt: No such file'
contains "unmatched"    'echo "abc'                     'unmatched double quote'
contains "trailing |"   'echo hi |'                     'syntax error'
contains "no target"    'echo hi >'                     'expected a file name'
touch ro.txt && chmod 000 ro.txt
if [ ! -r ro.txt ]; then   # root, and some Windows filesystems, ignore chmod
    contains "perm denied" 'cat ro.txt'                 'Permission denied'
else
    echo "skip: chmod 000 not enforced here (permission-denied test)"
fi
chmod 644 ro.txt

# --- background ---------------------------------------------------------
contains "bg start"     'sleep 0.2 &
echo after'                                             '[bg]'
contains "bg continues" 'sleep 0.2 &
echo after'                                             'after'
contains "bg reaped"    'sleep 0.1 &
sleep 0.5
echo x'                                                 '[done]'

# --- file operations ----------------------------------------------------
check "mkdir/touch/ls"  'mkdir d
touch d/b d/a
ls d'                                                   'a
b'
check "ls hides dot"    'mkdir hid
touch hid/.dot hid/vis
ls hid'                                                 'vis'
check "ls -a"           'ls -a hid'                     '.
..
.dot
vis'
contains "ls -l"        'ls -l hid'                     '-rw-r--r--'
check "mkdir -p"        'mkdir -p x/y/z
ls x/y'                                                 'z'
check "rm"              'touch gone
rm gone
ls gone'                                                'devshell: ls: cannot access '"'"'gone'"'"': No such file or directory'
check "rm -r"           'rm -r x d
ls x'                                                   'devshell: ls: cannot access '"'"'x'"'"': No such file or directory'
contains "rm dir"       'mkdir dd
rm dd'                                                  'Is a directory'
contains "rm ."         'rm -r .'                       'refusing to remove'
check "cat stdin pipe"  'echo hi | cat'                 'hi'
check "ls | wc"         'mkdir lsd
touch lsd/1 lsd/2 lsd/3
ls lsd | wc -l'                                         '3'

# --- scaffolding --------------------------------------------------------
contains "new c"        'new c-project hello'           "Created c-project 'hello'"
contains "new c builds" 'new c-project app
cd app
make
./app'                                                  'Hello from app!'
check "new c template"  'cat hello/src/main.c | grep printf' '    printf("Hello from hello!\n");'
contains "new python"   'new python-project py'         "Created python-project 'py'"
contains "new bad type" 'new rust-project r'            'unknown project type'
contains "new bad name" 'new c-project ../evil'         'invalid project name'

# --- variables and expansion --------------------------------------------
check "assign + expand" 'X=hello
echo $X'                                                'hello'
check "braces"          'X=ab
echo ${X}c'                                             'abc'
check "double quotes"   'X=1
echo "v=$X"'                                            'v=1'
check "single quotes"   'X=1
echo '"'"'$X'"'"                                        '$X'
check "escaped dollar"  'X=1
echo \$X'                                               '$X'
check "unset is empty"  'echo a$NOPE_VAR b'             'a b'
check "empty word gone" 'echo $NOPE_VAR end'            'end'
check "status var"      'false
echo $?'                                                '1'
check "status ok"       'true
echo $?'                                                '0'
check "late expansion"  'X=1; echo $X'                  '1'
check "assign in redir" 'F=o.txt
echo hi > $F
cat o.txt'                                              'hi'
check "assign twice"    'A=1 B=2
echo $A$B'                                              '12'
check "value w/ spaces" 'MSG="hello world"
echo "$MSG"'                                            'hello world'
check "value uses var"  'A=x
B=${A}y
echo $B'                                                'xy'
check "unset"           'X=1
unset X
echo "[$X]"'                                            '[]'
check "pid is number"   'echo $$ | tr -d 0-9'          ''
contains "ambiguous"    'echo hi > $NOPE_VAR'           'ambiguous redirect'

# scoping: shell variable vs exported (environment)
check "not exported"    'X=1
env | grep -c ^X='                                      '0'
check "exported"        'X=1
export X
env | grep ^X='                                         'X=1'
check "export NAME=val" 'export Y=2
env | grep ^Y='                                         'Y=2'
check "export updates"  'export Z=1
Z=2
env | grep ^Z='                                         'Z=2'
check "unset+env"       'export W=1
unset W
env | grep -c ^W='                                      '0'
contains "export list"  'export Q=1
export'                                                 'export Q="1"'
contains "set lists"    'LOCALVAR=7
set'                                                    'LOCALVAR=7'
contains "bad export"   'export 1bad'                   'not a valid identifier'
check "PATH export"     'export PATH=/bin:$PATH
echo $PATH | grep -c ^/bin:'                            '1'

# --- command lists: ; && || ---------------------------------------------
check "semicolon"       'echo a; echo b'                'a
b'
check "trailing ;"      'echo hi ;'                     'hi'
check "&& true"         'true && echo yes'              'yes'
check "&& false"        'false && echo yes'             ''
check "|| false"        'false || echo no'              'no'
check "|| true"         'true || echo no'               ''
check "left assoc 1"    'false && echo a || echo b'     'b'
check "left assoc 2"    'true && false || echo c'       'c'
check "left assoc 3"    'true || echo a && echo b'      'b'
check "pipe in list"    'echo a | tr a-z A-Z && echo done' 'A
done'
check "status of list"  'false; echo $?'                '1'
check "&& chain stops"  'echo 1 && false && echo 2 && echo 3' '1'
contains "bg + next"    'sleep 0.1 & echo after'        'after'
contains "&& error"     'ls nosuchfile_xyz && echo hi'  'No such file'
contains "syn ;"        '; ls'                          'syntax error'
contains "syn &&"       'ls &&'                         'missing command after'
contains "syn bg list"  'true && sleep 1 &'             "cannot follow"
check "quoted ;"        'echo "a;b" '"'"'c&&d'"'"       'a;b c&&d'
check "exit in list"    'echo a; exit; echo b'          'a'

# --- trace mode ---------------------------------------------------------
contains "trace lex"    'trace on
echo hi'                                                '[lex]'
contains "trace parse"  'trace on
echo hi'                                                '[parse]'
contains "trace builtin" 'trace on
echo hi'                                                'runs inside the shell process, no fork()'
contains "trace fork"   'trace on
/bin/echo hi | tr a-z A-Z'                              'fork() -> pid'
contains "trace pipe"   'trace on
/bin/echo hi | tr a-z A-Z'                              'pipe() -> read fd'
contains "trace dup2"   'trace on
/bin/echo hi | tr a-z A-Z'                              'dup2(pipe fd'
contains "trace exec"   'trace on
/bin/echo hi | tr a-z A-Z'                              'execvp("tr")'
contains "trace wait"   'trace on
/bin/echo hi'                                           'waitpid('
contains "trace redir"  'trace on
/bin/echo hi > t.txt'                                   'open("t.txt", truncate) -> stdout'
contains "trace expand" 'X=1
trace on
echo $X'                                                '$X -> 1'
contains "trace assign" 'trace on
X=5'                                                    'shell variable: not visible to child processes'
contains "trace skip"   'trace on
false && echo x'                                        'is skipped'
contains "trace bg"     'trace on
sleep 0.1 &'                                            'the shell does not wait'
contains "trace off"    'trace on
trace off
echo hi'                                                'trace off
hi'
check "trace status"    'trace'                         'trace is off'
contains "trace usage"  'trace maybe'                   'usage: trace'

# --- interface: prompt, banner, colour ------------------------------------
# `-i` forces the interactive front end even though stdin is a pipe.
ESC=$(printf '\033')
interactive() { printf '%s\n' "$1" | "$SHELL_BIN" -i 2>&1; }

out=$(interactive 'echo hi')
case "$out" in
    *"DevShell "*"a small developer shell"*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: banner"; echo "$out" ;;
esac
case "$out" in
    *"devshell "*"> "*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: prompt"; echo "$out" ;;
esac
case "$out" in
    *"$ESC"*) fail=$((fail + 1)); echo "FAIL: colour leaked into non-terminal output" ;;
    *) pass=$((pass + 1)) ;;
esac

case "$out" in
    *"tour"*"guided walkthrough"*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: banner lists quick-start commands"; echo "$out" ;;
esac

out=$(printf 'echo hi\n' | "$SHELL_BIN" -i -q 2>&1)
case "$out" in
    *"DevShell "*) fail=$((fail + 1)); echo "FAIL: -q still prints the banner" ;;
    *) pass=$((pass + 1)) ;;
esac
out=$(printf 'echo hi\n' | DEVSHELL_QUIET=1 "$SHELL_BIN" -i 2>&1)
case "$out" in
    *"DevShell "*) fail=$((fail + 1)); echo "FAIL: DEVSHELL_QUIET still prints the banner" ;;
    *) pass=$((pass + 1)) ;;
esac
out=$(interactive 'welcome' | tail -n +4)          # skip the startup banner itself
case "$out" in
    *"guided walkthrough"*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: welcome reprints the quick start"; echo "$out" ;;
esac

# --- tour -----------------------------------------------------------------
contains "tour needs tty" 'tour'                        'needs an interactive session'

# a full run with plausible answers; afterwards the cwd is restored and the
# scratch folder is gone
before=$(ls -d "${TMPDIR:-/tmp}"/devshell-tour-* 2>/dev/null | wc -l)
out=$(printf '%s\n' 'pwd' 'tour' 'echo hello' 'ls' 'echo hello | tr a-z A-Z' \
      'echo hi > note.txt' 'cat note.txt' 'TOURVAR=5' 'echo $TOURVAR' \
      'false || echo recovered' 'trace on' 'echo hi | tr a-z A-Z' 'trace off' \
      'pwd > pwd_after.txt' 'echo "TV=[$TOURVAR]"' | "$SHELL_BIN" -i 2>&1)
case "$out" in
    *"Tour complete"*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: tour completes"; echo "$out" ;;
esac
case "$out" in
    *"Step 11 of 11"*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: tour reaches the last step" ;;
esac
case "$out" in
    *"TV=[]"*) pass=$((pass + 1)) ;;                 # variable made in the tour is removed
    *) fail=$((fail + 1)); echo "FAIL: tour cleans up its variable" ;;
esac
if [ -f pwd_after.txt ] && [ "$(cat pwd_after.txt)" = "$(printf 'pwd\n' | "$SHELL_BIN")" ]; then
    pass=$((pass + 1))
else
    fail=$((fail + 1)); echo "FAIL: tour restores the working directory"
fi
after=$(ls -d "${TMPDIR:-/tmp}"/devshell-tour-* 2>/dev/null | wc -l)
if [ "$before" = "$after" ]; then pass=$((pass + 1)); else
    fail=$((fail + 1)); echo "FAIL: tour leaves a scratch folder behind"; fi

out=$(printf '%s\n' 'tour' 'skip' 'skip' 'skip' 'skip' 'skip' 'skip' 'skip' 'skip' \
      'trace on' 'quit' 'echo zz' | "$SHELL_BIN" -i 2>&1)
case "$out" in
    *"Leaving the tour"*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: tour quit"; echo "$out" ;;
esac
case "$out" in
    *"WORD(echo) WORD(zz)"*) fail=$((fail + 1)); echo "FAIL: tour left trace on" ;;
    *) pass=$((pass + 1)) ;;
esac

out=$(printf '%s\n' 'tour' 'ls' 'quit' | "$SHELL_BIN" -i 2>&1)
case "$out" in
    *'step is about "Run a command"'*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: tour hints on a non-matching command"; echo "$out" ;;
esac

out=$(printf '%s\n' 'tour' 'exit 4' | "$SHELL_BIN" -i >/dev/null 2>&1; echo $?)
if [ "$out" = "4" ]; then pass=$((pass + 1)); else
    fail=$((fail + 1)); echo "FAIL: exit inside the tour leaves the shell (got $out)"; fi

out=$(interactive 'nosuchcommand_xyz')
case "$out" in
    *"[127]> "*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: prompt shows last status"; echo "$out" ;;
esac

mkdir -p repo/.git repo/sub
echo 'ref: refs/heads/feature-x' > repo/.git/HEAD
out=$(cd repo && interactive 'exit')
case "$out" in
    *"(feature-x)"*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: prompt shows git branch"; echo "$out" ;;
esac
out=$(cd repo/sub && interactive 'exit')       # found from a sub-directory
case "$out" in
    *"(feature-x)"*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: git branch from sub-directory"; echo "$out" ;;
esac
echo 'abcdef0123456789' > repo/.git/HEAD
out=$(cd repo && interactive 'exit')
case "$out" in
    *"(abcdef0)"*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: detached HEAD shows short hash"; echo "$out" ;;
esac

out=$(printf 'nosuchcommand_xyz\n' | DEVSHELL_COLOR=always "$SHELL_BIN" 2>&1)
case "$out" in
    *"${ESC}[1;31mdevshell:"*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: coloured error"; echo "$out" ;;
esac
out=$(printf 'nosuchcommand_xyz\n' | DEVSHELL_COLOR=always NO_COLOR=1 "$SHELL_BIN" 2>&1)
case "$out" in
    *"$ESC"*) pass=$((pass + 1)) ;;   # explicit "always" wins over NO_COLOR
    *) fail=$((fail + 1)); echo "FAIL: DEVSHELL_COLOR=always" ;;
esac
out=$(printf 'nosuchcommand_xyz\n' | NO_COLOR=1 "$SHELL_BIN" 2>&1)
case "$out" in
    *"$ESC"*) fail=$((fail + 1)); echo "FAIL: NO_COLOR ignored" ;;
    *) pass=$((pass + 1)) ;;
esac
out=$(printf 'trace on\necho hi\n' | DEVSHELL_COLOR=always "$SHELL_BIN" 2>&1)
case "$out" in
    *"${ESC}[35m[lex]"*"${ESC}[34m[parse]"*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: coloured trace tags"; echo "$out" ;;
esac
out=$(printf 'mkdir cd1\ntouch fa\nls\n' | DEVSHELL_COLOR=always "$SHELL_BIN" 2>&1)
case "$out" in
    *"${ESC}[1;34mcd1${ESC}[0m"*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); echo "FAIL: ls colours directories"; echo "$out" ;;
esac
check "ls plain in pipe" 'ls | grep -c cd1'             '1'
check "ls -l -h"        'mkdir hs
echo hello > hs/f
ls -lh hs | grep -c " 6 "'                              '1'

# --- exit codes ---------------------------------------------------------
printf 'exit 3\n' | "$SHELL_BIN"
if [ $? -eq 3 ]; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: exit 3"; fi
printf 'false\n' | "$SHELL_BIN"
if [ $? -eq 1 ]; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: status of last command"; fi

echo "e2e: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
