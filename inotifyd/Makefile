CFLAGS= -g -I/lib/modules/`uname -r`/build/include
LDFLAGS= -lpthread
CXX= g++

all: inotifyd

inotifyd: inotifyd.o dirtree.o logger.o
	$(CXX) $(CFLAGS) $(LDFLAGS) $(filter %.o,$^) -o $@

%.o: %.cc %.h
	$(CXX) $(CFLAGS) -c $< -o $@

clean:
	rm -f inotifyd *.o
