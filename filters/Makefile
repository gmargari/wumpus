OBJECT_FILES = \
	document.o inputstream.o text_inputstream.o xml_inputstream.o \
	html_inputstream.o office_inputstream.o ps_inputstream.o pdf_inputstream.o \
	multitext_inputstream.o mbox_inputstream.o mp3_inputstream.o trec_inputstream.o \
	trecmulti_inputstream.o compressed_inputstream.o gzip_inputstream.o \
	bzip2_inputstream.o xtext_inputstream.o \
	conversion_inputstream.o troff_inputstream.o ngram_inputstream.o

%.o : %.cpp %.h
	$(CXX) $(CPPFLAGS) -c -o $@ $<

filters.a: $(OBJECT_FILES)
	rm -f $@
	ar cr $@ $(OBJECT_FILES)

clean:
	rm -f $(OBJECT_FILES)
	rm -f filters.a


