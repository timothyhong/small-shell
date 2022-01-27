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
#define PID_SYMBOL "$$"
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

/* Struct for commands */
typedef struct command_t {
	char* command;
	int numArgs;
	char** args; // pointer to array of char pointers
	char* inputFile;
	char* outputFile;
	int isBackground; // 0 if foreground, 1 if background
	int isComment; // 0 if not comment, 1 if comment
	char* comment;
} command_t;

command_t* createCommand(char* line) {
	command_t* currCommand = malloc(sizeof(*currCommand));
	initCommand(currCommand); // set default values

	// copy the line for second parse
	char* lineCpy = malloc(strlen(line) * sizeof(char) + 1);
	strcpy(lineCpy, line);

	char* savePtr = NULL;

	int commentFound = 0;
	int commandFound = 0;
	int argsFound = 0;
	int inputFound = 0;
	int outputFound = 0;
	int backgroundFound = 0;

	// first token is the command or a comment
	char* token = strtok_r(line, " ", &savePtr);
	while (token != NULL) {
		if (!commandFound) {
			// check if it's the COMMENT_CHAR (#)
			if (strcmp(token, COMMENT_CHAR) == 0) {
				currCommand->isComment = 1;
				token = strtok_r(NULL, "", &savePtr);
				currCommand->comment = malloc(sizeof(char) * strlen(token) + 1);
				strcpy(currCommand->comment, token);
				commentFound = 1;
				commandFound = 1;
			}
			// otherwise it's treated as a command
			else {
				currCommand->command = malloc(sizeof(char) * strlen(token) + 1);
				strcpy(currCommand->command, token);
				commandFound = 1;
			}
		}
		else if (!argsFound) {
			// find numArgs
			int numArgs = 0;

			char* tempPtr = NULL;
			char* tempToken = strtok_r(lineCpy + strlen(currCommand->command) + 1, " ", &tempPtr); // skip command
			while (tempToken != NULL && strcmp(tempToken, INPUT_CHAR) != 0 
				&& strcmp(tempToken, OUTPUT_CHAR) != 0 
				&& strcmp(tempToken, BACKGROUND_CHAR) != 0) {
				numArgs++;
				tempToken = strtok_r(NULL, " ", &tempPtr);
			}
			currCommand->numArgs = numArgs;
			free(lineCpy);

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
		return NULL;
	}
	return currCommand;
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
	while (nread == 1) {
		printf(PROMPT_CHAR);
		nread = getline(bufPtr, size, stdin);
	}
	return *bufPtr;
}

/*
 * Function: printCommand
 * ----------------------------
 *   Prints out a given command struct
 *
 *   command: a pointer to the command struct to print out
 */
void printCommand(command_t* command) {
	if (command->isComment) {
		printf("Comment: True\n");
		if (command->comment) {
			printf("Comment Text: %s\n", command->comment);
		}
		else {
			printf("No comment text.\n");
		}
	}
	else {
		printf("Comment: False\n");
	}

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
	command->isComment = 0; // 0 if not comment, 1 if comment
	command->comment = NULL;
}

/*
 * Function: destroyCommand
 * ----------------------------
 *   Frees dynamically allocated memory associated with given command structure
 *
 *   command: a pointer to the command structure to destroy
 */
void destroyCommand(command_t* command) {
	if (command->args) {
		free(command->args);
	}
	if (command->comment) {
		free(command->comment);
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


