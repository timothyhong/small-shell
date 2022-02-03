/*
Author: Timothy Hong
Course: CS344
Date: 01/26/2022
Program Description: This program is an implementation of a simple shell capable of the following:

-Provide a prompt for commands
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
#define MAX_PID_STR_SIZE 21
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
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

// global variable for signal handling
bool backgroundEnabled = true;

// global linked list to keep track of child processes
llNode* head = NULL;

/* Struct for commands */
typedef struct command_t {
	char* command;
	int numArgs;
	char** args; // pointer to array of char pointers
	char* inputFile;
	char* outputFile;
	bool isBackground;
} command_t;

/* Linked list struct for tracking child processes */
typedef struct llNode {
	pid_t pid;
	struct llNode* next;
	struct llNode* prev;
} llNode;

// Handler for SIGTSTP - enters foreground-only mode
void handle_SIGTSTP(int signo) {
	if (backgroundEnabled) {
		char const message[] = "Entering foreground-only mode (& is now ignored)\n";
		backgroundEnabled = false;
		write(STDOUT_FILENO, message, sizeof(message) - 1);
	}
	else {
		char const message[] = "Exiting foreground-only mode\n";
		backgroundEnabled = true;
		write(STDOUT_FILENO, message, sizeof(message) - 1);
	}
	write(STDOUT_FILENO, PROMPT_CHAR, 1);
}

// Handler for SIGCHLD
// Code adapted from instructor Ryan Gambord at https://edstem.org/us/courses/16718/discussion/1077321
void handle_SIGCHLD(int signo, siginfo_t* si, void* context) {
	int errno_sav = errno;

	int status;
	waitpid(si->si_pid, &status, 0);

	pid_t pid = si->si_pid;
	// check that pid exists in list of tracked background processes before printing
	llNode* currNode = head;
	while (currNode != NULL) {
		// if node is a tracked background process, print
		if (currNode->pid == pid) {
			char const str[] = "\nbackground pid ";
			write(STDOUT_FILENO, str, sizeof str - 1);

			int i = 1;

			// count digits
			while (pid / (i * 10) != 0) i *= 10;

			for (; 0 < i; i /= 10)
			{
				char c = (char)(pid / i) + '0'; // write ascii value of the int
				write(STDOUT_FILENO, &c, 1);
				pid = pid % i;
			}

			{
				char const str[] = " is done: ";
				write(STDOUT_FILENO, str, sizeof str - 1);
			}

			if (CLD_EXITED == si->si_code)
			{
				char const str[] = "exit value ";
				write(STDOUT_FILENO, str, sizeof str - 1);
			}
			else
			{
				char const str[] = "terminated by signal ";
				write(STDOUT_FILENO, str, sizeof str - 1);
			}

			{
				int status = si->si_status;
				int i = 1;
				while (status / (i * 10) != 0) i *= 10;

				for (; 0 < i; i /= 10)
				{
					char c = (char)(status / i) + '0';
					write(STDOUT_FILENO, &c, 1);
					status = status % i;
				}
			}

			write(STDOUT_FILENO, "\n", 1);
			write(STDOUT_FILENO, PROMPT_CHAR, 1);
			errno = errno_sav;
			// remove node
			removeFromChildList(head, pid);
		}
		currNode = currNode->next;
	}
}

/*
 * Function: addToChildList
 * ----------------------------
 *   Takes a childPid, creates a linked list node for it, and adds it to the beginning of the linked list
 *	 
 *	 head: the head of the linked list to add to (or NULL if none)
 *   childPid: the process id of the child to add to the linked list
 *
 *   returns: a pointer to the new head of the linked list
 *
 *	 notes: linked list must be freed using destroyChildList function
 */
llNode* addToChildList(llNode* head, pid_t childPid) {
	// create childNode as new head
	llNode* childNode = malloc(sizeof * childNode);
	childNode->pid = childPid;
	childNode->prev = NULL;
	childNode->next = head;
	if (head != NULL) {
		head->prev = childNode;
	}
	return childNode;
}

/*
 * Function: removeFromChildList
 * ----------------------------
 *   Takes a childPid and removes the first node in the linked list with that childPid
 *
 *	 head: the head of the linked list to add to remove from
 *   childPid: the process id of the child to remove from the linked list
 *
 *   returns: a pointer to the head of the linked list
 *
 *	 notes: linked list must be freed using destroyChildList function
 */
llNode* removeFromChildList(llNode* head, pid_t childPid) {
	if (head == NULL) {
		return head;
	}
	llNode* currNode = head;
	// advance currNode until we find node to remove
	llNode* prevNode = NULL;
	while (currNode != NULL) {
		if (currNode->pid == childPid) {
			// if there's a prev node, set it
			if (prevNode != NULL) {
				prevNode->next = currNode->next;
			}
			// otherwise, head needs to be reassigned
			else {
				head = currNode->next;
			}
			// if there's a next node, set it
			if (currNode->next != NULL) {
				currNode->next->prev = prevNode;
			}
			free(currNode);
			return head;
		}
		else {
			prevNode = currNode;
			currNode = currNode->next;
		}
	}
	return head;
}

/*
 * Function: printChildList
 * ----------------------------
 *   Takes the head of a linked list and prints out the nodes.
 *
 *	 head: the head of the linked list to print
 *
 *	 notes: linked list must be freed using destroyChildList function
 */
void printChildList(llNode* head) {
	if (head == NULL) {
		printf("LL is empty.\n");
		fflush(stdout);
	}
	llNode* tmp = head;
	int i = 1;
	while (tmp != NULL) {
		printf("Node %d PID: %d\n", i, tmp->pid);
		fflush(stdout);
		i++;
		tmp = tmp->next;
	}
}

/*
 * Function: destroyChildList
 * ----------------------------
 *   Takes the head node of a linked list created with addToChildList function and destroys each node.
 *
 *	 head: the head of the linked list to destroy
 */
void destroyChildList(llNode* head) {
	llNode* tmp;

	while (head != NULL)
	{
		tmp = head;
		head = head->next;
		free(tmp);
	}
}

/*
 * Function: createCommand
 * ----------------------------
 *   Given an inputted command line, parses out the command, arguments, input/output redirections, and
 *   background mode and creates a command structure from it.
 *
 *	 line: the stripped (no new line char) command line
 *
 *   returns: pointer to the command struct
 *
 *	 notes: commands must later be destroyed with destroyCommand function
 */
command_t* createCommand(char* line) {
	command_t* currCommand = malloc(sizeof(*currCommand));
	initCommand(currCommand); // initialize struct

	// copy the line (we will use it for a second parse)
	char* lineCpy = malloc(strlen(line) * sizeof(char) + 1);
	strcpy(lineCpy, line);

	char* savePtr = NULL;

	bool inputFound = false;
	bool outputFound = false;

	// first token is the command
	char* token = strtok_r(line, " ", &savePtr);

	// get pid
	char* pid = malloc((MAX_PID_STR_SIZE + 1) * sizeof(*pid));
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
	currCommand->args = malloc(sizeof(*currCommand->args) * numArgs); // allocate space for numArgs char ptrs
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
				inputFound = true;
			}
		}
		// output redirection filename
		else if (strcmp(token, OUTPUT_CHAR) == 0 && !outputFound) {
			// next token will be output filename
			token = strtok_r(NULL, " ", &savePtr);
			if (token) {
				currCommand->outputFile = expandCommand(token, VAR_EXP_CHAR, pid);
				outputFound = true;
			}
		}
		else if (strcmp(token, BACKGROUND_CHAR) == 0) {
			token = strtok_r(NULL, " ", &savePtr);
			if (token == NULL) {
				// valid background char
				currCommand->isBackground = true;
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
	fflush(stdout);
	nread = getline(bufPtr, size, stdin);
	// input validation (in case user just presses enter)
	// also check if empty space or if user starts with COMMENT_CHAR (#)
	while (nread == 1 || strncmp(*bufPtr, COMMENT_CHAR, 1) == 0 || isEmptyString(*bufPtr)) {
		printf(PROMPT_CHAR);
		fflush(stdout);
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
void printStatus(int status) {
	// if exited
	if (WIFEXITED(status)) {
		printf("exit value %d\n", WEXITSTATUS(status));
		fflush(stdout);
	}
	// if signal termination
	else if (WIFSIGNALED(status)) {
		printf("terminated by signal %d\n", WTERMSIG(status));
		fflush(stdout);
	}
}

/*
 * Function: printCommand
 * ----------------------------
 *   Prints out a given command struct for testing purposes.
 *
 *   command: a pointer to the command struct to print out
 */
void printCommand(command_t* command) {

	if (command->isBackground) {
		printf("Background: True\n");
		fflush(stdout);
	}
	else {
		printf("Background: False\n");
		fflush(stdout);
	}

	if (command->command) {
		printf("Command Name: %s\n", command->command);
		fflush(stdout);
	}
	else {
		printf("Not a command.\n");
		fflush(stdout);
	}

	printf("numArgs: %d\n", command->numArgs);
	fflush(stdout);

	for (int i = 0; i < command->numArgs; i++) {
		printf("Arg %d: %s\n", i, command->args[i]);
		fflush(stdout);
	}
	
	if (command->inputFile) {
		printf("Input File: %s\n", command->inputFile);
		fflush(stdout);
	}
	else {
		printf("No input file.\n");
		fflush(stdout);
	}

	if (command->outputFile) {
		printf("Output File: %s\n", command->outputFile);
		fflush(stdout);
	}
	else {
		printf("No output file.\n");
		fflush(stdout);
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
	command->isBackground = false;
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
 */
int startShell(void) {
	bool exitBool = false;
	bool statusInitialized = false;

	// track status of last completed/terminated child process
	int status;

	// signal handling
	struct sigaction SIGINT_action = { { 0 } }, SIGTSTP_action = { { 0 } }, SIGCHLD_action = { { 0 } };

	// ignore SIGINT by default
	SIGINT_action.sa_handler = SIG_IGN;
	// Block all catchable signals while handle_SIGINT is running
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = SA_RESTART;
	sigaction(SIGINT, &SIGINT_action, NULL);

	// default SIGTSTP handler
	SIGTSTP_action.sa_handler = handle_SIGTSTP;
	// Block all catchable signals while handle_SIGTSTP is running
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	// default SIGCHLD handler
	SIGCHLD_action.sa_sigaction = handle_SIGCHLD;
	sigemptyset(&SIGCHLD_action.sa_mask);
	SIGCHLD_action.sa_flags = SA_RESTART | SA_SIGINFO | SA_NOCLDSTOP;
	sigaction(SIGCHLD, &SIGCHLD_action, NULL);

	while (!exitBool) {
		char* buffPtr = NULL;
		size_t size = 0;
		char* currLine = NULL;

		currLine = getCommand(&buffPtr, &size);
		command_t* currCommand = createCommand(currLine);
		if (strcmp(currCommand->command, EXIT_CMD) == 0) {
			exitBool = true;
			// kill any remaining child processes
			llNode* tmp = head;
			while (tmp != NULL) {
				kill(tmp->pid, SIGKILL);
				tmp = tmp->next;
			}
		}
		else if (strcmp(currCommand->command, CD_CMD) == 0) {
			// changeDirectory built in
			changeDirectory(currCommand);
		}
		else if (strcmp(currCommand->command, STATUS_CMD) == 0) {
			// status variable has not been initialized
			if (statusInitialized == false) {
				printf("exit status 0\n");
				fflush(stdout);
			}
			else {
				printStatus(status);
				fflush(stdout);
			}
		}
		else {
			// spawn child process and divert command to exec()
			// build newargv[] array, add a spot for the command and the terminating NULL pointer
			int size = currCommand->numArgs + 2;
			char* newargv[size];

			// set null ptr terminator
			newargv[size - 1] = NULL;
			newargv[0] = currCommand->command;

			// set args
			for (int i = 0; i < currCommand->numArgs; i++) {
				newargv[i + 1] = currCommand->args[i];
			}

			// Fork a new process
			pid_t spawnPid = fork();

			switch (spawnPid) {
			case -1:
				perror("Error");
				exit(1);
				break;
			case 0:
				// child process
				// set SIGINT behavior if child process is foreground
				if (!currCommand->isBackground || backgroundEnabled == false) {
					SIGINT_action.sa_handler = SIG_DFL;
					sigaction(SIGINT, &SIGINT_action, NULL);
				}
				// set SIGTSTP to be ignored for any child process
				SIGTSTP_action.sa_handler = SIG_IGN;
				sigaction(SIGTSTP, &SIGTSTP_action, NULL);

				// handle input/output redirection
				if (currCommand->inputFile != NULL) {
					// Open input file
					int inputFD = open(currCommand->inputFile, O_RDONLY);
					if (inputFD == -1) {
						perror(currCommand->inputFile);
						exit(1);
					}

					// Redirect stdin to source file
					int result = dup2(inputFD, 0);
					if (result == -1) {
						perror("Error");
						exit(1);
					}

					// close open file
					if (close(inputFD) == -1) {
						perror("Error");
						exit(1);
					}
				}
				// no input redirection specified, send to dev/null
				else if (currCommand->isBackground && backgroundEnabled) {
					int inputFD = open("/dev/null", O_RDONLY);
					if (inputFD == -1) {
						perror("/dev/null");
						exit(1);
					}

					// Redirect stdin to source file
					int result = dup2(inputFD, 0);
					if (result == -1) {
						perror("Error");
						exit(1);
					}

					// close open file
					if (close(inputFD) == -1) {
						perror("Error");
						exit(1);
					}
				}
				if (currCommand->outputFile != NULL) {
					// Open output file
					int outputFD = open(currCommand->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
					if (outputFD == -1) {
						perror(currCommand->outputFile);
						exit(1);
					}

					// Redirect stdin to source file
					int result = dup2(outputFD, 1);
					if (result == -1) {
						perror("Error");
						exit(1);
					}

					// close open file
					if (close(outputFD) == -1) {
						perror("Error");
						exit(1);
					}
				}
				// no output redirection specified, send to dev/null
				else if (currCommand->isBackground && backgroundEnabled) {
					int outputFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
					if (outputFD == -1) {
						perror("/dev/null");
						exit(1);
					}

					// Redirect stdout to source file
					int result = dup2(outputFD, 1);
					if (result == -1) {
						perror("Error");
						exit(1);
					}

					// close open file
					if (close(outputFD) == -1) {
						perror("Error");
						exit(1);
					}
				}
				// Replace the current program with command->command
				execvp(newargv[0], newargv);
				// exec only returns if there is an error
				perror(currCommand->command);
				exit(1);
				break;
			default:
				// parent process
				// background
				if (currCommand->isBackground && backgroundEnabled) {
					printf("background pid is %d\n", spawnPid);
					fflush(stdout);
					// add child's pid to linked list
					head = addToChildList(head, spawnPid);
				} 
				// foreground, wait to complete
				else {
					spawnPid = waitpid(spawnPid, &status, 0);
					// check for signal termination
					if (WIFSIGNALED(status)) {
						printf("terminated by signal %d\n", WTERMSIG(status));
						fflush(stdout);
					}
				}
				// set status to initialized
				statusInitialized = 1;
				break;
			}
		}
		free(buffPtr);
		destroyCommand(currCommand);
	}
	destroyChildList(head);
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


