/**
 * author: Stefan Buettcher
 * created: 2006-09-04
 * changed: 2006-09-04
 **/


#include <assert.h>
#include <string.h>
#include "interpolation_language_model.h"
#include "../misc/all.h"


InterpolationLanguageModel::InterpolationLanguageModel(
		AbstractLanguageModel **models, double *weights, int count) {
	assert(count > 0);
	this->models = typed_malloc(AbstractLanguageModel*, count);
	this->weights = typed_malloc(double, count);
	this->count = count;
	double weightSum = 0;
	for (int i = 0; i < count; i++) {
		this->models[i] = models[i];
		weightSum += weights[i];
	}
	assert(weightSum > 0);
	for (int i = 0; i < count; i++)
		this->weights[i] = weights[i] / weightSum;
} // end of InterpolationLanguageModel(AbstractLanguageModel**, double*, int)


InterpolationLanguageModel::~InterpolationLanguageModel() {
	free(models);
	models = NULL;
	free(weights);
	weights = NULL;
} // end of ~InterpolationLanguageModel()


double InterpolationLanguageModel::getTermProbability(int termID) {
	double result = 0;
	for (int i = 0; i < count; i++)
		result += weights[i] * models[i]->getTermProbability(termID);
	return result;
} // end of getTermProbability(int)


double InterpolationLanguageModel::getDocumentProbability(int termID) {
	double result = 0;
	for (int i = 0; i < count; i++)
		result += weights[i] * models[i]->getDocumentProbability(termID);
	return result;
} // end of getDocumentProbability(int)


double InterpolationLanguageModel::getTermProbability(const char *term) {
	double result = 0;
	for (int i = 0; i < count; i++)
		result += weights[i] * models[i]->getTermProbability(term);
	return result;
} // end of getTermProbability(char*)


double InterpolationLanguageModel::getDocumentProbability(const char *term) {
	double result = 0;
	for (int i = 0; i < count; i++)
		result += weights[i] * models[i]->getDocumentProbability(term);
	return result;
} // end of getDocumentProbability(char*)



