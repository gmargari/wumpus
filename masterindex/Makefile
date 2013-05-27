TARGET = \
	masterindex.a

OBJECT_FILES = \
	masterindex.o master_docidcache.o master_ve.o

%.o : %.cpp %.h
	$(CXX) $(CPPFLAGS) -c -o $@ $<

$(TARGET): $(OBJECT_FILES)
	rm -f $@
	ar cr $@ $(OBJECT_FILES)

clean:
	rm -f $(TARGET) $(OBJECT_FILES)

