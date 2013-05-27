/**
 * author: Stefan Buettcher
 * created: 2007-04-01
 * changed: 2007-04-01
 **/


#include <stdlib.h>
#include <map>
#include <vector>
#include "address_space_transformation.h"
#include "../misc/all.h"


using namespace std;


AddressSpaceTransformation::AddressSpaceTransformation(TransformationElement *rules, int count) {
	assert(count >= 0);
	source = typed_malloc(offset, count + 1);
	destination = typed_malloc(offset, count + 1);
	length = typed_malloc(uint32_t, count + 1);
	this->count = 0;
	for (int i = 0; i < count; i++) {
		if (rules[i].length > 0) {
			source[this->count] = rules[i].source;
			destination[this->count] = rules[i].destination;
			length[this->count] = rules[i].length;
			this->count++;
		}
	}
} // end of AddressSpaceTransformation(TransformationElement*, int)


AddressSpaceTransformation::~AddressSpaceTransformation() {
	free(source);
	source = NULL;
	free(destination);
	destination = NULL;
	free(length);
	length = NULL;
} // end of ~AddressSpaceTransformation()


static int compareBySource(const void *a, const void *b) {
	TransformationElement *x = (TransformationElement*)a;
	TransformationElement *y = (TransformationElement*)b;
	if (x->source < y->source)
		return -1;
	else if (y->source < x->source)
		return +1;
	else
		return 0;
} // end of compareByTarget(void*, void*)


AddressSpaceTransformation * AddressSpaceTransformation::invert() {
	TransformationElement *tfe = typed_malloc(TransformationElement, count + 1);
	for (int i = 0; i < count; i++) {
		tfe[i].source = destination[i];
		tfe[i].destination = source[i];
		tfe[i].length = length[i];
	}
	qsort(tfe, count, sizeof(TransformationElement), compareBySource);
	AddressSpaceTransformation *result = new AddressSpaceTransformation(tfe, count);
	free(tfe);
	return result;
} // end of duplicate()


void AddressSpaceTransformation::transformSequence(offset *postings, int count) {
	if ((count <= 0) || (this->count <= 0))
		return;
	int prevRule = 0;
	offset first = source[0], last = source[this->count - 1] + length[this->count - 1] - 1;
	offset prevRuleStart = source[prevRule];

	// skip over all postings for which we *cannot* have an applicable rule
	int start = 0;
	while ((start < count) && (postings[start] < first))
		start++;

	for (int i = start; (i < count) && (postings[i] <= last); i++) {
		offset p = postings[i];
		if (p < prevRuleStart + length[prevRule]) {
			// same as last: apply same rule again
			postings[i] = p + destination[prevRule] - prevRuleStart;
		}
		else {
			// perform galloping search to find appropriate rule for this posting
			int lower = prevRule, delta = 1;
			while (source[lower + delta] + length[lower + delta] <= p) {
				delta += delta;
				if (lower + delta >= this->count) {
					delta = this->count - 1 - lower;
					break;
				}
			}
			int upper = lower + delta;
			while (lower < upper) {
				int middle = (lower + upper + 1) >> 1;
				if (source[middle] > p)
					upper = middle - 1;
				else
					lower = middle;
			}
			prevRule = lower;
			prevRuleStart = source[lower];

			// if matching rule found, apply transformation
			if (p < prevRuleStart + length[prevRule])
				postings[i] = p + destination[prevRule] - prevRuleStart;
		}
	}

	// sort postings according to their new values
	if (count > 1)
		sortOffsetsAscending(postings, count);
} // end of transformSequence(offset*, int)




// ---------------------------------------------------------------------

static map<offset,offset> initialTokenCount;
static map<offset,TransformationElement> transformationRules;
static map<offset,vector<offset> > rulesForFile;


void AddressSpaceTransformation::setInitialTokenCount(offset fileStart, offset tokenCount) {
	initialTokenCount[fileStart] = tokenCount;
} // end of setInitialTokenCount(offset, offset)


offset AddressSpaceTransformation::getInitialTokenCount(offset fileStart) {
	if (initialTokenCount.find(fileStart) == initialTokenCount.end())
		return 0;
	else
		return initialTokenCount[fileStart];
} // end of getInitialTokenCount(offset)


void AddressSpaceTransformation::removeRules(offset fileStart) {
	initialTokenCount.erase(fileStart);
	if (rulesForFile.find(fileStart) != rulesForFile.end()) {
		vector<offset> &vec = rulesForFile[fileStart];
		for (int i = 0; i < vec.size(); i++)
			transformationRules.erase(vec[i]);
		rulesForFile.erase(fileStart);
	}
} // end of removeRules(offset)


void AddressSpaceTransformation::updateRules(offset oldFileStart, offset newFileStart, offset length) {
	TransformationElement tfe;
	tfe.source = oldFileStart;
	tfe.destination = newFileStart;
	tfe.length = length;

	vector<offset> rulesForThisFile;
	if (rulesForFile.find(oldFileStart) != rulesForFile.end()) {
		vector<offset> &oldRules = rulesForFile[oldFileStart];
		for (int i = 0; i < oldRules.size(); i++) {
			transformationRules[oldRules[i]].destination = newFileStart;
			rulesForThisFile.push_back(oldRules[i]);
		}
	}
	rulesForThisFile.push_back(oldFileStart);

	rulesForFile.erase(oldFileStart);
	rulesForFile[newFileStart] = rulesForThisFile;	
	transformationRules[oldFileStart] = tfe;

	initialTokenCount[newFileStart] = initialTokenCount[oldFileStart];
	initialTokenCount.erase(oldFileStart);
} // end of updateRules(offset, offset, offset)


AddressSpaceTransformation * AddressSpaceTransformation::getRules() {
	TransformationElement *rules =
		typed_malloc(TransformationElement, transformationRules.size() + 1);
	int cnt = 0;
	map<offset,TransformationElement>::iterator iter;
	for (iter = transformationRules.begin(); iter != transformationRules.end(); ++iter)
		rules[cnt++] = iter->second;
	assert(cnt == transformationRules.size());
	AddressSpaceTransformation *result =
		new AddressSpaceTransformation(rules, cnt);
	free(rules);
	return result;
} // end of getRules()


