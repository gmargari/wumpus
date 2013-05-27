/**
 * This file contains the implementation of the TerabyteSurrogates class.
 * See header file for documentation.
 *
 * author: Stefan Buettcher
 * created: 2006-08-16
 * changed: 2006-09-03
 **/


#include <fcntl.h>
#include <map>
#include <set>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "terabyte_surrogates.h"
#include "../feedback/language_model.h"
#include "../index/index_compression.h"
#include "../misc/all.h"


#define LOG_ID "TerabyteSurrogates"


TerabyteSurrogates::TerabyteSurrogates(
		const char *fileName, bool create, int surrogateSize, bool loadIntoMemory) {
	this->surrogateSize = surrogateSize;
	this->surrogateData = NULL;
	this->documentCount = 0;
	this->surrogateBufferPos = 0;

	if (create) {
		// create new surrogate database; open the data file, truncate if necessary
		readOnly = false;
		fileHandle =
			open(fileName, O_CREAT | O_RDWR | O_TRUNC | O_LARGEFILE, DEFAULT_FILE_PERMISSIONS);
		if (fileHandle < 0) {
			sprintf(errorMessage, "Unable to create file: %s", fileName);
			log(LOG_ERROR, LOG_ID, errorMessage);
		}
		else {
			forced_write(fileHandle, &documentCount, sizeof(documentCount));
			forced_write(fileHandle, &surrogateSize, sizeof(surrogateSize));
		}
	}

	if (!create) {
		// read surrogate data from data file
		readOnly = true;
		fileHandle = open(fileName, O_RDONLY | O_LARGEFILE);
		if (fileHandle < 0) {
			sprintf(errorMessage, "Unable to open file: %s", fileName);
			log(LOG_ERROR, LOG_ID, errorMessage);
		}
		else {
			forced_read(fileHandle, &documentCount, sizeof(documentCount));
			forced_read(fileHandle, &(this->surrogateSize), sizeof(this->surrogateSize));
			if (surrogateSize != this->surrogateSize)
				log(LOG_ERROR, LOG_ID, "Conflicting values for surrogateSize.");
			assert(this->surrogateSize > 0);
			int arraySize = documentCount * this->surrogateSize + HEADER_SIZE;
			if (loadIntoMemory) {
				// if caller requested to load everything into mem, do so
				surrogateData = typed_malloc(byte, arraySize);
				if (surrogateData == NULL)
					log(LOG_ERROR, LOG_ID, "Unable to allocate memory for surrogates.");
				else {
					lseek(fileHandle, (off_t)0, SEEK_SET);
					int result = forced_read(fileHandle, surrogateData, arraySize);
					assert(result == arraySize);
				}
				close(fileHandle);
				fileHandle = -1;
			}
			else {
				// otherwise, mmap the data file so that we do not have to worry about
				// whether everything is in memory or not when processing requests
				surrogateData =
					(byte*)mmap(NULL, arraySize, PROT_READ, MAP_PRIVATE, fileHandle, 0);
				if ((surrogateData == NULL) || (surrogateData == MAP_FAILED)) {
					log(LOG_ERROR, LOG_ID, "Unable to mmap surrogate file.");
					perror(NULL);
				}
			}
		}
	}

} // TerabyteSurrogates(char*, bool, int, bool)


TerabyteSurrogates::~TerabyteSurrogates() {
	// if we have in-memory surrogate data, and they are not just an mmap,
	// free the memory allocated for them
	if (surrogateData != NULL) {
		if (munmap(surrogateData, documentCount * surrogateSize) != 0) {
			free(surrogateData);
			surrogateData = NULL;
		}
	}

	// if we have an open data file and some pending surrogates that still need
	// to be written to disk, do so before closing the file
	if (fileHandle >= 0) {
		if (surrogateBufferPos > 0)
			forced_write(fileHandle, surrogateBuffer, surrogateBufferPos);
		lseek(fileHandle, (off_t)0, SEEK_SET);
		forced_write(fileHandle, &documentCount, sizeof(documentCount));
		close(fileHandle);
		fileHandle = -1;
	}
} // ~TerabyteSurrogates()


int TerabyteSurrogates::addSurrogate(const TerabyteSurrogate *surrogate) {
	if ((readOnly) || (fileHandle < 0))
		return -1;
	if (surrogateBufferPos + surrogateSize + 32 >= sizeof(surrogateBuffer)) {
		forced_write(fileHandle, surrogateBuffer, surrogateBufferPos);
		surrogateBufferPos = 0;
	}
	encodeSurrogate(surrogate, &surrogateBuffer[surrogateBufferPos]);
	surrogateBufferPos += surrogateSize;
	return documentCount++;
} // end of addSurrogate(TerabyteSurrogate*)


bool TerabyteSurrogates::getSurrogate(int documentID, TerabyteSurrogate *result) {
	if ((documentID < 0) || (documentID >= documentCount))
		return false;
	if (!readOnly)
		return false;
	if ((surrogateData == NULL) && (fileHandle < 0))
		return false;
	decodeSurrogate(&surrogateData[documentID * surrogateSize + HEADER_SIZE], result);
	return true;
} // end of getSurrogate(int, TerabyteSurrogate*)


double TerabyteSurrogates::getCosineSimilarity(
		const TerabyteSurrogate *x, double docLenX,
		const TerabyteSurrogate *y, double docLenY,
		LanguageModel *languageModel) {
	std::map<int, double> xScores;
	double xVectorLength = 0;
	for (int i = 0; i < x->termCount; i++) {
		offset tf, df;
		languageModel->getTermInfo(x->terms[i].termID, &tf, &df);
		double q = MAX(tf, 1) / languageModel->corpusSize;
		double p = x->terms[i].frequency / docLenX;
		double score = p * log(p / q);
		xVectorLength += pow(score, 2);
		xScores[x->terms[i].termID] = score;
	}
	xVectorLength = sqrt(xVectorLength);

	double product = 0;
	double yVectorLength = 0;
	for (int i = 0; i < y->termCount; i++) {
		offset tf, df;
		languageModel->getTermInfo(y->terms[i].termID, &tf, &df);
		double q = MAX(tf, 1) / languageModel->corpusSize;
		double p = y->terms[i].frequency / docLenY;
		double score = p * log(p / q);
		yVectorLength += pow(score, 2);
		if (xScores.find(y->terms[i].termID) != xScores.end())
			product += score * xScores[y->terms[i].termID];
	}
	yVectorLength = sqrt(yVectorLength);

	return product / (xVectorLength * yVectorLength);
} // end of getSimilarity(TerabyteSurrogate*, TerabyteSurrogate*)


double TerabyteSurrogates::getKLD(
		const TerabyteSurrogate *p, double docLenP,
		const TerabyteSurrogate *q, double docLenQ,
		LanguageModel *languageModel) {
	std::set<int> allTerms;
	std::map<int, double> pProb;
	std::map<int, double> qProb;

	// sum of probabilities of all terms for which we don't know their exact
	// frequency values
	double pTotalWeightOfUnknown = 1;
	double qTotalWeightOfUnknown = 1;
	

	// compute term probabilities for p
	for (int i = 0; i < p->termCount; i++) {
		pProb[p->terms[i].termID] = p->terms[i].frequency / docLenP;
		allTerms.insert(p->terms[i].termID);
	}

	// compute term probabilities for q
	for (int i = 0; i < q->termCount; i++) {
		qProb[q->terms[i].termID] = q->terms[i].frequency / docLenQ;
		allTerms.insert(q->terms[i].termID);
	}

	double kld = 0;
	for (std::set<int>::iterator iter = allTerms.begin(); iter != allTerms.end(); ++iter) {
	}
} // end of getKLD(...)


static int sortByTermID(const void *a, const void *b) {
	SurrogateTermDescriptor *x = (SurrogateTermDescriptor*)a;
	SurrogateTermDescriptor *y = (SurrogateTermDescriptor*)b;
	return (x->termID - y->termID);
}


void TerabyteSurrogates::encodeSurrogate(
		const TerabyteSurrogate *surrogate, byte *buffer) {
	byte dummyBuffer[16];
	int termCount = surrogate->termCount;
	if (termCount > MAX_SURROGATE_TERM_COUNT)
		termCount = MAX_SURROGATE_TERM_COUNT;

	while (termCount > 1) {
		SurrogateTermDescriptor terms[MAX_SURROGATE_TERM_COUNT];
		memcpy(terms, surrogate->terms, termCount * sizeof(SurrogateTermDescriptor));
		int byteSize = 0;
		qsort(terms, termCount, sizeof(SurrogateTermDescriptor), sortByTermID);

		int prevTerm = -1;
		for (int i = 0; i < termCount; i++) {
			int delta = encodeVByte32(terms[i].termID - prevTerm, dummyBuffer)
				        + encodeVByte32(terms[i].frequency, dummyBuffer);
			if (byteSize + delta > surrogateSize) {
				byteSize = surrogateSize + 1;
				break;
			}
			byteSize += encodeVByte32(terms[i].termID - prevTerm, &buffer[byteSize]);
			byteSize += encodeVByte32(terms[i].frequency, &buffer[byteSize]);
			prevTerm = terms[i].termID;
		}

		if (byteSize <= surrogateSize) {
			if (byteSize < surrogateSize)
				buffer[byteSize] = 0;
			break;
		}
		termCount--;
	}
} // end of encodeSurrogate(TerabyteSurrogate*, byte*)


void TerabyteSurrogates::decodeSurrogate(
		const byte *buffer, TerabyteSurrogate *surrogate) {
	surrogate->termCount = 0;
	int prevTerm = -1;
	int pos = 0;
	while ((pos < surrogateSize) && (buffer[pos] != 0)) {
		int32_t value;
		int delta = decodeVByte32(&value, &buffer[pos]);
		assert(delta <= 4);
		pos += delta;
		prevTerm += value;
		surrogate->terms[surrogate->termCount].termID = prevTerm;
		delta = decodeVByte32(&value, &buffer[pos]);
		assert(delta <= 2);
		pos += delta;
		surrogate->terms[surrogate->termCount].frequency = value;
		surrogate->termCount++;
	}
} // end of decodeSurrogate(byte*, TerabyteSurrogate*)


