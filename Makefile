all:
	gcc -o vdl120 src/vdl120.c -lusb -lrt -Wall

install:
	cp -v vdl120 /usr/bin/
