/**
 * Implementation of the IncompleteLanguageModel class. See header file for
 * documentation.
 *
 * author: Stefan Buettcher
 * created: 2006-08-27
 * changed: 2007-02-12
 **/


#include <math.h>
#include <map>
#include <set>
#include "incomplete_language_model.h"
#include "../misc/all.h"


IncompleteLanguageModel::IncompleteLanguageModel(
		AbstractLanguageModel *backgroundModel, bool claimOwnership) {
	assert(backgroundModel != NULL);
	this->backgroundModel = backgroundModel;
	mustDeleteBackgroundModel = claimOwnership;
	tfProbabilities = new std::map<int,double>();
	spaceCovered = backgroundSpaceCovered = 0;
} // end of IncompleteLanguageModel()


IncompleteLanguageModel::~IncompleteLanguageModel() {
	delete ((std::map<int,double>*)tfProbabilities);
	if (mustDeleteBackgroundModel)
		delete backgroundModel;
} // end of ~IncompleteLanguageModel()


double IncompleteLanguageModel::getTermProbability(int termID) {
	LocalLock lock(this);
	std::map<int,double> *pMap = (std::map<int,double>*)tfProbabilities;
	if (pMap->find(termID) != pMap->end()) {
		// if we know this term, simply obtain the probability from the map
		return (*pMap)[termID];
	}
	else {
		// otherwise, query the background model and adjust the term's probability,
		// based on how much of the foreground and the background probability space
		// is covered by known terms
		double p = backgroundModel->getTermProbability(termID);
		p = p / (1.0 - backgroundSpaceCovered) * (1.0 - spaceCovered);
		return p;
	}
} // end of getTermProbability(int)


void IncompleteLanguageModel::setTermProbability(int termID, double p) {
	LocalLock lock(this);
	assert((p > 0) && (p <= 1));
	std::map<int,double> *pMap = (std::map<int,double>*)tfProbabilities;

	if (pMap->find(termID) != pMap->end()) {
		// if we already knew this term, we need to revert the coverage
		// caused by it
		spaceCovered -= (*pMap)[termID];
		backgroundSpaceCovered -= backgroundModel->getTermProbability(termID);
	}

	// put probability value into map and update coverage information
	(*pMap)[termID] = p;
	spaceCovered += (*pMap)[termID];
	assert((spaceCovered >= 0) && (spaceCovered <= 1));
	backgroundSpaceCovered += backgroundModel->getTermProbability(termID);
	assert((backgroundSpaceCovered >= 0) && (backgroundSpaceCovered <= 1));
} // end of setTermProbability(int, double)


double IncompleteLanguageModel::getDocumentProbability(int termID) {
	return backgroundModel->getDocumentProbability(termID);
} // end of getDocumentProbability(int)


double IncompleteLanguageModel::getTermProbability(const char *term) {
	assert("Not implemented" == NULL);
	return 0;
} // end of getTermProbability(char*)


double IncompleteLanguageModel::getDocumentProbability(const char *term) {
	assert("Not implemented" == NULL);
	return 0;
} // end of getDocumentProbability(char*)


double IncompleteLanguageModel::getKLD(IncompleteLanguageModel *p, IncompleteLanguageModel *q) {
	// collect the set of all known terms from both p and q
	std::map<int,double>::iterator iter;
	std::set<int> knownTerms;
	std::map<int,double> *pMap = (std::map<int,double>*)p->tfProbabilities;
	for (iter = pMap->begin(); iter != pMap->end(); ++iter)
		knownTerms.insert(iter->first);
	std::map<int,double> *qMap = (std::map<int,double>*)q->tfProbabilities;
	for (iter = qMap->begin(); iter != qMap->end(); ++iter)
		knownTerms.insert(iter->first);

	// compute the KLD contributions of the known terms
	double pCovered = 0, qCovered = 0, result = 0;
	for (std::set<int>::iterator iter2 = knownTerms.begin(); iter2 != knownTerms.end(); ++iter2) {
		double P = p->getTermProbability(*iter2);
		pCovered += P;
		double Q = q->getTermProbability(*iter2);
		qCovered += Q;
		if (P < 1E-9)
			continue;
		if (Q < 1E-9)
			Q = 1E-9;
		result += P * log(P / Q);
	}
	assert((pCovered <= 1) && (qCovered <= 1));

	// compute the KLD contributions of the unknown terms
	result += (1 - pCovered) * log((1 - pCovered) / (1 - qCovered));
	return result;
} // end of getKLD(...)


