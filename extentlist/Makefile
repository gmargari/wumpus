TARGET = \
	extentlist.a

OBJECT_FILES = \
	extentlist.o extentlist_fromto.o extentlist_sequence.o extentlist_or.o \
	extentlist_and.o extentlist.o extentlist_containment.o extentlist_oneelement.o \
	extentlist_range.o extentlist_copy.o extentlist_empty.o extentlist_ordered.o \
	extentlist_or_postings.o extentlist_transformation.o address_space_transformation.o \
	simplifier.o optimizer.o

%.o : %.cpp extentlist.h
	$(CXX) $(CPPFLAGS) -c -o $@ $<

.PHONY: $(TARGET)
	
$(TARGET): $(OBJECT_FILES)
	rm -f $@
	ar cr $@ $(OBJECT_FILES)

clean:
	rm -f $(TARGET) $(OBJECT_FILES)

