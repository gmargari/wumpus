TARGET = \
	filemanager.a

OBJECT_FILES = \
	directorycontent.o filemanager.o securitymanager.o extentlist_security.o

%.o : %.cpp %.h
	$(CXX) $(CPPFLAGS) -c -o $@ $<

$(TARGET): $(OBJECT_FILES)
	rm -f $@
	ar cr $@ $(OBJECT_FILES)

clean:
	rm -f $(TARGET) $(OBJECT_FILES)


