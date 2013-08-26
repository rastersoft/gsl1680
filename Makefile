driver: driver.o
	gcc -o driver driver.o -lm

driver.o: driver.c driver.h
	gcc -c -o driver.o driver.c

clear:
	rm -rf driver *.o

