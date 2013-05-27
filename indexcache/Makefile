TARGET = \
	indexcache.a

OBJECT_FILES = \
	cached_extents.o extentlist_cached.o extentlist_cached_compressed.o indexcache.o \
	docidcache.o documentcache.o

%.o : %.cpp
	$(CXX) $(CPPFLAGS) -c -o $@ $<

.PHONY: $(TARGET)
	
$(TARGET): $(OBJECT_FILES)
	rm -f $@
	ar cr $@ $(OBJECT_FILES)

clean:
	rm -f $(TARGET) $(OBJECT_FILES)

