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
#define EXIT_CMD "exit"
#define CD_CMD "cd"
#define STATUS_CMD "status"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

	int inputFound = 0;
	int outputFound = 0;

	// first token is the command
	char* token = strtok_r(line, " ", &savePtr);

	// get pid
	char* pid = malloc(21);
	sprintf(pid, "%d", getpid());

	// perform variable expansion on $$
	currCommand->command = expandCommand(token, VAR_EXP_CHAR, pid);

	token = strtok_r(NULL, " ", &savePtr);

	// look for args
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
		currCommand->args[i] = expandCommand(token, VAR_EXP_CHAR, pid);
		token = strtok_r(NULL, " ", &savePtr);
	}

	while (token != NULL) {
		// input redirection filename
		if (strcmp(token, INPUT_CHAR) == 0 && !inputFound) {
			// next token will be input filename
			token = strtok_r(NULL, " ", &savePtr);
			if (token) {
				currCommand->inputFile = expandCommand(token, VAR_EXP_CHAR, pid);
				inputFound = 1;
			}
		}
		// output redirection filename
		else if (strcmp(token, OUTPUT_CHAR) == 0 && !outputFound) {
			// next token will be output filename
			token = strtok_r(NULL, " ", &savePtr);
			if (token) {
				currCommand->outputFile = expandCommand(token, VAR_EXP_CHAR, pid);
				outputFound = 1;
			}
		}
		else if (strcmp(token, BACKGROUND_CHAR) == 0) {
			token = strtok_r(NULL, " ", &savePtr);
			if (token == NULL) {
				// valid background char
				currCommand->isBackground = 1;
				break;
			}
			else {
				continue;
			}
		}
		token = strtok_r(NULL, " ", &savePtr);
	}

	free(lineCpy);
	free(pid);
	return currCommand;
}

/*
 * Function: expandCommand
 * ----------------------------
 *   Takes a string and replaces all instances of a given input substring with a given output substring.
 *   Replacement occurs left to right.
 *
 *	 str: the string to expand
 *   fromSubstr: the input substring to expand
 *   toSubstr: the output substring to expand to
 *
 *   returns: a pointer to the expanded string
 * 
 *	 notes: must free returned string
 */

char* expandCommand(char* str, char* fromSubstr, char* toSubstr) {
	// count variable strings to expand from
	int i = 0;
	int newLength = strlen(str);
	int adjustedLength = strlen(toSubstr) - strlen(fromSubstr);
	
	// figure out how much space to allocate for expanded string
	while (i < strlen(str)) {
		// if match, allot room for expansion
		if (strncmp(&str[i], fromSubstr, strlen(fromSubstr)) == 0) {
			newLength += adjustedLength;
			i += strlen(fromSubstr);
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
	while (i < strlen(str)) {
		if (strncmp(&str[i], fromSubstr, strlen(fromSubstr)) == 0) {
			strncat(expandedStr, toSubstr, strlen(toSubstr));
			i += strlen(fromSubstr);
		}
		// doesn't match
		else {
			strncat(expandedStr, &str[i], 1);
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
 * Function: changeDirectory
 * ----------------------------
 *   Wrapper for the chdir() function. If command does not contain any args, changes to the
 *   default HOME directory (i.e. getenv("HOME
 *
 *   command: a pointer to the command struct
 *
 *	 returns: 0 if successful; 1 if unsuccessful
 */
int changeDirectory(command_t* command) {
	int retVal;
	// double check command is CD_CMD
	if (strcmp(command->command, CD_CMD) != 0) {
		return -1;
	}
	// if no args, cd to HOME
	if (command->numArgs == 0) {
		retVal = chdir(getenv("HOME"));
	}
	// otherwise do whatever chdir normally does, ignoring other args besides first
	else {
		retVal = chdir(command->args[0]);
	}
	if (retVal != 0) {
		perror("Error");
	}
	// for debugging
	else {
		size_t size = MAX_LENGTH;
		char* cwd = malloc(size);
		getcwd(cwd, size);
		printf("CWD: %s\n", cwd);
		free(cwd);
	}
	return retVal;
}

/*
 * Function: printStatus
 * ----------------------------
 *   Prints the status of the last terminated/completed wstatus
 *
 *   wstatus: a pointer to the int value
 */
void printStatus(int* wstatus) {
	// if nothing's been run yet
	if (wstatus == NULL) {
		printf("exit status 0\n");
	}
	// if exited successfully
	else if (WIFEXITED(wstatus)) {
		printf("exit status %d\n", WEXITSTATUS(wstatus));
	}
	// if signal termination
	else if (WIFSIGNALED(wstatus)) {
		printf("terminated by signal %d\n", WTERMSIG(wstatus));
	}
	else {
		printf("unknown exit status\n");
	}
}

/*
 * Function: spawnChildAndExecute
 * ----------------------------
 *   Spawns a new child and executes the given command
 *
 *   command: a pointer to the command struct
 *	 wstatus: a pointer to the int wstatus value
 * 
 *   returns: pointer to the int wstatus value
 */
int* spawnChildAndExecute(command_t* command, int* wstatus) {
	// build newargv[] array, add a spot for the command and the terminating NULL pointer
	char* newargv[command->numArgs + 2];

	newargv[command->numArgs + 1] = NULL;
	for (int i = 0; i < command->numArgs; i++) {
		newargv[i + 1] = malloc(strlen(command->args[i]) * sizeof(char) + 1);
		strcpy(newargv[i + 1], command->args[i]);
	}

	// Fork a new process
	pid_t spawnPid = fork();

	switch (spawnPid) {
	case -1:
		perror("fork()\n");
		exit(1);
		break;
	case 0:
		// child process
		printf("CHILD(%d) running %s command\n", getpid(), command->command);
		// Replace the current program with command->command
		execvp(newargv[0], newargv);
		// exec only returns if there is an error
		perror(command->command);
		exit(1);
		break;
	default:
		// parent process
		// free up newargv
		for (int i = 0; i < command->numArgs + 1; i++) {
			free(newargv[i]);
		}
		// wait for child
		spawnPid = waitpid(spawnPid, wstatus, 0);
		printf("PARENT(%d): child(%d) terminated.\n", getpid(), spawnPid);
		break;
	}
	return wstatus;
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
		for (int i = 0; i < command->numArgs; i++) {
			free(command->args[i]);
		}
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

	// track wstatus of last completed/terminated child process
	int* wstatus = NULL;

	while (!exit) {
		char* buffPtr = NULL;
		size_t size = 0;
		char* currLine = NULL;

		currLine = getCommand(&buffPtr, &size);
		command_t* currCommand = createCommand(currLine);
		printCommand(currCommand);
		if (strcmp(currCommand->command, EXIT_CMD) == 0) {
			// run process cleanup
			exit = 1;
		}
		else if (strcmp(currCommand->command, CD_CMD) == 0) {
			changeDirectory(currCommand);
		}
		else if (strcmp(currCommand->command, STATUS_CMD) == 0) {
			printStatus(wstatus);
		}
		else {
			// spawn child process and divert to exec()
			wstatus = spawnChildAndExecute(currCommand, wstatus);
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
	return EXIT_SUCCESS;
}


