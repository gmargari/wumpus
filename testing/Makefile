TARGET = \
	testing.a

OBJECT_FILES = \
	testing.o \
	test_compression.o test_postings.o test_utils.o

%.o : %.cpp %.h index_types.h
	$(CXX) $(CPPFLAGS) -c -o $@ $<

.PHONY: $(TARGET)
	
testing.a: $(OBJECT_FILES)
	rm -f $@
	ar cr $@ $(OBJECT_FILES)

clean:
	rm -f $(TARGET) $(OBJECT_FILES)

