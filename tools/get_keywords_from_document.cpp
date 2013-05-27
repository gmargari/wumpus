/**
 * Usage:  get_keywords_from_document LM_FILE KEYWORD_COUNT < DOCUMENT > KEYWORDS
 *
 * author: Stefan Buettcher
 * created: 2007-10-06
 * changed: 2007-10-06
 **/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include "../feedback/language_model.h"
#include "../filters/trec_inputstream.h"
#include "../index/compactindex.h"
#include "../index/index_iterator.h"
#include "../index/index_merger.h"
#include "../index/multiple_index_iterator.h"
#include "../misc/all.h"

using namespace std;

int main( int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Usage:  get_keywords_from_document LM_FILE KEYWORD_COUNT < DOCUMENT > KEYWORDS\n");
		return 1;
	}
	int keywordCount;
	LanguageModel lm(argv[1]);
	assert(sscanf(argv[2], "%d", &keywordCount) == 1);
	assert(keywordCount > 0);

	vector<int> v;
	map<int,int> tf;
	TRECInputStream input(fileno(stdin));
	InputToken token;
	while (input.getNextToken(&token)) {
		int id = lm.getTermID((char*)token.token);
		if (id < 0)
			continue;
		if (lm.getTermProbability(id) > 0.01)
			continue;
		v.push_back(id);
		if (tf.find(id) == tf.end())
			tf[id] = 0;
		tf[id]++;
	}

	map<int,double> kldScores;
	for (map<int,int>::iterator iter = tf.begin(); iter != tf.end(); ++iter) {
		int id = iter->first, f = iter->second;
		double p = f * 1.0 / v.size();
		double q = lm.getTermProbability(id);
		double pSmoothed = 0.8 * p + 0.2 * q;
		double kld = pow(pSmoothed, 0.5) * log(pSmoothed / q);
		kldScores[id] = kld;
	}

	int dl = v.size();
#if 1
	static const int WINDOW_SIZE = 3;
	int best = 0;
	double bestScore = 0;
	map<int,int> windowTF;
	for (int i = 0 ; i < WINDOW_SIZE; i++) {
		if (windowTF.find(v[i]) == windowTF.end())
			windowTF[v[i]] = 0;
		windowTF[v[i]]++;
		if (windowTF[v[i]] == 1)
			bestScore += kldScores[v[i]];
	}
	double score = bestScore;
	for (int i = 1; i < dl - WINDOW_SIZE + 1; i++) {
		windowTF[v[i - 1]]--;
		if (windowTF[v[i - 1]] == 0)
			score -= kldScores[v[i - 1]];
		if (windowTF.find(v[i + WINDOW_SIZE - 1]) == windowTF.end())
			windowTF[v[i + WINDOW_SIZE - 1]] = 0;
		windowTF[v[i + WINDOW_SIZE - 1]]++;
		if (windowTF[v[i + WINDOW_SIZE - 1]] == 1)
			score += kldScores[v[i + WINDOW_SIZE - 1]];
		if (score > bestScore) {
			bestScore = score;
			best = i;
		}
	}
	printf("best passage:");
	for (int i = best; i < best + WINDOW_SIZE; i++)
		printf(" %s", lm.getTermString(v[i]));
	printf("\n");
#else
	int bestStart = 0, bestEnd = 0;
	double bestScore = 0;
	for (int w = 2; w <= 10; w++) {
		for (int start = 0; start <= dl - w; start++) {
			double score = 0;
			set<int> seen;
			for (int i = start; i < start + w; i++) {
				int id = v[i];
				double q = lm.getTermProbability(id);
				if (seen.find(id) == seen.end())
					score += kldScores[id] / log(w + 1);
				seen.insert(id);
			}
			if (score > bestScore) {
				bestScore = score;
				bestStart = start;
				bestEnd = start + w - 1;
			}
		}
	}
	printf("best passage:");
	for (int i = bestStart; i <= bestEnd; i++)
		printf(" %s", lm.getTermString(v[i]));
	printf("\n");
#endif

	vector< pair<double,int> > scores;
	for (map<int,int>::iterator iter = tf.begin(); iter != tf.end(); ++iter) {
		int id = iter->first, f = iter->second;
		double kld = kldScores[id];
		scores.push_back(pair<double,int>(kld, id));
	}

	sort(scores.begin(), scores.end());
	for (int i = 0; (i < scores.size()) && (i < keywordCount); i++) {
		int id = scores[scores.size() - 1 - i].second;
		char *term = lm.getTermString(id);
		printf("\"%s\": %.1lf\n", term, 0.0);
		free(term);
	}

	return 0;
}

