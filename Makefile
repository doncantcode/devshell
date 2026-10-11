CC      = gcc
CFLAGS  = -std=gnu11 -Wall -Wextra -g -Iinclude -MMD -MP
LDLIBS  =
SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:src/%.c=build/%.o)
LIB_OBJ = build/lexer.o build/parser.o build/errors.o build/vars.o build/expand.o \
          build/trace.o build/color.o

# Command history and line editing use GNU readline when its headers are
# installed (sudo apt install libreadline-dev / pacman -S libreadline-devel).
# Without it DevShell still builds, just without history.
HAVE_RL := $(shell printf '\043include <readline/readline.h>\n' | $(CC) -E - >/dev/null 2>&1 && echo 1)
ifeq ($(HAVE_RL),1)
CFLAGS += -DHAVE_READLINE
LDLIBS += -lreadline
endif

devshell: $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDLIBS)

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/test_%: tests/test_%.c $(LIB_OBJ)
	$(CC) $(CFLAGS) $< $(LIB_OBJ) -o $@

test: devshell build/test_lexer build/test_parser build/test_expand
	./build/test_lexer
	./build/test_parser
	./build/test_expand
	sh tests/run_tests.sh

valgrind: devshell
	valgrind --leak-check=full --error-exitcode=1 ./devshell < tests/valgrind_input.txt

clean:
	rm -rf build devshell

.PHONY: test valgrind clean

-include $(OBJ:.o=.d)
