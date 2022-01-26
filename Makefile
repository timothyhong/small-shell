main:
	gcc -std=c99 -Wall -g -o smallsh smallsh.c
	
clean:
	rm -f smallsh