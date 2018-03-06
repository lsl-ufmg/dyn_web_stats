NAME = simul
CC = gcc
CFLAGS = -O0 -g -Wall -Wextra -pedantic -DDEBUG
#CFLAGS = -O3 -march=native -Wall -Wextra -pedantic
LDFLAGS =
SRC = .
DEPS = $(wildcard $(SRC)/*.h)
CODE = $(wildcard $(SRC)/*.c)
OBJ = $(patsubst %.c,%.o,$(CODE))
ARGS=ls sort wc
VALARGS=--leak-check=full --show-leak-kinds=all --track-origins=yes

.PHONY: install clean dist-gzip dist-bzip2 dist-xz dist
.SILENT: install clean dist-gzip dist-bzip2 dist-xz dist

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

$(NAME): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

run: $(NAME)
	./$(NAME) $(ARGS)

val: $(NAME)
	valgrind $(VALARGS) ./$(NAME) $(ARGS)

clean:
	rm -f $(SRC)/*.o *~ $(SRC)/*~ $(NAME)
