#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <dirent.h> //for path resolution.
const char *sysname = "trash";

enum return_codes {
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};

struct command_t {
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3]; // in/out redirection
	struct command_t *next; // for piping
};


#define MAX_STRINGS 50 
#define MAX_STRING_LENGTH 200 

int numberOfPaths;
char** PathArr;



//------------------------------------------------------------------------------------------------------------------------------

bool stringInArray(const char* strings[], int size, const char* target);
char* findPath(char* commandName);
char** splitString(char* str, char delimiter, int* numStrings);
bool isInPath(const char *directory, const char *targetString);
void print_command(struct command_t *command);
int free_command(struct command_t *command);
int show_prompt();
int parse_command(char *buf, struct command_t *command);
void prompt_backspace();
int prompt(struct command_t *command);
int process_command(struct command_t *command);

//------------------------------------------------------------------------------------------------------------------------------


char* findPath(char* commandName) {
    for (int i = 0; i < numberOfPaths; ++i) {
        if (isInPath(PathArr[i], commandName)) {
            char* fullPath = malloc(512 * sizeof(char));  
            if (fullPath == NULL) return NULL;
            snprintf(fullPath, 512, "%s/%s", PathArr[i], commandName);
			//printf("Full path: %s\n", fullPath);
            return fullPath;
        }
    }
    return NULL;
}

	
/**
 * Takes a string, a seperator and a pointer to an integer. 
 * Seperates the said string according to the seperator and saves the number of strings to the integer.
 * @param char* str, char seperator, int* numStrings
 * @return char**
 *
 *
 */

char** splitString(char* str, char seperator, int* numStrings) {
    char** result = (char**)malloc(MAX_STRINGS * sizeof(char*)); 

    int i = 0;
    char* token = strtok(str, &seperator); 
    while (token != NULL) {
        result[i] = (char*)malloc(MAX_STRING_LENGTH * sizeof(char));
        strcpy(result[i], token); 
        token = strtok(NULL, &seperator); 
        i++;
    }
    *numStrings = i; 
    return result;
}

/**
 * Takes a directory name and a target string.
 * Returns true if there is an executable with targetName in the directory.
 * @param const char *directory, const char *targetString
 * @return bool
 */
bool isInPath(const char *directory, const char *targetString){
	DIR *dir = opendir(directory);
    if (dir == NULL) {
        //perror("Unable to open directory");
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, targetString) == 0) {
                closedir(dir);
                return true;
        }
    }

    closedir(dir);
    return false;
}



/**
 *	Takes an array ,size of the array and a string. //int size == sizeof(array) / sizeof(array[0]) 
 * 	Searches the array for the said string and returns true if found.
 *	@param const char* strings[], const char target
 *	@return bool 
 */
bool stringInArray(const char* strings[], int size, const char* target) {

    for (int i = 0; i < size; i++) {
        if (strcmp(strings[i], target) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n",
		   command->auto_complete ? "yes" : "no");
	printf("\tRedirects:\n");

	for (i = 0; i < 3; i++) {
		printf("\t\t%d: %s\n", i,
			   command->redirects[i] ? command->redirects[i] : "N/A");
	}

	printf("\tArguments (%d):\n", command->arg_count);

	for (i = 0; i < command->arg_count; ++i) {
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	}

	if (command->next) {
		printf("\tPiped to:\n");
		print_command(command->next);
	}
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
	if (command->arg_count) {
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}

	for (int i = 0; i < 3; ++i) {
		if (command->redirects[i])
			free(command->redirects[i]);
	}

	if (command->next) {
		free_command(command->next);
		command->next = NULL;
	}

	free(command->name);
	free(command);
	return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
	//printf("Parsing Command\n");
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);

	// trim left whitespace
	while (len > 0 && strchr(splitters, buf[0]) != NULL) {
		buf++;
		len--;
	}

	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL) {
		// trim right whitespace
		buf[--len] = 0;
	}

	// auto-complete
	if (len > 0 && buf[len - 1] == '?') {
		command->auto_complete = true;
	}

	// background
	if (len > 0 && buf[len - 1] == '&') {
		command->background = true;
	}

	char *pch = strtok(buf, splitters);
	if (pch == NULL) {
		command->name = (char *)malloc(1);
		command->name[0] = 0;
	} else {
		command->name = (char *)malloc(strlen(pch) + 1);
		strcpy(command->name, pch);
	}

	command->args = (char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index = 0;
	char temp_buf[1024], *arg;

	while (1) {
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// trim left whitespace
		while (len > 0 && strchr(splitters, arg[0]) != NULL) {
			arg++;
			len--;
		}

		// trim right whitespace
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL) {
			arg[--len] = 0;
		}

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// piping to another command
		if (strcmp(arg, "|") == 0) {
			struct command_t *c = malloc(sizeof(struct command_t));
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0) {
			// handled before
			continue;
		}

		// handle input redirection
		redirect_index = -1;
		if (arg[0] == '<') {
			redirect_index = 0;
		}

		if (arg[0] == '>') {
			if (len > 1 && arg[1] == '>') {
				redirect_index = 2;
				arg++;
				len--;
			} else {
				redirect_index = 1;
			}
		}

		if (redirect_index != -1) {
			command->redirects[redirect_index] = malloc(len);
			strcpy(command->redirects[redirect_index], arg + 1);
			continue;
		}

		// normal arguments
		if (len > 2 &&
			((arg[0] == '"' && arg[len - 1] == '"') ||
			 (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}

		command->args =
			(char **)realloc(command->args, sizeof(char *) * (arg_index + 1));

		command->args[arg_index] = (char *)malloc(len + 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;

	// increase args size by 2
	command->args = (char **)realloc(
		command->args, sizeof(char *) * (command->arg_count += 2));

	// shift everything forward by 1
	for (int i = command->arg_count - 2; i > 0; --i) {
		command->args[i] = command->args[i - 1];
	}

	// set args[0] as a copy of name
	command->args[0] = strdup(command->name);

	// set args[arg_count-1] (last) to NULL
	command->args[command->arg_count - 1] = NULL;

	return 0;
}

void prompt_backspace() {
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
	size_t index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &=
		~(ICANON |
		  ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	show_prompt();
	buf[0] = 0;

	while (1) {
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		// handle tab
		if (c == 9) {
			buf[index++] = '?'; // autocomplete
			break;
		}

		// handle backspace
		if (c == 127) {
			if (index > 0) {
				prompt_backspace();
				index--;
			}
			continue;
		}

		if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
			continue;
		}

		// up arrow
		if (c == 65) {
			while (index > 0) {
				prompt_backspace();
				index--;
			}

			char tmpbuf[4096];
			printf("%s", oldbuf);
			strcpy(tmpbuf, buf);
			strcpy(buf, oldbuf);
			strcpy(oldbuf, tmpbuf);
			index += strlen(buf);
			continue;
		}

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n') // enter key
			break;
		if (c == 4) // Ctrl+D
			return EXIT;
	}

	// trim newline from the end
	if (index > 0 && buf[index - 1] == '\n') {
		index--;
	}

	// null terminate string
	buf[index++] = '\0';

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}

int main() {		
	char PathEnv[strlen(getenv("PATH"))];
	strncpy(PathEnv,getenv("PATH"),strlen(getenv("PATH")));
	PathArr = splitString(PathEnv, ':', &numberOfPaths);
	if (PathArr == NULL)
	{
		perror("Path is empty");
	}

	while (1) {
		struct command_t *command = malloc(sizeof(struct command_t));

		// set all bytes to 0
		memset(command, 0, sizeof(struct command_t));

		int code;
		code = prompt(command);
		if (code == EXIT) {
			break;
		}

		code = process_command(command);
		if (code == EXIT) {
			break;
		}

		free_command(command);
	}

	printf("\n");
	return 0;
}

int process_command(struct command_t *command) {

	if (strcmp(command->name, "") == 0) {
		return SUCCESS;
	}

	if (strcmp(command->name, "exit") == 0) {
		return EXIT;
	}

	// if(command->next != NULL){ //If there is a next command.
    //     printf("%s\n", command->next->name);
    // }

	if (strcmp(command->name, "cd") == 0) {
		//printf("CD invoked\n");
		if (command->arg_count > 0) {
			int r = chdir(command->args[1]);
			if (r == -1) {
				printf("%s: %s: The directory '%s' does not exist\n", sysname, command->name, command->args[1]);
			}
			if (command->next != NULL) {
				command = command->next;
				process_command(command);
			}
			return SUCCESS;
		}
	}

	if (command->next != NULL) {
		//printf("PIPING!\n");
		pid_t pid = fork();
		if (pid == 0) { // Child
			execv(findPath(command->name), command->args);
			printf("%s: Unknown command: %s\n", sysname, command->name);
			exit(0);
		} else { // Parent
			wait(0);
			process_command(command->next);
		}
	}
	else{
		pid_t pid = fork();
		// child
		if (pid == 0) {
			/// This shows how to do exec with environ (but is not available on MacOs)
			// extern char** environ; // environment variables
			// execvpe(command->name, command->args, environ); // exec+args+path+environ

			/// This shows how to do exec with auto-path resolve
			// add a NULL argument to the end of args, and the name to the beginning
			// as required by exec

			// TODO: do your own exec with path resolving using execv()
			// do so by replacing the execvp call below
			execv(findPath(command->name), command->args); // exec+args+path
			printf("%s: Unknown command: %s\n", sysname, command->name);
			exit(0);
		} else {
			// TODO: implement background processes here
			wait(0); // wait for child process to finish
			return SUCCESS;
		}

		printf("Hello\n");

		return UNKNOWN;
	}
	return UNKNOWN;
}
