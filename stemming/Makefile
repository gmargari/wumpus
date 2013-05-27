TARGET = \
	stemming.a

OBJECT_FILES = \
	api.o english1.o english2.o german.o stemmer.o utilities.o

%.o : %.cpp
	$(CXX) $(CPPFLAGS) -c -o $@ $<

$(TARGET): $(OBJECT_FILES)
	rm -f $@
	ar cr $@ $(OBJECT_FILES)

clean:
	rm -f $(TARGET) $(OBJECT_FILES)

