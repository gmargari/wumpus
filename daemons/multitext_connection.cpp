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
 * Implementation of the MultiTextConnection class.
 *
 * author: Stefan Buettcher
 * created: 2005-07-20
 * changed: 2005-07-20
 **/


#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "multitext_connection.h"
#include "../index/index.h"
#include "../misc/all.h"
#include "../misc/stringtokenizer.h"


static const char * OK_STRING = "@0-Ok. (0 ms total, 0 ms CPU)\n";

static const char * MODE_STRINGS[5] = {
	"@normal",
	"@count",
	"@estimate",
	"@histogram",
	NULL
};

static const char * DOCUMENTS = "\"<doc>\"..\"</doc>\"";

static const char * DOCNOS = "[add=\"<docno>\"..\"</docno>\"]";


MultiTextConnection::MultiTextConnection(Index *index, int fd, uid_t userID) {
	initialize(index, fd, userID);

	workMode = WORKMODE_NORMAL;
	limit = DEFAULT_LIMIT;
	responseMode = RESPONSE_MODE_NORMAL;
	startOffsetForGetQuery = -1;
} // end of MultiTextConnection(Index*, int, uid_t)


MultiTextConnection::~MultiTextConnection() {
}


bool MultiTextConnection::changeWorkMode(char *command) {
	// iterate over all possible work modes in order to find out whether the
	// current command corresponds to one of them
	for (int i = 0; MODE_STRINGS[i] != NULL; i++) {
		if (strncasecmp(MODE_STRINGS[i], command, strlen(MODE_STRINGS[i])) == 0) {
			if (command[strlen(MODE_STRINGS[i])] == 0) {
				if (workMode == i)
					workMode = WORKMODE_NORMAL;
				else
					workMode = i;
				return true;
			}
			if (command[strlen(MODE_STRINGS[i])] == ' ') {
				int value;
				if (sscanf(&command[strlen(MODE_STRINGS[i]) + 1], "%d", &value) == 1) {
					if (value == 0) {
						if (workMode == i)
							workMode = WORKMODE_NORMAL;
					}
					else
						workMode = i;
					return true;
				}
			}
		}
	}
	return false;
} // end of changeWorkMode(char*)


int MultiTextConnection::transformScorers(char *oldSequence, char *newSequence) {
	int outPos = 0;
	StringTokenizer *tok = new StringTokenizer(oldSequence, " \t");
	while (tok->hasNext()) {
		char *token = tok->getNext();
		if (token[0] == 0)
			continue;
		if (token[0] == '#')
			outPos += sprintf(&newSequence[outPos], " %s", token);
		else if ((token[0] >= '0') && (token[0] <= '9'))
			outPos += sprintf(&newSequence[outPos], " %s,", token);
		else if ((token[0] == '[') || (token[0] == '"') || (token[0] == '('))
			outPos += sprintf(&newSequence[outPos], " %s,", token);
		else
			outPos += sprintf(&newSequence[outPos], " \"%s\",", token);
	}
	delete tok;
	if (outPos > 0)
		if (newSequence[outPos - 1] == ',')
			outPos--;
	newSequence[outPos] = 0;
	return outPos;
} // end of transformScorers(char*, char*)


int MultiTextConnection::processLine(char *line) {
	// remove leading whitespaces
	while ((*line > 0) && (*line <= ' '))
		line++;

	if (changeWorkMode(line))
		return sendMessage(OK_STRING);

	if (strcasecmp(line, "@unlimit") == 0) {
		limit = MAX_LIMIT;
		return sendMessage(OK_STRING);
	}

	if (strncasecmp(line, "@limit ", 7) == 0) {
		int value = -1;
		sscanf(&line[7], "%d", &value);
		if ((value > 0) && (value <= MAX_LIMIT)) {
			limit = value;
			return sendMessage(OK_STRING);
		}
		else
			return sendMessage("1-Illegal value. (0 ms total, 0 ms CPU)\n");
	}

	char *newLine = (char*)malloc(strlen(line) * 2 + 64);
	int newLineLen = 0, oldLinePos = 0;

	if (strncasecmp(line, "@okapi ", 7) == 0) {
		oldLinePos = 7;
		newLineLen =
			sprintf(newLine, "@okapi[count=%d]%s %s by ", limit, DOCNOS, DOCUMENTS);
	}
	else if (strncasecmp(line, "@okapiw ", 8) == 0) {
		oldLinePos = 8;
		newLineLen =
			sprintf(newLine, "@okapi[count=%d][noidf]%s %s by ", limit, DOCNOS, DOCUMENTS);
	}
	else if (strncasecmp(line, "@qap ", 5) == 0) {
		oldLinePos = 5;
		newLineLen =
			sprintf(newLine, "@qap[count=%d]%s %s by ", limit, DOCNOS, DOCUMENTS);
		responseMode = RESPONSE_MODE_QAP;
	}
	else if (strncasecmp(line, "@qa ", 4) == 0) {
		oldLinePos = 5;
		newLineLen =
			sprintf(newLine, "@qa[count=%d]%s %s by ", limit, DOCNOS, DOCUMENTS);
		responseMode = RESPONSE_MODE_QA;
	}

	// process ugly @get queries, consisting of a line that only contains numbers
	if (strlen(line) < 40) {
		offset start, end;
		char from[40], to[40], dummy[40], getQuery[64];
		int n = sscanf(line, "%s%s%s", from, to, dummy);
		switch (n) {
			case 1:
				if (sscanf(line, OFFSET_FORMAT, &start) == 1) {
					if (startOffsetForGetQuery >= 0) {
						sprintf(newLine, "@get " OFFSET_FORMAT " " OFFSET_FORMAT "\n",
								startOffsetForGetQuery, start);
						responseMode = RESPONSE_MODE_GET;
						endOffsetForGetQuery = start;
						int result = ClientConnection::processLine(newLine);
						free(newLine);
						startOffsetForGetQuery = -1;
						return result;
					}
					else {
						startOffsetForGetQuery = start;
						strcpy(fromString, from);
						free(newLine);
						return 0;
					}
				}
				break;
			case 2:
				if (sscanf(line, OFFSET_FORMAT OFFSET_FORMAT, &start, &end) == 2) {
					sprintf(newLine, "@get %s %s\n", from, to);
					responseMode = RESPONSE_MODE_GET;
					startOffsetForGetQuery = start;
					endOffsetForGetQuery = end;
					strcpy(fromString, from);
					int result = ClientConnection::processLine(newLine);
					free(newLine);
					startOffsetForGetQuery = -1;
					return result;
				}
				break;
		}
	} // end if (strlen(line) < 40)

	if (newLineLen > 0)
		newLineLen += transformScorers(&line[oldLinePos], &newLine[newLineLen]);
	else if (line[0] == '@') {
		if (strncasecmp(line, "@documentsContaining ", strlen("@documentsContaining ")) == 0) {
			newLineLen = sprintf(newLine, "@docs ");
			char *p = &line[strlen("@documentsContaining ")];
			while ((*p > 0) && (*p <= ' '))
				p++;
			if ((p[0] == '"') || (p[0] == '(') || (p[0] == '['))
				newLineLen += sprintf(&newLine[newLineLen], "%s", p);
			else
				newLineLen += sprintf(&newLine[newLineLen], "\"%s\"", p);
		}
		else
			newLineLen = sprintf(newLine, "%s", line);
	}
	else {
		switch (workMode) {
			case WORKMODE_NORMAL:
				newLineLen = sprintf(newLine, "@gcl[count=%d] ", limit);
				break;
			case WORKMODE_COUNT:
				newLineLen = sprintf(newLine, "@count ");
				break;
			case WORKMODE_ESTIMATE:
				newLineLen = sprintf(newLine, "@estimate ");
				break;
			case WORKMODE_HISTOGRAM:
				newLineLen = sprintf(newLine, "@histogram ");
				break;
			default:
				assert("This should never happen: Illegal work mode" == NULL);
		}
		if ((line[0] == '"') || (line[0] == '(') || (line[0] == '['))
			newLineLen += sprintf(&newLine[newLineLen], "%s", line);
		else
			newLineLen += sprintf(&newLine[newLineLen], "\"%s\"", line);
	} // end else [no leading '@' for this query]

	// send modified query to query processor
	int result = ClientConnection::processLine(newLine);
	free(newLine);
	responseMode = RESPONSE_MODE_NORMAL;
	return result;
} // end of processLine(char*)


int MultiTextConnection::sendMessage(const char *message) {
	char header[80];
	char newMessage[256], queryID[256], score[256], docStart[256], docEnd[256];
	char passageStart[256], passageEnd[256], docnoStart[256], docnoEnd[256];
	int headerLen, sent;

	if (responseMode == RESPONSE_MODE_GET) {
		if (message[0] == '@') {
			if (message[1] == '@')
				message = &message[1];
			else {
				message = "";
				responseMode = RESPONSE_MODE_NORMAL;
			}
		}
	}
	else if (message[0] == '@')
		responseMode = RESPONSE_MODE_NORMAL;

	switch (responseMode) {
		case RESPONSE_MODE_NORMAL:
			return ClientConnection::sendMessage(message);
		case RESPONSE_MODE_QA:
			if (strlen(message) > sizeof(newMessage) - 4)
				return ClientConnection::sendMessage(message);
			else {
				sscanf(message, "%s%s%s%s%s%s%s%s", queryID, score, docStart, docEnd,
						passageStart, passageEnd, docnoStart, docnoEnd);
				sprintf(newMessage, "%s %s %s %s %s %s %s %s\n", queryID, score,
						passageStart, passageEnd, docStart, docEnd, docnoStart, docnoEnd);
				return ClientConnection::sendMessage(newMessage);
			}
		case RESPONSE_MODE_QAP:
			if (strlen(message) > sizeof(newMessage) - 4)
				return ClientConnection::sendMessage(message);
			else {
				sscanf(message, "%s%s%s%s%s%s%s%s", queryID, score, docStart, docEnd,
						passageStart, passageEnd, docnoStart, docnoEnd);
				sprintf(newMessage, "%s %s %s %s %s %s %s %s\n", queryID, score,
						docStart, docEnd, docnoStart, docnoEnd, passageStart, passageEnd);
				return ClientConnection::sendMessage(newMessage);
			}
		case RESPONSE_MODE_GET:
			headerLen = 0;
			headerLen += sprintf(&header[headerLen], "%20s\n", fromString);
			headerLen += sprintf(&header[headerLen], "%20d\n",
					(int)((endOffsetForGetQuery + 1) - startOffsetForGetQuery));
			headerLen += sprintf(&header[headerLen], "%20d\n", static_cast<int>(strlen(message)));
			sent = ClientConnection::sendMessage(header);
			sent += ClientConnection::sendMessage(message);
			return sent;
		default:
			return ClientConnection::sendMessage(message);
	}
} // end of sendMessage(char*)


