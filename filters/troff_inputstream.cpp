/**
 * author: Stefan Buettcher
 * created: 2007-01-15
 * changed: 2007-01-15
 **/


#include <string.h>
#include "troff_inputstream.h"
#include "../misc/all.h"


TroffInputStream::TroffInputStream(const char *fileName) :
		ConversionInputStream(fileName, TROFF_COMMAND) {
} // end of TroffInputStream(char*)


TroffInputStream::~TroffInputStream() {
} // end of ~TroffInputStream()


int TroffInputStream::getDocumentType() {
	return DOCUMENT_TYPE_TROFF;
} // end of getDocumentType()


bool TroffInputStream::canProcess(const char *fileName, byte *fileStart, int length) {
	if (length < 8)
		return false;
	if (strncmp((char*)fileStart, ".\\\" ", 4) == 0)
		return true;
	if (strncmp((char*)fileStart, ".TH ", 4) == 0)
		return true;
	return false;
} // end of canProcess(char*, byte*, int)


