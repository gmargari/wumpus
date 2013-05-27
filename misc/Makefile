TARGET = \
	misc.a

OBJECT_FILES = \
	alloc.o stringbuffer.o stringbuffersegment.o execute.o stringtokenizer.o \
	utils.o general_avltree.o lockable.o configurator.o io.o logging.o \
	compression.o document_analyzer.o global.o stopwords.o term_iterator.o

%.o : %.cpp %.h
	$(CXX) $(CPPFLAGS) -c -o $@ $<

$(TARGET): $(OBJECT_FILES)
	rm -f $@
	ar cr $@ $(OBJECT_FILES)

clean:
	rm -f $(TARGET) $(OBJECT_FILES)

