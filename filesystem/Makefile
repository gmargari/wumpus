OBJECT_FILES = \
	filesystem.o filesystemcache.o file.o bucketfilesystem.o \
	nonfrag_filesystem.o pagecomparators.o filefile.o

%.o : %.cpp filesystem.h
	$(CXX) $(CPPFLAGS) -c -o $@ $<

filesystem.a: $(OBJECT_FILES)
	rm -f $@
	ar crs $@ $(OBJECT_FILES)

clean:
	rm -f $(OBJECT_FILES)
	rm -f filesystem.a

