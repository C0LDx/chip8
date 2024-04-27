all:
	gcc -Wall -Werror -Wextra -Isrc/include -Lsrc/lib -std=c11 *.c -o chip8 -lmingw32 -lSDL2main -lSDL2