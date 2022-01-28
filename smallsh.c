/*
Author: Timothy Hong
Course: CS344
Date: 01/26/2022
Program Description: This program is an implementation of a simple shell capable of the following:

-Provide a prompt for running commands
-Handle blank lines and comments, which are lines beginning with the # character
-Provide expansion for the variable $$
-Execute 3 commands exit, cd, and status via code built into the shell
-Execute other commands by creating new processes using a function from the exec family of functions
-Support input and output redirection
-Support running commands in foreground and background processes
-Implement custom handlers for 2 signals, SIGINT and SIGTSTP
*/

#define _GNU_SOURCE
#define PROMPT_CHAR ":"
#define BACKGROUND_CHAR "&"
#define INPUT_CHAR "<"
#define OUTPUT_CHAR ">"
#define COMMENT_CHAR "#"
#define VAR_EXP_CHAR "$$"
#define MAX_LENGTH 2048
#define MAX_ARGS 512
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include "smallsh.h"

/* Struct for commands */
typedef struct command_t {
	char* command;
	int numArgs;
	char** args; // pointer to array of char pointers
	char* inputFile;
	char* outputFile;
	int isBackground; // 0 if foreground, 1 if background
} command_t;

command_t* createCommand(char* line) {
	command_t* currCommand = malloc(sizeof(*currCommand));
	initCommand(currCommand); // set default values

	// copy the line (we will use it for a second parse)
	char* lineCpy = malloc(strlen(line) * sizeof(char) + 1);
	strcpy(lineCpy, line);

	char* savePtr = NULL;

	int commandFound = 0;
	int argsFound = 0;
	int inputFound = 0;
	int outputFound = 0;
	int backgroundFound = 0;

	// first token is the command or a comment
	char* token = strtok_r(line, " ", &savePtr);
	while (token != NULL) {
		if (!commandFound) {
			// perform variable expansion on $$
			char* pid = malloc(21);
			sprintf(pid, "%d", getpid());
			currCommand->command = expandCommand(token, VAR_EXP_CHAR, pid);
			free(pid);
			commandFound = 1;
		}
		else if (!argsFound) {
			// get numArgs
			int numArgs = 0;

			// use a copy of the string to parse through args to get numArgs
			char* tempPtr = NULL;
			char* tempToken = strtok_r(lineCpy, " ", &tempPtr);
			tempToken = strtok_r(NULL, " ", &tempPtr); // advance it once to skip command
			while (tempToken != NULL && strcmp(tempToken, INPUT_CHAR) != 0 
				&& strcmp(tempToken, OUTPUT_CHAR) != 0 
				&& strcmp(tempToken, BACKGROUND_CHAR) != 0) {
				numArgs++;
				tempToken = strtok_r(NULL, " ", &tempPtr);
			}
			currCommand->numArgs = numArgs;

			// store args
			currCommand->args = malloc(sizeof(char*) * numArgs); // allocate space for numArgs char ptrs
			for (int i = 0; i < numArgs; i++) {
				currCommand->args[i] = token;
				token = strtok_r(NULL, " ", &savePtr);
			}
			argsFound = 1;
		}
		token = strtok_r(NULL, " ", &savePtr);
	}

	// NULL token -> exit
	if (!commandFound) {
		free(currCommand);
		free(lineCpy);
		return NULL;
	}
	free(lineCpy);
	return currCommand;
}

/*
 * Function: expandCommand
 * ----------------------------
 *   Takes a command string and expands out the VAR_EXP_CHAR into smallsh's PID.
 *
 *	 commandStr: the command string to expand
 *   expStrFrom: the variable string to expand from
 *   expStrTo: the variable string to expand to
 *
 *   returns: a pointer to the expanded string
 * 
 *	 notes: must free returned string
 */

char* expandCommand(char* commandStr, char* expStrFrom, char* expStrTo) {
	// count variable strings to expand from
	int i = 0;
	int newLength = strlen(commandStr);
	int adjustedLength = strlen(expStrTo) - strlen(expStrFrom);
	
	// figure out how much space to allocate for expanded string
	while (i < strlen(commandStr)) {
		// if match, allot room for expansion
		if (strncmp(&commandStr[i], expStrFrom, strlen(expStrFrom)) == 0) {
			newLength += adjustedLength;
			i += strlen(expStrFrom);
		}
		// doesn't match
		else {
			i++;
		}
	}
	i = 0;

	// allocate the space for expanded string
	char* expandedStr = malloc(sizeof(char) * newLength + 1);
	memset(expandedStr, '\0', newLength + 1);

	// now copy the expanded string
	while (i < strlen(commandStr)) {
		if (strncmp(&commandStr[i], expStrFrom, strlen(expStrFrom)) == 0) {
			strncat(expandedStr, expStrTo, strlen(expStrTo));
			i += strlen(expStrFrom);
		}
		// doesn't match
		else {
			strncat(expandedStr, &commandStr[i], 1);
			i++;
		}
	}
	return expandedStr;
}

/*
 * Function: getCommand
 * ----------------------------
 *   Gets a user-inputted command from stdin.
 *
 *   bufPtr: a double pointer to the first character in the string
 *   size: a pointer to the size of the buffer
 *
 *   returns: a pointer to the first character in the string
 *
 *   notes: bufPtr must be freed later
 */
char* getCommand(char** bufPtr, size_t* size) {
	size_t nread;
	printf(PROMPT_CHAR);
	nread = getline(bufPtr, size, stdin);
	// input validation (in case user just presses enter)
	// also check if empty space or if user starts with COMMENT_CHAR (#)
	while (nread == 1 || strncmp(*bufPtr, COMMENT_CHAR, 1) == 0 || isEmptyString(*bufPtr)) {
		printf(PROMPT_CHAR);
		nread = getline(bufPtr, size, stdin);
	}
	// strip new line
	if ((*bufPtr)[nread - 1] == '\n') {
		(*bufPtr)[nread - 1] = '\0';
	}
	return *bufPtr;
}
/*
 * Function: isEmptyString
 * ----------------------------
 *   Checks if a given string is all empty space.
 *
 *   s: a pointer to the string to check
 * 
 *	 returns: 0 if not empty; 1 if empty
 */
int isEmptyString(char* s) {
	for (int i = 0; i < strlen(s); i++) {
		if (!isspace(*s)) {
			return 0;
		}
	}
	return 1;
}

/*
 * Function: printCommand
 * ----------------------------
 *   Prints out a given command struct
 *
 *   command: a pointer to the command struct to print out
 */
void printCommand(command_t* command) {

	if (command->isBackground) {
		printf("Background: True\n");
	}
	else {
		printf("Background: False\n");
	}

	if (command->command) {
		printf("Command Name: %s\n", command->command);
	}
	else {
		printf("Not a command.\n");
	}

	printf("numArgs: %d\n", command->numArgs);

	for (int i = 0; i < command->numArgs; i++) {
		printf("Arg %d: %s\n", i, command->args[i]);
	}
	
	if (command->inputFile) {
		printf("Input File: %s\n", command->inputFile);
	}
	else {
		printf("No input file.\n");
	}

	if (command->outputFile) {
		printf("Output File: %s\n", command->outputFile);
	}
	else {
		printf("No output file.\n");
	}
}

/*
 * Function: initCommand
 * ----------------------------
 *   Initializes a given command struct with default values.
 *
 *   command: a pointer to the command struct to print out
 */
void initCommand(command_t* command) {
	command->command = NULL;
	command->numArgs = 0;
	command->args = 0;
	command->inputFile = NULL;
	command->outputFile = NULL;
	command->isBackground = 0; // 0 if foreground, 1 if background
}

/*
 * Function: destroyCommand
 * ----------------------------
 *   Frees dynamically allocated memory associated with given command structure
 *
 *   command: a pointer to the command structure to destroy
 */
void destroyCommand(command_t* command) {
	if (command->command) {
		free(command->command);
	}
	if (command->args) {
		free(command->args);
	}
	if (command->inputFile) {
		free(command->inputFile);
	}
	if (command->outputFile) {
		free(command->outputFile);
	}
	free(command);
}

/*
 * Function: startShell
 * ----------------------------
 *   Starts the shell
 *
 *   returns: 0 if success, -1 if error
 */
int startShell(void) {
	int exit = 0;
	while (!exit) {
		char* buffPtr = NULL;
		size_t size = 0;
		char* currLine = NULL;

		currLine = getCommand(&buffPtr, &size);
		command_t* currCommand = createCommand(currLine);
		printCommand(currCommand);
		if (strcmp(currCommand->command, "exit") == 0) {
			exit = 1;
		}
		free(buffPtr);
		destroyCommand(currCommand);
	}
	return 0;
}

/*
* To compile: gcc --std=c99 -Wall -g -o smallsh smallsh.c
*/
int main(int argc, char* argv[])
{
	if (argc > 1)
	{
		printf("No arguments needed.\n");
		printf("Example usage: ./smallsh\n");
		return EXIT_FAILURE;
	}
	startShell();
}


