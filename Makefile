# Use the following flags when compiling for Linux (default).
CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D__STDC_FORMAT_MACROS -O2 -ffast-math
LDFLAGS = -lpthread -lcrypt -lz

# Use the following flags when compiling for Apple MacOS.
#CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D__STDC_FORMAT_MACROS -O2 -ffast-math
#LDFLAGS = -lpthread -lz

# Use the following flags when compiling for Solaris.
#CPPFLAGS = -D_XOPEN_SOURCE=500 -D_FILE_OFFSET_BITS=64 -O2 -ffast-math -D__SOLARIS__
#LDFLAGS = -lpthread -lcrypt -lz -lsocket -lnsl -lrt

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
	terabyte \
	testing

ARCHIVES = $(foreach dir,$(SUBDIRS),$(dir)/$(dir).a)

EXECUTABLES = \
	bin/handyman \
	bin/wumpus


.PHONY: $(SUBDIRS) test

export CPPFLAGS LDFLAGS

default: $(EXECUTABLES)
#	@$(MAKE) --quiet -C http
#	@$(MAKE) --quiet -C inotifyd
	@echo ""
	@echo "Build completed."
	@echo "You can start the Wumpus IR system by typing \"bin/wumpus --config=wumpus.cfg\"."
	@echo ""

$(SUBDIRS):
	@echo [Building $@/$@.a]
	@$(MAKE) -j 4 --quiet -C $@

bin/combine_restrict: terabyte/combine_restrict.cpp $(SUBDIRS)
	@echo [Building $@]
	@$(CXX) $(CPPFLAGS) -o $@ $< $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) $(LDFLAGS)

bin/prune_index: terabyte/prune_index.cpp $(SUBDIRS)
	@echo [Building $@]
	@$(CXX) $(CPPFLAGS) -o $@ $< $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) $(LDFLAGS)

bin/merge_pruned_indices: terabyte/merge_pruned_indices.cpp $(SUBDIRS)
	@echo [Building $@]
	@$(CXX) $(CPPFLAGS) -o $@ $< $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) $(LDFLAGS)

bin/%: executable/%.cpp $(SUBDIRS)
	@echo [Building $@]
	@$(CXX) $(CPPFLAGS) -o $@ $< $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) $(LDFLAGS)

terabyte/pruning_for_chapter6: terabyte/pruning_for_chapter6.cpp $(SUBDIRS)
	@$(CXX) $(CPPFLAGS) -o $@ $< $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) $(ARCHIVES) $(LDFLAGS)

distclean: clean
	@echo [Cleaning database/]
	@rm -rf database/* database/.index_disallow
	@rm -f logfile

clean:
	@for i in $(SUBDIRS) ; do \
		echo [Cleaning $$i/] ; \
		$(MAKE) --quiet -C $$i clean ; \
	done
	@echo [Cleaning bin/]
	@rm -f bin/*
	@rm -f `find . -name a.out`
	@$(MAKE) --quiet -C http clean
	@$(MAKE) --quiet -C inotifyd clean

clean-mountpoints:
	rm -f /.indexdir/*
	rm -f /home/.indexdir/*


