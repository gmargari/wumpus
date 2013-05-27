/**
 * Implementation of the LanguageModel class. See header file for documentation.
 *
 * author: Stefan Buettcher
 * created: 2006-01-27
 * changed: 2009-02-01
 **/


#include <string.h>
#include "language_model.h"
#include "../filters/xml_inputstream.h"
#include "../misc/all.h"
#include "../misc/language.h"
#include "../query/getquery.h"
#include "../stemming/stemmer.h"


const double LanguageModel::ARRAY_GROWTH_RATE;

static const char *LOG_ID = "LanguageModel";


void LanguageModel::initialize() {
	termSlotsAllocated = INITIAL_TERM_SLOTS;
	terms = typed_malloc(LanguageModelTermDescriptor, termSlotsAllocated);
	termSlotsUsed = 0;
	hashTableSize = INITIAL_HASHTABLE_SIZE;
	hashTable = typed_malloc(int32_t, hashTableSize);
	for (int i = 0; i < hashTableSize; i++)
		hashTable[i] = -1;
	corpusSize = 0.0;
	documentCount = 0.0;
	stemmed = false;
	useStemmingCache = false;
} // end of initialize()


LanguageModel::LanguageModel(char *fileName) {
	FILE *f = NULL;
	if ((fileName != NULL) && (fileName[0] != 0))
		f = fopen(fileName, "r");
	if (f == NULL) {
		snprintf(errorMessage, sizeof(errorMessage), "Unable to open file: %s", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
		initialize();
		corpusSize = 1.0;
		documentCount = 1.0;
		stemmed = false;
	}
	else {
		initialize();
		int s;
		char line[1024];
	
		getNextNonCommentLine(f, line, sizeof(line));
		sscanf(line, "%d", &s);
		stemmed = (s != 0);
	
		getNextNonCommentLine(f, line, sizeof(line));
		sscanf(line, "%d%lf%lf", &termSlotsUsed, &corpusSize, &documentCount);
		termSlotsAllocated = termSlotsUsed + 32;
		if (termSlotsAllocated < INITIAL_TERM_SLOTS)
			termSlotsAllocated = INITIAL_TERM_SLOTS;
		terms = typed_realloc(LanguageModelTermDescriptor, terms, termSlotsAllocated);
	
		for (int i = 0; i < termSlotsUsed; i++) {
			getNextNonCommentLine(f, line, sizeof(line));
			long long tf, df;
			int status =
				sscanf(line, "%s%s%lld%lld", terms[i].term, terms[i].stemmed, &tf, &df);
			terms[i].termFrequency = tf;
			terms[i].documentCount = df;
			assert(status == 4);
			assert(strlen(terms[i].term) > 0);
			assert(strlen(terms[i].stemmed) > 0);
		}

		resizeHashTable(termSlotsAllocated);
		fclose(f);
	}
} // end of LanguageModel(char*)


LanguageModel::LanguageModel(double corpusSize, double documentCount, bool stemmed) {
	initialize();
	this->corpusSize = corpusSize;
	this->documentCount = documentCount;
	this->stemmed = stemmed;
} // end of LanguageModel(double, double, bool)


LanguageModel::LanguageModel(Index *index, offset start, offset end, bool stemmed) {
	char line[Query::MAX_RESPONSELINE_LENGTH + 4];
	initialize();
	this->corpusSize = (end - start + 1);
	this->documentCount = 1;
	this->stemmed = stemmed;

	char queryBody[64];
	sprintf(queryBody, OFFSET_FORMAT " " OFFSET_FORMAT, start, end);
	const char *modifiers[2] = { "filtered", NULL };
	GetQuery *query = new GetQuery(index, "get", modifiers, queryBody, Index::GOD, -1);
	if (query->parse()) {
		line[sizeof(line) - 1] = 0;
		while (query->getNextLine(line)) {
			assert(line[sizeof(line) - 1] == 0);
			StringTokenizer *tok = new StringTokenizer(line, " ");
			while (tok->hasNext()) {
				char *t = tok->getNext();
				if ((strchr(t, '<') == NULL) && (strchr(t, '>') == NULL)) {
					if (getTermID(t) < 0)
						addTerm(t, 1, 1);
					else
						updateTerm(t, 1, 0);
				}
			}
			delete tok;
		}
	}
	else {
		log(LOG_ERROR, LOG_ID, "Parsing failed in LanguageModel(Index*, offset, offset, bool)");
		fprintf(stderr, "%lld %lld\n", static_cast<long long>(start), static_cast<long long>(end));
	}
	delete query;

	this->corpusSize = 0;
	for (int i = 0; i < termSlotsUsed; i++)
		this->corpusSize += terms[i].termFrequency;
} //end of LanguageModel(Index*, offset, offset, bool)


LanguageModel::LanguageModel(LanguageModel **models, int modelCount) {
	initialize();
	this->corpusSize = corpusSize;
	this->documentCount = documentCount;
	for (int i = 0; i < modelCount; i++)
		if (models[i]->stemmed)
			stemmed = true;
	for (int i = 0; i < modelCount; i++)
		addLanguageModel(models[i]);
} // end of LanguageModel(LanguageModel*, int)


LanguageModel::~LanguageModel() {
	if (terms != NULL) {
		free(terms);
		terms = NULL;
	}
	if (hashTable != NULL) {
		free(hashTable);
		hashTable = NULL;
	}
} // end of ~LanguageModel()


void LanguageModel::saveToFile(char *fileName) {
	LocalLock lock(this);
	FILE *f = fopen(fileName, "w");
	if (f == NULL) {
		snprintf(errorMessage, sizeof(errorMessage), "Unable to write to file: %s", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
	}
	else {
		fprintf(f, "# The next line indicates whether the LM is stemmed (1) or unstemmed (0).\n");
		fprintf(f, "%d\n", stemmed ? 1 : 0);
		fprintf(f, "# The following line: TERM_COUNT CORPUS_SIZE DOCUMENT_COUNT\n");
		fprintf(f, "%d %.1lf %.1lf\n", termSlotsUsed, corpusSize, documentCount);
		fprintf(f, "# All following lines: TERM STEMMED_FORM CORPUS_FREQUENCY DOC_FREQUENCY\n");
		for (int i = 0; i < termSlotsUsed; i++)
			fprintf(f, "%s %s " OFFSET_FORMAT " " OFFSET_FORMAT "\n",
					terms[i].term, terms[i].stemmed,
					terms[i].termFrequency, terms[i].documentCount);
		fclose(f);
	}
} // end of saveToFile(char*)


void LanguageModel::setAllDocumentFrequencies(offset df) {
	for (int i = 0; i < termSlotsUsed; i++)
		terms[i].documentCount = df;
} // end of setAllDocumentFrequencies(offset)


void LanguageModel::enableStemmingCache() {
	useStemmingCache = true;
}


int LanguageModel::getTermCount() {
	return termSlotsUsed;
}


void LanguageModel::addTerm(char *term, offset termFrequency, offset documentCount) {
	LocalLock lock(this);
	removeTerm(term);
	updateTerm(term, termFrequency, documentCount);
} // end of addTerm(char*, offset, offset)


void LanguageModel::removeTerm(char *term) {
	LocalLock lock(this);
	int termID = getTermID(term);
	if (termID >= 0)
		removeTermDescriptor(termID);
} // end of removeTerm(char*)


void LanguageModel::updateTerm(char *term, offset deltaTF, offset deltaDF) {
	LocalLock lock(this);
	int id = getTermID(term);
	if (id >= 0) {
		terms[id].termFrequency += deltaTF;
		terms[id].documentCount += deltaDF;
	}
	else {
		char *normalized = normalizeTerm(term);
		int termID = addTermDescriptor();
		unsigned int hashSlot = simpleHashFunction(normalized) % hashTableSize;
		terms[termID].next = hashTable[hashSlot];
		hashTable[hashSlot] = termID;
		strcpy(terms[termID].term, term);
		strcpy(terms[termID].stemmed, normalized);
		terms[termID].termFrequency = deltaTF;
		terms[termID].documentCount = deltaDF;
		free(normalized);
	}
} // end of updateTerm(char*, offset, offset)


void LanguageModel::addLanguageModel(LanguageModel *m) {
	LocalLock lock1(this);
	LocalLock lock2(m);
	corpusSize += m->corpusSize;
	documentCount += m->documentCount;
	for (int i = 0; i < m->termSlotsUsed; i++)
		updateTerm(m->terms[i].term, m->terms[i].termFrequency, m->terms[i].documentCount);
} // end of addLanguageModel(LanguageModel*)


void LanguageModel::getTermInfo(const char *term, offset *termFrequency, offset *documentCount) {
	LocalLock lock(this);
	getTermInfo(getTermID((char*)term), termFrequency, documentCount);
} // end of getTermInfo(char*, offset*, offset*)


void LanguageModel::getTermInfo(int termID, offset *termFrequency, offset *documentCount) {
	LocalLock lock(this);
	*termFrequency = *documentCount = 0;
	if ((termID >= 0) && (termID < termSlotsUsed)) {
		*termFrequency = terms[termID].termFrequency;
		*documentCount = terms[termID].documentCount;
	}
} // end of getTermInfo(int, offset*, offset*)


double LanguageModel::getTermProbability(int termID) {
	LocalLock lock(this);
	if ((termID < 0) || (termID >= termSlotsUsed))
		return 0;
	else
		return terms[termID].termFrequency / corpusSize;
} // end of getTermProbability(int)


double LanguageModel::getTermProbability(const char *term) {
	LocalLock lock(this);
	return LanguageModel::getTermProbability(getTermID((char*)term));
} // end of getTermProbability(char*)


double LanguageModel::getDocumentProbability(int termID) {
	LocalLock lock(this);
	if ((termID < 0) || (termID >= termSlotsUsed))
		return 0;
	else
		return terms[termID].documentCount / documentCount;
} // end of getDocumentProbability(int)


double LanguageModel::getDocumentProbability(const char *term) {
	LocalLock lock(this);
	return getDocumentProbability(getTermID((char*)term));
} // end of getDocumentProbability(char*)


double LanguageModel::kullbackLeiblerDivergence(LanguageModel *p, AbstractLanguageModel *q) {
	LocalLock lock1(p);
	LocalLock lock2(q);
	double result = 0.0;
	for (int i = 0; i < p->termSlotsUsed; i++) {
		double pX = p->terms[i].termFrequency / p->corpusSize;
		double qX = q->getTermProbability(p->terms[i].term);
		result += kullbackLeiblerDivergence(pX, qX, 1E6);
	}
	return result;
} // end of kullbackLeiblerDivergence(LanguageModel*, AbstractLanguageModel*)


double LanguageModel::kullbackLeiblerDivergence(LanguageModel *p, LanguageModel *q) {
	LocalLock lock1(p);
	LocalLock lock2(q);
	double result = 0.0;
	for (int i = 0; i < p->termSlotsUsed; i++) {
		double pX = p->terms[i].termFrequency / p->corpusSize;
		double qX = q->getTermProbability(p->terms[i].term);
		result += kullbackLeiblerDivergence(pX, qX, q->corpusSize);
	}
	return result;
} // end of kullbackLeiblerDivergence(LanguageModel*, LanguageModel*)


double LanguageModel::getKLD(
		LanguageModel *p, AbstractLanguageModel *q, AbstractLanguageModel *backgroundModel) {
	LocalLock lock1(p);
	LocalLock lock2(q);
	LocalLock lock3(backgroundModel);
	double result = 0.0;
	double coverage = 0.0;
	for (int i = 0; i < p->termSlotsUsed; i++) {
		char *term = p->terms[i].term;
		double bX = backgroundModel->getTermProbability(term);
		if (bX < 1E-10)
			continue;
		double pX = 0.8 * p->terms[i].termFrequency / p->corpusSize + 0.2 * bX;
		double qX = 0.8 * q->getTermProbability(term) + 0.2 * bX;
		coverage += pX;
		result += pX * log(pX / qX);
	}
	return result / coverage;
} // end of getKLD(LanguageModel*, AbstractLanguageModel*^2)


double LanguageModel::kullbackLeiblerDivergence(double p, double q, double corpusSize) {
	assert(corpusSize > 2);
	if (p < 1E-9)
		return 0;
	if (q < 0.5 / corpusSize)
		q = 0.5 / corpusSize;
	return p * log(p / q);
} // end of kullbackLeiblerDivergence(double, double, double)


char * LanguageModel::getTermString(int termID) {
	LocalLock lock(this);
	if ((termID < 0) || (termID >= termSlotsUsed))
		return NULL;
	return duplicateString(terms[termID].term);
} // end of getTermString(int)


char * LanguageModel::getStemmedTermString(int termID) {
	LocalLock lock(this);
	if ((termID < 0) || (termID >= termSlotsUsed))
		return NULL;
	return duplicateString(terms[termID].stemmed);
} // end of getStemmedTermString(int)


int LanguageModel::getTermID(char *term) {
	LocalLock lock(this);
	term = normalizeTerm(term);
	unsigned int hashSlot = simpleHashFunction(term) % hashTableSize;
	int id = hashTable[hashSlot];
	while (id >= 0) {
		assert(id < termSlotsUsed);
		if (strcmp(term, terms[id].stemmed) == 0) {
			free(term);
			return id;
		}
		id = terms[id].next;
	}
	free(term);
	return -1;
} // end of getTermID(char*)


char * LanguageModel::normalizeTerm(char *term) {
	LocalLock lock(this);
	if (term == NULL)
		return NULL;
	if (term[0] == 0)
		return duplicateString(term);
	if (term[0] == '$')
		term++;
	int len = strlen(term);
	if ((stemmed) && (strchr(term, '$') == NULL)) {
		char temp[MAX_TOKEN_LENGTH + 1];
		snprintf(temp, MAX_TOKEN_LENGTH - 1, "%s", term);
		temp[MAX_TOKEN_LENGTH] = temp[MAX_TOKEN_LENGTH - 1] = 0;
		Stemmer::stem(temp, LANGUAGE_ENGLISH, useStemmingCache);
		if (temp[0] == 0) {
			snprintf(temp, MAX_TOKEN_LENGTH, "%s$", term);
			temp[MAX_TOKEN_LENGTH] = 0;
		}
		else
			strcat(temp, "$");
		return duplicateString(temp);
	}
	return duplicateString(term);
} // end of normalizeTerm(char*)


void LanguageModel::removeTermDescriptor(int termID) {
	LocalLock lock(this);
	if ((termID < 0) || (termID >= termSlotsUsed))
		return;

	unsigned int hashSlot;
	int previous, id;

	// remove term descriptor with ID "termID" from hash table
	hashSlot = simpleHashFunction(terms[termID].stemmed) % hashTableSize;
	id = hashTable[hashSlot];
	previous = -1;
	while (id >= 0) {
		assert(id < termSlotsUsed);
		if (id == termID) {
			if (previous >= 0)
				terms[previous].next = terms[id].next;
			else
				hashTable[hashSlot] = terms[id].next;
			break;
		}
		previous = id;
		id = terms[id].next;
	}

	// copy term descriptor with ID (termSlotsUsed-1) to now-free slot and
	// update hash table
	if (termID != termSlotsUsed - 1) {
		memcpy(&terms[termID], &terms[termSlotsUsed - 1], sizeof(LanguageModelTermDescriptor));
		hashSlot = simpleHashFunction(terms[termID].stemmed) % hashTableSize;
		id = hashTable[hashSlot];
		if (id == termSlotsUsed - 1)
			hashTable[hashSlot] = termID;
		else {
			while (id >= 0) {
				if (terms[id].next == termSlotsUsed - 1) {
					terms[id].next = termID;
					break;
				}
				id = terms[id].next;
			}
		}
	} // end if (termID != termSlotsUsed - 1)

	// decrease slot use counter and resize arrays if appropriate
	termSlotsUsed--;
	if ((termSlotsUsed > INITIAL_TERM_SLOTS) && (termSlotsUsed < 0.5 * termSlotsAllocated)) {
		termSlotsAllocated = (int)(termSlotsUsed * ARRAY_GROWTH_RATE);
		terms = typed_realloc(LanguageModelTermDescriptor, terms, termSlotsAllocated);
	}
	if ((termSlotsUsed < hashTableSize/2) && (hashTableSize > INITIAL_HASHTABLE_SIZE)) {
		int nhs = (int)(hashTableSize / ARRAY_GROWTH_RATE);
		if (nhs < INITIAL_HASHTABLE_SIZE)
			nhs = INITIAL_HASHTABLE_SIZE;
		resizeHashTable(nhs);
	}
} // end of removeTermDescriptor(int)


int LanguageModel::addTermDescriptor() {
	if (termSlotsUsed >= termSlotsAllocated) {
		termSlotsAllocated = (int)(termSlotsUsed * ARRAY_GROWTH_RATE);
		terms = typed_realloc(LanguageModelTermDescriptor, terms, termSlotsAllocated);
	}
	if (termSlotsUsed >= hashTableSize)
		resizeHashTable((int)(termSlotsUsed * ARRAY_GROWTH_RATE));
	terms[termSlotsUsed].next = -1;
	return termSlotsUsed++;
} // end of addTermDescriptor()


static int sortByTF(const void *a, const void *b) {
	LanguageModelTermDescriptor *x = (LanguageModelTermDescriptor*)a;
	LanguageModelTermDescriptor *y = (LanguageModelTermDescriptor*)b;
	if (x->termFrequency > y->termFrequency)
		return -1;
	else if (y->termFrequency > x->termFrequency)
		return +1;
	else
		return 0;
} // end of sortByTF(const void*, const void*)


void LanguageModel::restrictToMostFrequent(int newTermCount) {
	assert(newTermCount > 0);
	LocalLock lock(this);
	if (newTermCount < termSlotsUsed) {
		qsort(terms, termSlotsUsed, sizeof(LanguageModelTermDescriptor), sortByTF);
		resizeHashTable(hashTableSize);
		while (termSlotsUsed > newTermCount)
			removeTermDescriptor(termSlotsUsed - 1);
	}
} // end of restrictToMostFrequent(int)


void LanguageModel::resizeHashTable(int size) {
	LocalLock lock(this);
	hashTableSize = size;
	hashTable = typed_realloc(int32_t, hashTable, hashTableSize);
	for (int i = 0; i < hashTableSize; i++)
		hashTable[i] = -1;
	for (int i = 0; i < termSlotsUsed; i++) {
		unsigned int hashSlot = simpleHashFunction(terms[i].stemmed) % hashTableSize;
		terms[i].next = hashTable[hashSlot];
		hashTable[hashSlot] = i;
	}
} // end of resizeHashTable()



