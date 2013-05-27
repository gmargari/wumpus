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
 * Implementation of the configuration manager.
 *
 * author: Stefan Buettcher
 * created: 2004-11-02
 * changed: 2009-02-01
 **/


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "configurator.h"
#include "../misc/all.h"


static bool configuratorInitialized = false;

static const char * LOG_ID = "Configurator";


#define HASHTABLE_SIZE 257

typedef struct {
	char *key;
	char *value;
	void *next;
} KeyValuePair;

static KeyValuePair *hashTable[HASHTABLE_SIZE];

static KeyValuePair configurationData[1024];
static int kvpCount = 0;

static const int CONFIG_VALUE_BUFFER_SIZE = MAX_CONFIG_VALUE_LENGTH + 256;
static char *currentBuffer = NULL;
static int currentBufferPos = CONFIG_VALUE_BUFFER_SIZE;


static bool defined(char *key) {
	if (key == NULL)
		return false;
	int32_t hashValue = simpleHashFunction(key) % HASHTABLE_SIZE;
	KeyValuePair *candidate = hashTable[hashValue];
	while (candidate != NULL) {
		if (strcmp(candidate->key, key) == 0)
			return true;
		candidate = (KeyValuePair*)candidate->next;
	}
	return false;
} // end of defined(char*)


static char *addToBuffer(char *string) {
	int len = strlen(string);
	if (currentBufferPos + len >= CONFIG_VALUE_BUFFER_SIZE) {
		currentBuffer = (char*)malloc(CONFIG_VALUE_BUFFER_SIZE);
		currentBufferPos = 0;
	}
	char *result = &currentBuffer[currentBufferPos];
	strcpy(result, string);
	currentBufferPos += (len + 1);
	return result;
} // end of addToBuffer(char*)


static void addToHashTable(char *key, char *value) {
	char message[256];
	if (key == NULL) {
		snprintf(message, 255, "Syntax error in configuration file: %s\n", key);
		log(LOG_ERROR, LOG_ID, message);
		return;
	}
	if (strlen(key) >= MAX_CONFIG_KEY_LENGTH - 2) {
		snprintf(message, 255, "Key too long in configuration file: %s\n", key);
		log(LOG_ERROR, LOG_ID, message);
		return;
	}
	if (strlen(value) >= MAX_CONFIG_VALUE_LENGTH - 2) {
		snprintf(message, 255, "Value too long in configuration file: %s\n", value);
		log(LOG_ERROR, LOG_ID, message);
		return;
	}

	int32_t hashValue = simpleHashFunction(key) % HASHTABLE_SIZE;
	KeyValuePair *kvp = &configurationData[kvpCount++];
	kvp->key = addToBuffer(key);
	kvp->value = addToBuffer(value);
	kvp->next = hashTable[hashValue];
	hashTable[hashValue] = kvp;

	if (strcasecmp(key, "LOG_LEVEL") == 0) {
		int logLevel;
		if (sscanf(value, "%d", &logLevel) == 1)
			setLogLevel(logLevel);
	}
	if (strcasecmp(key, "LOG_FILE") == 0) {
		if (strcmp(value, "stdout") == 0)
			setLogOutputStream(stdout);
		else if (strcmp(value, "stderr") == 0)
			setLogOutputStream(stderr);
		else {
			FILE *f = fopen(value, "a");
			setLogOutputStream(f);
		}
	}
} // end of addToHashTable(char*, char*)


static void processConfigFile(const char *fileName) {
	char line[65536];
	FILE *f = fopen(fileName, "r");
	if (f == NULL)
		return;
	while (fgets(line, 65534, f) != NULL) {
		if ((line[0] == '\n') || (line[0] == 0))
			continue;
		char *ptr = chop(line);
		if ((*ptr == '#') || (*ptr == 0)) {
			free(ptr);
			continue;
		}
		char *eq = strstr(ptr, "=");
		if (eq == NULL) {
			char message[256];
			snprintf(message, 255, "Syntax error in configuration file: %s\n", ptr);
			log(LOG_ERROR, LOG_ID, message);
			free(ptr);
			continue;
		}
		*eq = 0;
		char *key = chop(ptr);
		char *value = chop(&eq[1]);
		if (!defined(key))
			addToHashTable(key, value);
		free(ptr);
		free(key);
		free(value);
	}
	fclose(f);
} // end of processConfigFile(const char*)


void initializeConfiguratorFromCommandLineParameters(int argc, const char **argv) {
	bool configFileGivenAsParam = false;
	initializeConfigurator("/dev/null", "/dev/null");
	for (int i = 1; i < argc; i++) {
		StringTokenizer *tok = new StringTokenizer(argv[i], "=");
		char *key = chop(tok->getNext());
		char *value = chop(tok->getNext());
		if ((key != NULL) && (value != NULL)) {
			char *key2 = key;
			while (*key2 == '-')
				key2++;
			if ((strcasecmp(key2, "CONFIG") == 0) || (strcasecmp(key2, "CONFIGFILE") == 0)) {
				configFileGivenAsParam = true;
				initializeConfigurator(value, "/dev/null");
			}
			else
				addToHashTable(key2, value);
		}
		if (key != NULL)
			free(key);
		if (value != NULL)
			free(value);
		delete tok;
	}
	if (!configFileGivenAsParam) {
		char *configFile = getenv("WUMPUS_CONFIG_FILE");
		if (configFile != NULL)
			initializeConfigurator(configFile, "/dev/null");
	}
	initializeConfigurator();
} // end of initializeConfiguratorFromCommandLineParameters(int, char**)


void initializeConfigurator(const char *primaryFile, const char *secondaryFile) {
	if (!configuratorInitialized)
		for (int i = 0; i < HASHTABLE_SIZE; i++)
			hashTable[i] = NULL;
	if (primaryFile != NULL)
		processConfigFile(primaryFile);
	if (secondaryFile != NULL)
		processConfigFile(secondaryFile);
	configuratorInitialized = true;
} // end of initializeConfigurator(const char*, const char*)


void initializeConfigurator() {
	char *homeDir = getenv("HOME");
	char *primaryFile = NULL;
	if (homeDir != NULL) {
		primaryFile = (char*)malloc(strlen(homeDir) + 32);
		strcpy(primaryFile, homeDir);
		if (primaryFile[strlen(primaryFile) - 1] != '/')
			strcat(primaryFile, "/");
		strcat(primaryFile, ".wumpusconf");
	}
	initializeConfigurator(primaryFile, "/etc/wumpusconf");
	free(primaryFile);
} // end of initializeConfigurator()


bool getConfigurationValue(const char *key, char *value) {
	assert(configuratorInitialized == true);
	if (value == NULL)
		return false;
	int32_t hashValue = simpleHashFunction(key) % HASHTABLE_SIZE;
	KeyValuePair *result = hashTable[hashValue];
	while (result != NULL) {
		if (strcmp(result->key, key) == 0) {
			strcpy(value, result->value);
			return true;
		}
		result = (KeyValuePair*)result->next;
	}
	return false;
} // end of getConfigurationValue(const char*, char*)


bool getConfigurationInt(const char *key, int *value, int defaultValue) {
	char string[MAX_CONFIG_VALUE_LENGTH], string2[MAX_CONFIG_VALUE_LENGTH];
	*value = defaultValue;
	if (!getConfigurationValue(key, string2))
		return false;
	if (sscanf(string2, "%s", string) < 1)
		return false;
	int v = 0;
	for (int i = 0; string[i] != 0; i++) {
		if ((string[i] < '0') || (string[i] > '9')) {
			if (string[i + 1] != 0)
				return false;
			char c = (string[i] | 32);
			if ((c != 'm') && (c != 'k') && (c != 'g'))
				return false;
			if (c == 'k')
				*value = v * 1024;
			if (c == 'm')
				*value = v * 1024 * 1024;
			if (c == 'g')
				*value = v * 1024 * 1024 * 1024;
			return true;
		}
		v = v * 10 + (string[i] - '0');
	}
	*value = v;
	return true;
} // end of getConfigurationInt(const char*, int*)


bool getConfigurationInt64(const char *key, int64_t *value, int64_t defaultValue) {
	char string[MAX_CONFIG_VALUE_LENGTH], string2[MAX_CONFIG_VALUE_LENGTH];
	*value = defaultValue;
	if (!getConfigurationValue(key, string2))
		return false;
	if (sscanf(string2, "%s", string) < 1)
		return false;
	int64_t v = 0;
	for (int i = 0; string[i] != 0; i++) {
		if ((string[i] < '0') || (string[i] > '9')) {
			if (string[i + 1] != 0)
				return false;
			char c = (string[i] | 32);
			if ((c != 'm') && (c != 'k') && (c != 'g'))
				return false;
			if (c == 'k')
				*value = v * 1024;
			if (c == 'm')
				*value = v * 1024 * 1024;
			if (c == 'g')
				*value = v * 1024 * 1024 * 1024;
			return true;
		}
		v = v * 10 + (string[i] - '0');
	}
	*value = v;
	return true;
} // end of getConfigurationInt64(const char*, int64_t*, int64_t)


bool getConfigurationBool(const char *key, bool *value, bool defaultValue) {
	char string[MAX_CONFIG_VALUE_LENGTH];
	*value = defaultValue;
	if (!getConfigurationValue(key, string))
		return false;
	if ((strcasecmp(string, "true") == 0) || (strcmp(string, "1") == 0)) {
		*value = true;
		return true;
	}
	if ((strcasecmp(string, "false") == 0) || (strcmp(string, "0") == 0)) {
		*value = false;
		return true;
	}
	return false;
} // end of getConfigurationBool(const char*, bool*)


bool getConfigurationDouble(const char *key, double *value, double defaultValue) {
	char string[MAX_CONFIG_VALUE_LENGTH];
	double v;
	*value = defaultValue;
	if (!getConfigurationValue(key, string))
		return false;
	if (sscanf(string, "%lf", &v) != 1)
		return false;
	*value = v;
	return true;
} // end of getConfigurationDouble(const char*, double*, double)


char ** getConfigurationArray(const char *key) {
	char string[MAX_CONFIG_VALUE_LENGTH];
	if (!getConfigurationValue(key, string))
		return NULL;
	int cnt = 0;
	bool inQuotes = false;
	for (int i = 0; string[i] != 0; i++) {
		if (string[i] == '"')
			inQuotes = !inQuotes;
		if (!inQuotes)
			cnt++;
	}
	if ((inQuotes) || (cnt < 1))
		return NULL;
	char **result = typed_malloc(char*, cnt + 1);
	cnt = 0;
	int pos = 0;
	while (string[pos] != 0) {
		if (string[pos] == '"') {
			int start = ++pos;
			while (string[pos] != '"')
				pos++;
			string[pos++] = 0;
			result[cnt++] = duplicateString(&string[start]);
		}
		else
			pos++;
	}
	result[cnt] = NULL;
	return result;
} // end of getConfigurationArray(const char*)



