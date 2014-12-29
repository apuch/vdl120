all:
	gcc -o vdl120 src/vdl120.c -lusb -lrt -Wall -O0 -g

install:
	cp -v vdl120 /usr/bin/
