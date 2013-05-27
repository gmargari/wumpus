TARGET = \
	terabyte.a

OBJECT_FILES = \
	terabyte_lexicon.o terabyte_query.o terabyte_surrogates.o chapter6.o

TB_CPPFLAGS = -D_XOPEN_SOURCE=500 -D_FILE_OFFSET_BITS=64 -O2 -ffast-math
TB_LDFLAGS = -lpthread -lcrypt -lz

%.o : %.cpp %.h
	$(CXX) $(CPPFLAGS) -c -o $@ $<

$(TARGET): $(OBJECT_FILES)
	rm -f $@
	ar cr $@ $(OBJECT_FILES)

frontend: frontend.cpp ../misc/utils.cpp ../misc/alloc.cpp ../misc/logging.cpp ../misc/lockable.cpp ../misc/stringtokenizer.cpp ../misc/stopwords.cpp
	$(CXX) $(TB_CPPFLAGS) -o $@ $^ $(TB_LDFLAGS)

copy_index: copy_index.cpp ../index/index_compression.cpp ../misc/alloc.cpp ../misc/logging.cpp ../misc/lockable.cpp
	$(CXX) $(TB_CPPFLAGS) -o $@ $^ $(TB_LDFLAGS)

pruning_for_chapter6: pruning_for_chapter6.cpp ../index/index.a ../misc/misc.a ../index/index.a
	$(CXX) $(TB_CPPFLAGS) -o $@ $^ $(TB_LDFLAGS)

clean:
	rm -f $(TARGET) $(OBJECT_FILES)
	rm -f copy_index frontend merge_pruned_indices prune_index


