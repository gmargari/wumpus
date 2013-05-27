TARGET = \
	query.a

OBJECT_FILES = \
	query.o countquery.o gclquery.o bm25query.o rankedquery.o miscquery.o \
	getquery.o updatequery.o qapquery.o xpathquery.o xpath_primitives.o \
	xpath_tokenizer.o xpath_predicate.o desktopquery.o qap2query.o \
	cdrquery.o ponte_croft.o querytokenizer.o npquery.o experimental_query.o \
	languagemodel_query.o vectorspace_query.o divergence_query.o bm25f_query.o \
	helpquery.o synonymquery.o

%.o : %.cpp %.h
	$(CXX) $(CPPFLAGS) -c -o $@ $<

$(TARGET): $(OBJECT_FILES)
	rm -f $@
	ar cr $@ $(OBJECT_FILES)

clean:
	rm -f $(TARGET) $(OBJECT_FILES)


