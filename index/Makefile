TARGET = \
	index.a

OBJECT_FILES = \
	index.o annotator.o postinglist.o segmentedpostinglist.o indextotext.o \
	index_compression.o index_merger.o fakeindex.o lexicon.o index_types.o \
	compactindex.o compactindex2.o \
	index_iterator.o index_iterator2.o multiple_index_iterator.o \
	compressed_lexicon.o compressed_lexicon_iterator.o threshold_iterator.o \
	realloc_lexicon.o realloc_lexicon_iterator.o ondisk_index_manager.o \
	inplace_index.o fs_inplace_index.o postinglist_in_file.o my_inplace_index.o \
	finegrained_iterator.o hybrid_lexicon.o

%.o : %.cpp index_types.h
	$(CXX) $(CPPFLAGS) -c -o $@ $<

.PHONY: $(TARGET)
	
index.a: $(OBJECT_FILES)
	rm -f $@
	ar cr $@ $(OBJECT_FILES)

clean:
	rm -f $(TARGET) $(OBJECT_FILES)

