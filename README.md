# cs344-smallsh

smallsh is a simple shell written in C for CS344 (Operating Systems) and implements a subset of features of well-known shells, such as bash.

Features:

- Provides a prompt for running commands
- Handles blank lines and comments, which are lines beginning with the # character
- Provides expansion for the variable $$
- Execute 3 commands exit, cd, and status via code built into the shell
- Executes other commands by creating new processes using execvp
- Supports input and output redirection
- Supports running commands in foreground and background processes
- Implements custom handlers for 2 signals, SIGINT and SIGTSTP
