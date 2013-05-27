ALL = \
	get_document get_document2 approximate_index_size \
	get_dictionary_size get_hashdictionary_speed \
	measure_dictionary_performance measure_allocation_performance \
	measure_hybrid_indexing_performance \
	get_token_lengths count_ngrams measure_hashbased_indexing_performance \
	compress_by_reference get_zipf_curve measure_disk_seeks sorting \
	get_keywords_from_document tokenize_trec_document

SUBDIRS = \
	daemons \
	extentlist \
	feedback \
	filemanager \
	filesystem \
	filters \
	index \
	indexcache \
	logger \
	masterindex \
	misc \
	query \
	stemming \
	terabyte

ARCHIVES = $(foreach dir,$(SUBDIRS),../$(dir)/$(dir).a)


default: $(ALL)

get_document: get_document.cpp
	$(CXX) -O2 -o $@ $^

get_document2: get_document2.cpp
	$(CXX) -O2 -o $@ $^

approximate_index_size: approximate_index_size.cpp
	$(CXX) -O2 -o $@ $< ../filters/*.cpp ../misc/utils.cpp ../misc/logging.cpp ../misc/configurator.cpp ../misc/stringtokenizer.cpp ../misc/io.cpp ../misc/execute.cpp ../misc/lockable.cpp ../indexcache/documentcache.cpp -lpthread

get_zipf_curve: get_zipf_curve.cpp
	$(CXX) -O2 -o $@ $< ../filters/*.cpp ../misc/utils.cpp ../misc/logging.cpp ../misc/configurator.cpp ../misc/stringtokenizer.cpp ../misc/io.cpp ../misc/execute.cpp ../misc/lockable.cpp ../indexcache/documentcache.cpp -lpthread

measure_dictionary_performance: measure_dictionary_performance.cpp
	$(CXX) -O2 -o $@ $< ../filters/*.cpp ../misc/utils.cpp ../misc/logging.cpp ../misc/configurator.cpp ../misc/stringtokenizer.cpp ../misc/io.cpp ../misc/execute.cpp ../misc/lockable.cpp ../indexcache/documentcache.cpp -lpthread

measure_allocation_performance: measure_allocation_performance.cpp
	$(CXX) -O2 -o $@ $< $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) -lpthread

measure_hashbased_indexing_performance: measure_hashbased_indexing_performance.cpp
	$(CXX) -O2 -o $@ $< $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) -lpthread

measure_sortbased_indexing_performance: measure_sortbased_indexing_performance.cpp
	$(CXX) -O2 -o $@ $< $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) -lpthread

measure_hybrid_indexing_performance: measure_hybrid_indexing_performance.cpp
	$(CXX) -O2 -o $@ $< $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) -lpthread

get_keywords_from_document: get_keywords_from_document.cpp
	$(CXX) -O2 -o $@ $< $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) -lpthread

tokenize_trec_document: tokenize_trec_document.cpp
	$(CXX) -O2 -o $@ $< $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) -lpthread -static

get_token_lengths: get_token_lengths.cpp
	$(CXX) -O2 -o $@ $< ../filters/*.cpp ../misc/utils.cpp ../misc/logging.cpp ../misc/configurator.cpp ../misc/stringtokenizer.cpp ../misc/io.cpp ../misc/execute.cpp ../misc/lockable.cpp ../indexcache/documentcache.cpp -lpthread

compute_doclen_distribution: compute_doclen_distribution.cpp
	$(CXX) -static -m32 -O2 -o $@ $< ../filters/*.cpp ../misc/utils.cpp ../misc/logging.cpp ../misc/configurator.cpp ../misc/stringtokenizer.cpp ../misc/io.cpp ../misc/execute.cpp ../misc/lockable.cpp ../indexcache/documentcache.cpp -lpthread

count_ngrams: count_ngrams.cpp
	$(CXX) -O2 -o $@ $< ../filters/*.cpp ../misc/utils.cpp ../misc/logging.cpp ../misc/configurator.cpp ../misc/stringtokenizer.cpp ../misc/io.cpp ../misc/execute.cpp ../misc/lockable.cpp ../indexcache/documentcache.cpp -lpthread

get_dictionary_size: get_dictionary_size.cpp
	$(CXX) -D_FILE_OFFSET_BITS=64 -O2 -o $@ $< ../misc/utils.cpp -lz

get_dictionary_speed: get_dictionary_speed.cpp
	$(CXX) -O2 -o $@ $< ../misc/utils.cpp -lz

get_interleaving_performance: get_interleaving_performance.cpp
	$(CXX) -O2 -o $@ $< ../misc/utils.cpp -lz

compress_by_reference: compress_by_reference.cpp
	$(CXX) -O2 -o $@ $< $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) -lpthread

%: %.cpp
	$(CXX) -D_FILE_OFFSET_BITS=64 -O2 -o $@ $<

clean:
	rm -f $(ALL)

	
