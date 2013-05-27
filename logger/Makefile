OBJECT_FILES = fslogger_client.o

EXECUTABLES = fslogger client

default: logger.a

%.o : %.cpp
	$(CXX) $(CPPFLAGS) -c -o $@ $<

.PHONY: logger.a

logger.a: $(OBJECT_FILES)
	rm -f $@
	ar cr $@ $(OBJECT_FILES)

clean:
	rm -f $(OBJECT_FILES)
	rm -f logger.a
	rm -f $(EXECUTABLES)

EXE_PARAMS = $(CPPFLAGS) $(LDFLAGS)

executables: $(EXECUTABLES)

fslogger: fslogger.cpp fslogger.h
	$(CXX) $(EXE_PARAMS) -o fslogger fslogger.cpp ../misc/misc.a

client: client.cpp fslogger_client.cpp fslogger_client.h
	$(CXX) $(EXE_PARAMS) -o fslogger_client client.cpp fslogger_client.cpp


