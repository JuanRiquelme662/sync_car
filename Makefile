CC     = gcc
CFLAGS = -Wall -Wextra -pthread

sync_car: sync_car.o sync_car-runner.o
	$(CC) $(CFLAGS) -o sync_car sync_car.o sync_car-runner.o

sync_car.o: sync_car.c sync_car.h
	$(CC) $(CFLAGS) -c sync_car.c

sync_car-runner.o: sync_car-runner.c sync_car.h
	$(CC) $(CFLAGS) -Wno-unused-parameter -c sync_car-runner.c

clean:
	rm -f sync_car *.o

.PHONY: clean