#pragma once
#include <sys/types.h>


typedef struct command_t command_t;
typedef struct llNode llNode;
llNode* addToChildList(llNode* head, pid_t childPid);
int changeDirectory(command_t* command);
command_t* createCommand(char* line);
void destroyChildList(llNode* head);
void destroyCommand(command_t* command);
char* expandCommand(char* commandStr, char* expStrFrom, char* expStrTo);
char* getCommand(char** bufPtr, size_t* size);
void handle_SIGCHLD(int signo, siginfo_t* si, void* context);
void handle_SIGTSTP(int signo);
void initCommand(command_t* command);
int isEmptyString(char* s);
int main(int argc, char* argv[]);
void printChildList(llNode* head);
void printCommand(command_t* command);
void printStatus(int status);
llNode* removeFromChildList(llNode* head, pid_t childPid);
int startShell(void);