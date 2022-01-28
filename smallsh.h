#pragma once
#include <sys/types.h>


typedef struct command_t command_t;
command_t* createCommand(char* line);
char* getCommand(char** bufPtr, size_t* size);
void printCommand(command_t* command);
void initCommand(command_t* command);
void destroyCommand(command_t* command);
int startShell(void);
int main(int argc, char* argv[]);
int isEmptyString(char* s);
char* expandCommand(char* commandStr, char* expStrFrom, char* expStrTo);