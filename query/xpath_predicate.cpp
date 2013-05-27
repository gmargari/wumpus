/**
 * Copyright (C) 2007 Stefan Buettcher. All rights reserved.
 * This is free software with ABSOLUTELY NO WARRANTY.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA
 **/

/**
 * author: Stefan Buettcher
 * created: 2004-12-04
 * changed: 2004-12-05
 **/


#include <math.h>
#include <stdio.h>
#include <string.h>
#include "xpath_predicate.h"
#include "../misc/all.h"


#define TYPE_NOT 1
#define TYPE_OR 2
#define TYPE_AND 3
#define TYPE_EQ 4
#define TYPE_NEQ 5
#define TYPE_LTE 6
#define TYPE_GTE 7
#define TYPE_LT 8
#define TYPE_GT 9
#define TYPE_ADD 10
#define TYPE_SUB 11
#define TYPE_MUL 12
#define TYPE_DIV 13
#define TYPE_MOD 14
#define TYPE_NUMBER 15
#define TYPE_FUNCTION 16

static const char * XPATH_OPERATORS[20] =
	{ "or", "and", "=", "!=", ">=", "<=", ">", "<",
	  "+", "-", "*", "div", "mod", NULL };

static const int XPATH_PREDICATE_TYPES[20] =
	{ TYPE_OR, TYPE_AND, TYPE_EQ, TYPE_NEQ, TYPE_GTE, TYPE_LTE, TYPE_GT, TYPE_LT,
	  TYPE_ADD, TYPE_SUB, TYPE_MUL, TYPE_DIV, TYPE_MOD };

static const char * XPATH_FUNCTIONS[32] =
	{ "string", "concat", "starts-with", "contains", "substring-before",
	  "substring-after", "substring", "string-length", "normalize-space",
	  "translate", "boolean", "not", "true", "false", "number", "sum",
	  "floor", "ceiling", "round", "product", "difference", "division",
	  "modulo", "last", "position", "first", "count", NULL };

static const int XPATH_FUNCTION_PARAMETER_COUNT[32] =
	{ 1, 2, 2, 2, 2, 2, 3, 1, 1, 3, 1, 1, 0, 0, 1, 2, 1, 1, 1, 2, 2, 2,
	  2, 0, 0, 0, 1, -1 };


XPathPredicate::XPathPredicate(char *string, Index *index) {
	this->index = index;
	subPredicate1 = NULL;
	subPredicate2 = NULL;
	subPredicate3 = NULL;
	functionName = NULL;
	syntaxError = false;

	char *pred = duplicateAndTrim(string);
	int len = strlen(pred);

	// printf("creating predicate: ...%s...%s...\n", string, pred);

	if (len == 0) {
		syntaxError = true;
		free(pred);
		return;
	}

	// remove unnecessary outer "("..")" pairs
	while (pred[0] == '(') {
		if (pred[len - 1] != ')') {
			syntaxError = true;
			free(pred);
			return;
		}
		pred[len - 1] = 0;
		char *newPred = duplicateAndTrim(pred);
		free(pred);
		pred = newPred;
		len = strlen(pred);
	}

	// check if this is a negation
	if ((strncasecmp(pred, "not(", 4) == 0) || (strncasecmp(pred, "not ", 4) == 0)) {
		type = TYPE_NOT;
		subPredicate1 = new XPathPredicate(&pred[3], index);
		free(pred);
		return;
	}

	// check all possible combination types (AND, OR, EQ, ...) in order of
	// their relative precedence
	for (int op = 0; XPATH_OPERATORS[op] != NULL; op++) {
		const char *optor = XPATH_OPERATORS[op];
		int optorLen = strlen(optor);
		bool inQuote = false;
		int bracketCnt = 0;
		for (int i = strlen(pred) - 1; i >= 0; i--) {
			if (inQuote) {
				if ((pred[i] == '"') && (pred[i - 1] != '\\'))
					inQuote = false;
			}
			else if (pred[i] == '"')
				inQuote = true;
			else if (pred[i] == ')')
				bracketCnt++;
			else if (pred[i] == '(') {
				bracketCnt--;
				if (bracketCnt < 0) {
					syntaxError = true;
					free(pred);
					return;
				}
			}
			if ((inQuote) || (bracketCnt != 0))
				continue;
			if (strncasecmp(&pred[i], optor, optorLen) == 0) {
				if ((pred[i + optorLen] == '(') ||
				    ((pred[i + optorLen] > 0) && (pred[i + optorLen] <= ' '))) {
					if (i == 0) {
						syntaxError = true;
						free(pred);
						return;
					}
					else if ((pred[i - 1] <= ' ') && (pred[i - 1] > 0)) {
						type = XPATH_PREDICATE_TYPES[op];
						pred[i] = 0;
						subPredicate1 = new XPathPredicate(pred, index);
						subPredicate2 = new XPathPredicate(&pred[i + optorLen], index);
						free(pred);
						return;
					}
				}
			}
		}
	} // end for (int op = 0; XPATH_OPERATORS[op] != NULL; op++)

	// if we get here, we can be sure that this is a non-composite predicate;
	// check for built-in predicates
	if (pred[strlen(pred) - 1] == ')') {
		for (int i = 0; XPATH_FUNCTIONS[i] != NULL; i++) {
			int funcLen = strlen(XPATH_FUNCTIONS[i]);
			if (strncasecmp(pred, XPATH_FUNCTIONS[i], funcLen) == 0)
				if (pred[funcLen] == '(') {
					bool inQuote = false;
					int bracketCnt = 0;
					int commaCnt = 0;
					int commas[4];
					for (int k = funcLen; pred[k] != 0; k++) {
						if (inQuote) {
							if ((pred[k] == '"') && (pred[k - 1] != '\\'))
								inQuote = false;
						}
						else if (pred[k] == '"')
							inQuote = true;
						else if (pred[k] == '(')
							bracketCnt++;
						else if (pred[k] == ')') {
							bracketCnt--;
							if (bracketCnt <= 0) {
								if (pred[k + 1] != 0)
									goto syntaxErrorTrue;
								break;
							}
						}
						else if ((bracketCnt == 0) && (pred[k] == ',')) {
							commas[commaCnt++] = k;
							if (commaCnt > 2)
								goto syntaxErrorTrue;
						}
					}
					if ((!inQuote) && (bracketCnt == 0)) {
						type = TYPE_FUNCTION;
						functionName = (char*)XPATH_FUNCTIONS[i];
						if (commaCnt == 0) {
							if (XPATH_FUNCTION_PARAMETER_COUNT[i] == 0) {
								for (int k = funcLen + 1; pred[k] != ')'; k++)
									if ((pred[k] < 0) || (pred[k] > ' '))
										goto syntaxErrorTrue;
							}
							else if (XPATH_FUNCTION_PARAMETER_COUNT[i] == 1)
								subPredicate1 = new XPathPredicate(&pred[funcLen], index);
							else
								goto syntaxErrorTrue;
						}
						else if (commaCnt == 1) {
							if (XPATH_FUNCTION_PARAMETER_COUNT[i] != 2)
								goto syntaxErrorTrue;
							else {
								pred[commas[0]] = 0;
								subPredicate1 = new XPathPredicate(&pred[funcLen], index);
								subPredicate2 = new XPathPredicate(&pred[commas[0] + 1], index);
							}
						}
						else if (commaCnt == 2) {
							pred[commas[0]] = pred[commas[1]] = 0;
							subPredicate1 = new XPathPredicate(&pred[funcLen], index);
							subPredicate2 = new XPathPredicate(&pred[commas[0] + 1], index);
							subPredicate3 = new XPathPredicate(&pred[commas[1] + 1], index);
						}
						else
							goto syntaxErrorTrue;
						free(pred);
						return;
					}
				}
		} // end for (int i = 0; predicates[i] != NULL; i++)
	}
	else {
		int predLen = strlen(pred);
		if (sscanf(pred, "%f", &numberValue) == 1) {
			type = TYPE_NUMBER;
			free(pred);
			return;
		}
	}

syntaxErrorTrue:
	syntaxError = true;
	free(pred);
	return;
} // end of XPathPredicate(char*, Index*)


XPathPredicate::~XPathPredicate() {
	if (subPredicate1 != NULL)
		delete subPredicate1;
	if (subPredicate2 != NULL)
		delete subPredicate2;
	if (subPredicate3 != NULL)
		delete subPredicate3;
} // end of ~XPathPredicate()


bool XPathPredicate::hasSyntaxError() {
	bool result = syntaxError;
	if (subPredicate1 != NULL)
		result = (result) && (subPredicate1->hasSyntaxError());
	if (subPredicate2 != NULL)
		result = (result) && (subPredicate2->hasSyntaxError());
	return result;
} // end of hasSyntaxError()


XPathData ** XPathPredicate::apply(XMLElementList *list) {
	XPathData **result1, **result2;
	switch (type) {
		case TYPE_NOT:
			result1 = subPredicate1->apply(list);
			for (int i = 0; i < list->length; i++) {
				if (result1[i]->dataType != XPATH_BOOLEAN) {
					XPathData *temp = XPath_boolean(result1[i]);
					XPath_deleteXPathData(result1[i]);
					result1[i] = temp;
				}
				result1[i]->booleanValue = !(result1[i]->booleanValue);
			}
			return result1;
		case TYPE_AND:
		case TYPE_OR:
			result1 = subPredicate1->apply(list);
			result2 = subPredicate2->apply(list);
			for (int i = 0; i < list->length; i++) {
				if (result1[i]->dataType != XPATH_BOOLEAN) {
					XPathData *temp = XPath_boolean(result1[i]);
					XPath_deleteXPathData(result1[i]);
					result1[i] = temp;
				}
				if (result2[i]->dataType != XPATH_BOOLEAN) {
					XPathData *temp = XPath_boolean(result2[i]);
					XPath_deleteXPathData(result2[i]);
					result2[i] = temp;
				}
				if (type == TYPE_AND)
					result1[i]->booleanValue = (result1[i]->booleanValue) && (result2[i]->booleanValue);
				else if (type == TYPE_OR)
					result1[i]->booleanValue = (result1[i]->booleanValue) || (result2[i]->booleanValue);
				XPath_deleteXPathData(result2[i]);
			}
			free(result2);
			return result1;
		case TYPE_ADD:
		case TYPE_SUB:
		case TYPE_MUL:
		case TYPE_DIV:
		case TYPE_MOD:
			result1 = subPredicate1->apply(list);
			result2 = subPredicate2->apply(list);
			for (int i = 0; i < list->length; i++) {				
				int int1, int2;
				float value1 = XPath_extractNumber(result1[i]);
				XPath_deleteXPathData(result1[i]);
				float value2 = XPath_extractNumber(result2[i]);
				XPath_deleteXPathData(result2[i]);
				switch (type) {
					case TYPE_ADD:
						value1 += value2;
						break;
					case TYPE_SUB:
						value1 -= value2;
						break;
					case TYPE_MUL:
						value1 *= value2;
						break;
					case TYPE_DIV:
						if (value2 != 0.0)
							value1 /= value2;
						break;
					case TYPE_MOD:
						int1 = (int)LROUND(value1);
						int2 = (int)LROUND(value2);
						if (int2 == 0)
							value1 = 0.0;
						else
							value1 = int1 % int2;
						break;
				}
				result1[i] = XPath_createXPathData(index, value1);
			}
			free(result2);
			return result1;
		case TYPE_EQ:
		case TYPE_NEQ:
		case TYPE_LTE:
		case TYPE_GTE:
		case TYPE_LT:
		case TYPE_GT:
			result1 = subPredicate1->apply(list);
			result2 = subPredicate2->apply(list);
			for (int i = 0; i < list->length; i++) {
				XPathData *xpd = XPath_compare(result1[i], result2[i], type);
				bool result = xpd->booleanValue;
				XPath_deleteXPathData(xpd);
				XPath_deleteXPathData(result1[i]);
				XPath_deleteXPathData(result2[i]);
				result1[i] = XPath_createXPathData(index, result);
			}
			free(result2);
			return result1;
		case TYPE_NUMBER:
			result1 = typed_malloc(XPathData*, list->length);
			for (int i = 0; i < list->length; i++)
				result1[i] = XPath_createXPathData(index, numberValue);
			return result1;
		case TYPE_FUNCTION:
			return applyFunction(list);
		default:
			return NULL;
	}
} // end of apply(XMLElementList*)


XPathData ** XPathPredicate::applyFunction(XMLElementList *list) {
	XPathData **result = typed_malloc(XPathData*, list->length);
	if (strcasecmp(functionName, "true") == 0) {
		for (int i = 0; i < list->length; i++)
			result[i] = XPath_createXPathData(index, true);
	}
	else if (strcasecmp(functionName, "false") == 0) {
		for (int i = 0; i < list->length; i++)
			result[i] = XPath_createXPathData(index, false);
	}
	else if (strcasecmp(functionName, "first") == 0) {
		for (int i = 0; i < list->length; i++)
			if (i == 0)
				result[i] = XPath_createXPathData(index, true);
			else
				result[i] = XPath_createXPathData(index, false);
	
	}
	else if (strcasecmp(functionName, "last") == 0) {
		for (int i = 0; i < list->length; i++)
			if (i == list->length - 1)
				result[i] = XPath_createXPathData(index, true);
			else
				result[i] = XPath_createXPathData(index, false);
	}
	else if (strcasecmp(functionName, "position") == 0) {
		for (int i = 0; i < list->length; i++)
			result[i] = XPath_createXPathData(index, static_cast<float>(i + 1));
	}
	else if (strcasecmp(functionName, "count") == 0) {
		XPathData **subResult = subPredicate1->apply(list);
		for (int i = 0; i < list->length; i++) {
			if (subResult[i]->dataType == XPATH_NODESET)
				result[i] = XPath_createXPathData(index, static_cast<float>(subResult[i]->nodeValue->length));
			else
				result[i] = XPath_createXPathData(index, 0.0f);
			XPath_deleteXPathData(subResult[i]);
		}
		free(subResult);
	}
	else if (strcasecmp(functionName, "string") == 0) {
		XPathData **subResult = subPredicate1->apply(list);
		for (int i = 0; i < list->length; i++) {
			result[i] = XPath_string(subResult[i]);
			XPath_deleteXPathData(subResult[i]);
		}
		free(subResult);
	}
	else if (strcasecmp(functionName, "concat") == 0) {
		XPathData *arguments[3];
		XPathData **subResult1 = subPredicate1->apply(list);
		XPathData **subResult2 = subPredicate2->apply(list);
		arguments[2] = NULL;
		for (int i = 0; i < list->length; i++) {
			arguments[0] = subResult1[i];
			arguments[1] = subResult2[i];
			result[i] = XPath_concat(arguments);
			XPath_deleteXPathData(subResult1[i]);
			XPath_deleteXPathData(subResult2[i]);
		}
		free(subResult1);
		free(subResult2);
	}
	else
		for (int i = 0; i < list->length; i++)
			result[i] = XPath_createXPathData(index, false);
	return result;
} // end of applyFunction(XMLElementList*)

#if 0
	{ "starts-with", "contains", "substring-before",
	  "substring-after", "substring", "string-length", "normalize-space",
	  "translate", "boolean", "not", "true", "false", "number", "sum",
	  "floor", "ceiling", "round", "product", "difference", "division",
	  "modulo", "last", "position", "first", "count", NULL };
#endif



