/**
 * This file contains the implementation of the HelpQuery class.
 *
 * author: Stefan Buettcher
 * created: 2007-03-13
 * changed: 2009-02-01
 **/


#include <string.h>
#include "helpquery.h"


HelpQuery::HelpQuery(Index *index, const char *command, const char **modifiers, const char *body, uid_t userID, int memoryLimit) {
	assert(strcasecmp(command, HELP_COMMAND) == 0);
	queryString = duplicateAndTrim(body);
	cmd = (queryString[0] == '@' ? &queryString[1] : queryString);
	helpText = NULL;
	ok = false;
} // end of HelpQuery(Index*, char*, char*, uid_t)


HelpQuery::~HelpQuery() {
	if (helpText != NULL) {
		free(helpText);
		helpText = NULL;
	}
} // end of ~HelpQuery()


bool HelpQuery::parse() {
	if (strlen(queryString) == 0)
		helpText = getQueryCommandSummary();
	else
		helpText = getQueryHelpText(cmd);
	ok = (helpText != NULL);
	return ok;
} // end of parse()


bool HelpQuery::getNextLine(char *line) {
	if (helpText != NULL) {
		int len = strlen(helpText);
		if (len > 0)
			if (helpText[len - 1] == '\n')
				helpText[len - 1] = 0;
		const char *separator = "-------------------------------------------------------------------------------";
		sprintf(line, "%s\n%s\n%s", separator, helpText, separator);
		free(helpText);
		helpText = NULL;
		return true;
	}
	else
		return false;
} // end of getNextLine(char*)


bool HelpQuery::getStatus(int *code, char *description) {
	if (ok) {
		*code = STATUS_OK;
		strcpy(description, "Ok.");
	}
	else {
		*code = STATUS_ERROR;
		sprintf(description, "Command \"%s\" not found.", queryString);
	}
} // end of getStatus(int*, char*)


bool HelpQuery::isValidCommand(const char *command) {
	return (strcasecmp(command, HELP_COMMAND) == 0);
} // end of isValidCommand(char*)


int HelpQuery::getType() {
	return QUERY_TYPE_HELP;
}



