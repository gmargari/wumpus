TARGETS = \
	get_document_urls build_reorder_matrix build_reordered_index build_reordered_si_index

SUBDIRS = \
	arise \
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

ARCHIVES = $(foreach dir,$(SUBDIRS),../../$(dir)/$(dir).a)

default: $(TARGETS)

get_document_urls: get_document_urls.cpp
	$(CXX) -O2 -o $@ $< ../../filters/*.cpp ../../misc/utils.cpp ../../misc/logging.cpp ../../misc/configurator.cpp ../../misc/stringtokenizer.cpp ../../misc/io.cpp ../../misc/execute.cpp ../../misc/lockable.cpp ../../indexcache/documentcache.cpp -lpthread

build_reorder_matrix: build_reorder_matrix.cpp
	$(CXX) -O2 -o $@ $<

build_reordered_index: build_reordered_index.cpp
	$(CXX) -O2 -o $@ $< $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) -lpthread
	
build_reordered_si_index: build_reordered_si_index.cpp
	$(CXX) -O2 -o $@ $< $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) -lpthread
	
clean:
	rm -f $(TARGETS)


