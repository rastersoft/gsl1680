gslx680: driver.o
	gcc -o gslx680 driver.o -lm

driver.o: driver.c driver.h
	gcc -c -o driver.o driver.c

install:
	install gslx680 /bin
	install igslx680.init /etc/init.d/igslx680
	update-rc.d gslx680 defaults

clean:
	rm -rf gslx680 *.o

