gslx680: driver.o
	gcc -o gslx680 driver.o -lm

driver.o: driver.c driver.h
	gcc -c -o driver.o driver.c

install:
	install gslx680 /bin
	#install igslx680.init /etc/init.d/igslx680
	install gslx680.service /etc/systemd/system/gslx680.service
	#rm -f /etc/init.d/gslx680
	#update-rc.d igslx680 defaults

clean:
	rm -rf gslx680 *.o

