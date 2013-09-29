gslx680: driver.o
	gcc -o driver driver.o -lm

driver.o: driver.c driver.h
	gcc -c -o driver.o driver.c

install:
	install gslx680 /bin
	install gslx680.init /etc/init.d/gslx680
	update-rc.d gslx680 defaults

clean:
	rm -rf gslx680 *.o

