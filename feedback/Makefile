TARGET = \
	feedback.a

OBJECT_FILES = \
	dmc.o feedback.o language_model.o incomplete_language_model.o \
	interpolation_language_model.o relevance_model.o qrels.o

%.o : %.cpp %.h
	$(CXX) $(CPPFLAGS) -c -o $@ $<

$(TARGET): $(OBJECT_FILES)
	rm -f $@
	ar cr $@ $(OBJECT_FILES)

clean:
	rm -f $(TARGET) $(OBJECT_FILES)

