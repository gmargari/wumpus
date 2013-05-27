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
 * The configurator interface helps us to quickly read system-wide
 * configuration information from config files and pass them to the
 * class where they belong.
 *
 * author: Stefan Buettcher
 * created: 2004-11-02
 * changed: 2005-09-20
 **/


#ifndef __MISC__CONFIGURATOR_H
#define __MISC__CONFIGURATOR_H


#include <sys/types.h>
#include "all.h"


#define MAX_CONFIG_KEY_LENGTH 128
#define MAX_CONFIG_VALUE_LENGTH 4096


// We use this as a access modifier to show which member variables get their
// values from the configuration manager.
#define configurable


/** Initializes the configurator using data given by cmd-line parameters. **/
void initializeConfiguratorFromCommandLineParameters(int argc, const char **argv);

/**
 * Initializes the configuration manager with config data found in two files.
 * The primary file is the user-specific configuration, the secondary file
 * is the system-wide configuration.
 **/
void initializeConfigurator(const char *primaryFile, const char *secondaryFile);

/**
 * Initializes the configuration manager with data found in ~/.indexconf and
 * /etc/indexconf.
 **/
void initializeConfigurator();

/**
 * Searches for the entry "key" in the configuration database and
 * writes the value into the memory referenced by "value". If the key cannot
 * be found, "false" is returned ("true" otherwise).
 **/
bool getConfigurationValue(const char *key, char *value);

/**
 * Reads an integer value from the configuration file. Multipliers K, M, and G for
 * 2^10, 2^20, and 2^30, respectively, are supported. For example, a value of
 * 2M in the configuration file is interpreted as 2^21.
 **/
bool getConfigurationInt64(const char *key, int64_t *value, int64_t defaultValue);

/** Same as above. **/
bool getConfigurationInt(const char *key, int *value, int defaultValue);

/** Same as above. **/
bool getConfigurationBool(const char *key, bool *value, bool defaultValue);

/** Same as above. **/
bool getConfigurationDouble(const char *key, double *value, double defaultValue);

/**
 * Returns a NULL-terminated array of character strings, representing the
 * configuration value for the given key. If the configuration variable does not
 * contain an array, or the array is syntactically incorrect, or the value is
 * not specified, then this function returns NULL. All memory has to be freed
 * by the caller.
 **/
char **getConfigurationArray(const char *key);


#endif


