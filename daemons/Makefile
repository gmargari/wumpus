OBJECT_FILES = \
	daemon.o filesys_daemon.o authconn_daemon.o conn_daemon.o \
	client_connection.o query_executor.o eventqueue.o multitext_connection.o

%.o : %.cpp %.h
	$(CXX) $(CPPFLAGS) -c -o $@ $<

daemons.a: $(OBJECT_FILES)
	rm -f $@
	ar cr $@ $(OBJECT_FILES)

fsd: $(OBJECT_FILES)
	$(CC) -lpthread -o fsd $(OBJECT_FILES)
	
clean:
	rm -f $(OBJECT_FILES)
	rm -f daemons.a


