#pragma once
#include <sys/types.h>


typedef struct command_t command_t;
int changeDirectory(command_t* command);
command_t* createCommand(char* line);
void destroyCommand(command_t* command);
char* expandCommand(char* commandStr, char* expStrFrom, char* expStrTo);
char* getCommand(char** bufPtr, size_t* size);
void initCommand(command_t* command);
int isEmptyString(char* s);
int main(int argc, char* argv[]);
void printCommand(command_t* command);
int startShell(void);