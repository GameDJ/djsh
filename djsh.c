/*
 * djsh.c v1.0
 * Originally created March 2024
 * This program executes a limited linux shell.
 * The shell can perform some basic built-in commands as well as execute path commands.
 * Execute with ./djsh, which defaults to using execlp, or use ./djsh -execvp, to use execvp
 * Built-in commands:
 *   exit:           exit djsh
 *   cd <arg1>:      change directory (".." to go up a directory)
 *   path:           print the current path variable
 *   path <arg1>:    overwrite the path variable with colon-separated path directories
 *   history:        print out recent inputs, up to 50
 *   history <arg1>: specify the number of recent inputs to print
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define MAX_ARGS 4 // NOTE: changing this will require changing execlp() below

// Return the command without its path, eg "/bin/ls" returns "ls"
char* getCommandFromPath(char* cmdPath);

// Print the one and only error message
void djsh_error();

// Check for the current command along Path and current directory
// Return the first path found (including the command)
char* checkPath(char* cmd, char* path);

// Node for singly-linked list containing command history
// Head is oldest, Tail is newest
struct HistEntry {
	char* cmd;  // The command to be stored
	struct HistEntry* next;  // Pointer to the next most entry
};

int main(int argc, char *argv[]) {

	// Vars for reading input
	// Initial input
	const char default_msg[] = "**By default, execlp() will be used**\n";
	const char execlp_msg[] = "**Based on your choice, execlp() will be used**\n";
	const char execvp_msg[] = "**Based on your choice, execvp() will be used**\n";
	char execType = 'l';  // 'l' for execlp, 'v' for execvp
	// djsh input
	const char prompt[] = "djsh> ";
	char* line = NULL;
	size_t len = 0;
	ssize_t nread;

	// Vars for parsing input
	char* nextToken = NULL;
	const char* whiteSpace = " \t\n\r";
	// pointer to the string of the path/command + each argument + NULL terminator
	char* args[MAX_ARGS+2] = {NULL}; 
	char* command;  // the command WITHOUT its path

	// Output redirection
	int redirect = 0;  // Changes to 1 for redirect
	char* filename = NULL;
	FILE* file = NULL;
	int output_fd = 1;  // 1 is stdout's default fd
	int temp_fd;

	// History management
	struct HistEntry* head = NULL;  // Oldest entry
	struct HistEntry* tail = NULL;  // Newest entry
	struct HistEntry* temp = NULL;
	char* tempCmd = NULL;
	int numHistory = 0;  // Number of entries in history (stops incrementing at 50)
	int numEntriesToPrint;

	// Path
	char* path = NULL;
	char* cmdPath = NULL;

	if (argc < 2) {
		write(STDOUT_FILENO, default_msg, strlen(default_msg));
	} else {
		if (strcmp(argv[1], "-execlp") == 0) {
			write(STDOUT_FILENO, execlp_msg, strlen(execlp_msg));
		} else if (strcmp(argv[1], "-execvp") == 0) {
			execType = 'v';
			write(STDOUT_FILENO, execvp_msg, strlen(execvp_msg));
		} else {
			djsh_error();
			write(STDOUT_FILENO, default_msg, strlen(default_msg));
		}
	}

	// Main loop
	while(1) {
		write(STDOUT_FILENO, prompt, strlen(prompt));
		nread = getline(&line, &len, stdin);

		// If input failed then just skip it all
		if (nread == -1)
			continue;
		
		/// HISTORY
		// Add command to history
		temp = (struct HistEntry*)malloc(sizeof(struct HistEntry));
		if (temp == NULL) {
			//perror("Memory allocation failure\n");
			djsh_error();
			exit(1);
		}
		// First replace trailing carriage return with null terminator
		if (line[nread-1] == '\n') {
			line[nread-1] = '\0';
			if (nread >= 2 && line[nread-2] == '\r')
				 line[nread-2] = '\0';
		}
		// Copy the command and store it
		if (nread == 0 
			|| nread == 1 & line[0] == '\0'
			|| strcmp(line, "\r\n") == 0) {
			// just store a space if blank input
			tempCmd = (char*)malloc(sizeof(char) * 2);
			tempCmd = " \0";
		} else {
			if (line[nread-1] == '\0')
				tempCmd = (char*)malloc(sizeof(char) * (strlen(line)));
			else
				tempCmd = (char*)malloc(sizeof(char) * (strlen(line+1)));
			strcpy(tempCmd, line);
			if (tempCmd == NULL) {
				//perror("Memory allocation failure\n");
				djsh_error();
				exit(1);
			}
		}
		temp->cmd = tempCmd;
		// No head? set it (and the tail)
		if (head == NULL) {
			head = temp;
			tail = temp;
		} else {
			// point tail to the new entry, and now remember it as the tail instead
			tail->next = temp;
			tail = temp;
		}
		// If there are already <50 entries just increment, otherwise delete oldest
		if (numHistory < 50) {
			numHistory++;
		} else {
			// More than 50 entries, so delete the oldest (head)
			// Here we're repurposing temp to remember the old head
			temp = head;
			head = head->next;
			if (temp->cmd != NULL)
				free(temp->cmd);
			free(temp);
		}

		/// ARGUMENTS	
		// Parse the arguments from the input
		for (int i=0; i < MAX_ARGS+2; i++) {
			// First free any previously held args if applicable
			if (i < MAX_ARGS+1 && args[i] != NULL)
				free(args[i]);
			// copy each argument string into dynamic memory for easy access
			if (i == 0)
				nextToken = strtok(line, whiteSpace);
			else
				nextToken = strtok(NULL, whiteSpace);
			if (nextToken != NULL) {
				// first check if we're trying to redirect output
				if (strcmp(nextToken, ">") == 0) {
					// get dest filename
					filename = strtok(NULL, whiteSpace);
					// set up output file
					output_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
					if (output_fd < 0) {
						//perror("error opening file for output redirection");
						djsh_error();
					} else {
						// first save stdout's fd
						temp_fd = dup(STDOUT_FILENO); 
						// redirect output from stdout to the file
						if (dup2(output_fd, STDOUT_FILENO) == -1) {
							//perror("dup2 failed");
							djsh_error();
						} else 
							redirect = 1;
					}
					// Make sure ">" isn't saved as an argument
					if (i < MAX_ARGS) {
						args[i] = NULL;
					} else {  // We're past the space for actual args, so break
						break;
					}
				} else if (i < MAX_ARGS) {
					// allocate memory to store the (going to be null-terminated) arg
					args[i] = (char*)malloc((strlen(nextToken)+1) * sizeof(char));
					strcpy(args[i], nextToken);
				}
			}
			else {
				// Set this arg to null
				args[i] = NULL;

				// If no first argument, not a valid input
				if (i == 0) {
					djsh_error();
					break;
				}
			}
		}
		// No first argument, skip this iteration
		if (args[0] == NULL)
			continue;

		/// COMMANDS
		// Handle built-in commands
		if (strcmp(args[0], "exit") == 0) {
			// If there are arguments then error, otherwise exit djsh
			if (strtok(NULL, whiteSpace) != NULL) {
				djsh_error();
			} else {
				exit(0);
			}
		} else if (strcmp(args[0], "cd") == 0) {
			if (args[1] == NULL || args[2] != NULL) {
				// Must take exactly one argument
				djsh_error();
			} else {
				// Change directory, error if fails
				if (chdir(args[1]) < 0) {
					djsh_error();
				}
			}
		} else if (strcmp(args[0], "path") == 0) {
			if (args[1] == NULL) {
				// No args provided, print path instead
				if (path != NULL)
					write(STDOUT_FILENO, path, strlen(path));
					write(STDOUT_FILENO, "\n", sizeof(char));
			} else {
				// Write/overwrite path
				// First delete old one (if it exists), then copy nextToken into new one
				if (path != NULL)
					free(path);
				path = (char*)malloc(sizeof(char) * (strlen(args[1])+1));
				if (path == NULL) {
					//perror("path malloc");
					djsh_error();
					continue;  // skip to next iteration
				}
				strcpy(path, args[1]);
			}
		} else if (strcmp(args[0], "history") == 0) {
			// will use temp to track current entry
			temp = head;

			// If arg n, set things up to print n-many entries
			if (args[1] != NULL) {
				// Just using this to avoid multiple atoi calls I suppose
				numEntriesToPrint = atoi(args[1]);
				if (numEntriesToPrint < 0 || numEntriesToPrint > 50) {
					djsh_error();
					continue;
				} else {
					// skip past all the unwanted entries
					for (int i=0; i < (numHistory - numEntriesToPrint) && i < numHistory; i++) {
						if (temp != NULL)
							temp = temp->next;
					}
				}
			}
			// Print entries
			while (temp != NULL) {
				if (temp->cmd != NULL)
					write(STDOUT_FILENO, temp->cmd, strlen(temp->cmd));
				write(STDOUT_FILENO, "\n", sizeof(char));
				temp = temp->next;
			}
		} else {  // Non-builtin commands
			// Make child process
			pid_t pid = fork();
			if (pid < 0) {  // error
				//fprintf(stderr, "Fork Failed");
				djsh_error();
				return 1;
			}
			else if (pid == 0) {  // child
				cmdPath = checkPath(args[0], path);
				if (cmdPath == 0) {  // no path found
					djsh_error();
				} else {
					if (execType == 'l') {
						command = getCommandFromPath(args[0]);
						if (execlp(cmdPath, command, args[1], args[2], args[3], args[4], NULL) < 0) {
							//perror("execlp error\n");
							djsh_error();
							exit(1);  // exit this child process
						} 
					} else if (execType == 'v') {
						if (execvp(cmdPath, &args[0]) < 0) {
							//perror("execvp error\n");
							djsh_error();
							exit(1);  // exit this child process
						}
					}
				}
			}
			else { // should be parent
				// wait for child process to terminate	
				wait(NULL);
			}
		}
		// If redirected, direct output back to stdout and close the file
		if (redirect == 1) {
			if (dup2(temp_fd, STDOUT_FILENO) == -1) {
				//perror("error switching back to stdout from file");
				djsh_error();
			}
			close(output_fd);
		}
	}
	return 0;
}

char* getCommandFromPath(char* cmdPath) {
	int lastSlash = -1;
	int len = strlen(cmdPath);
	// Find index of last slash in path
	for (int i=0; i < len; i++) {
		if (cmdPath[i] == '/')
			lastSlash = i;
	}
	
	if (lastSlash > -1) {
		// make var to contain substring (include spot for null terminator)
		const int newLen = len - lastSlash;
		char* cmd = (char*)malloc(newLen * sizeof(char));
		if (cmd == NULL) {
			//perror("cmd malloc");
			djsh_error();
			exit(1); // Exit the current process (probably a child)
		}
		// copy characters over to cmd from end of cmdPath
		for (int i=0; i < newLen; i++) {
			cmd[i] = cmdPath[i + lastSlash + 1];
		}
		return cmd;
	}
	// (else) no slashes anywhere so just return the original
	return cmdPath;
}

void djsh_error() {
	char error_message[] = "An error has occurred (from DJ)\n";
	write(STDERR_FILENO, error_message, strlen(error_message));
}

char* checkPath(char* cmd, char* path) {
	// Check for command along path's directories
	// Start by getting first token
	char* nextToken = strtok(path, ":");
	char* curPath;
	while (nextToken != NULL) {
		// Copy nextToken into its own char* (with extra space) for safer concatenation
		curPath = (char*)malloc(sizeof(char) * (strlen(nextToken) + 1 + 1024));
		if (curPath == NULL) {
			//perror("curPath malloc");
			djsh_error();
			exit(1); // Exit the current process (probably a child)
		}
		strcpy(curPath, nextToken);

		// add / to end if not already
		if (curPath[strlen(curPath)-1] != '/') {
			strcat(curPath, "/");
		}
		
		// Concatenate cmd onto nextToken
		strcat(curPath, cmd);
		// Check if this forms a viable cmd path
		if (access(curPath, X_OK) == 0) {
			return curPath;
		}
		nextToken = strtok(NULL, ":");
		// free dynamically allocated memory before continuing
		free(curPath);
	}
	
	// No valid path found, return error value
	return "0";
}

