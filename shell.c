#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define OUT 0
#define OUT_AND_APPEND 1
#define CHUNK_SIZE 8

char* get_command(void);
char* get_word(char* str, int* i);
void free_command_and_files(void);
void next_command(int s);
char special(char c);

char* command = NULL;
char* input_file = NULL;
char* output_file = NULL;

int main(void) {
	signal(SIGINT, SIG_IGN);
	while (1) {
		command = NULL;
		input_file = NULL;
		output_file = NULL;	
		command = get_command();
		if (!strcmp(command, "exit")) {
			free(command);
			break;
		}
		if (!command[0]) {
			free(command);
			continue;
		}
		int len = strlen(command) + 1;
		char background = 0;
		char out_mode = 0;
		char next = 0;
		char check = '|';
		for (int i = 0; i < len; ++i) {
			if (command[i] == ' ') {
				continue;
			}
			if (command[i] == '&') {
				background = 1;
			}
			else if (command[i] == '<') {
				++i;
				input_file = get_word(command, &i);
				if (!input_file[0]) {
					fprintf(stderr, "Expected input file's name after '<'\n");
					next = 1;
					break;
				}
			}
			else if (command[i] == '>') {
				if (command[++i] == '>') {
					++i;
					out_mode = OUT_AND_APPEND;
				}
				else {
					out_mode = OUT;
				}
				output_file = get_word(command, &i);
				if (!output_file[0]) {
					fprintf(stderr, "Expected output file's name after '>' or '>>'\n");
					next = 1;
					break;
				}
			}
			else if (!background && !input_file && !output_file) {
				if ((command[len - 2] == '|') || (command[i] == '|') && (check == '|')) {
					fprintf(stderr, "Got empty command\n");
					next = 1;
					break;
				}
				check = command[i];
			}
		}
		if (next) {
			free_command_and_files();
			continue;
		}
		char new_prog = 1;
		int k = 0;
		char** argv = NULL;
		int words_count = 0;
		int prog_num = 0;
		int fd_left[2], fd_right[2];
		if ((pipe(fd_left) == -1) || (pipe(fd_right) == -1)) {
			perror("pipe");
			exit(1);
		}
		for (int i = 0; i < len; ++i) {
			if (!command[i] || special(command[i])) {
				++prog_num;
				int pid;
				if ((pid = fork()) == -1) {
					perror("fork");
					exit(1);
				}
				if (!pid) {//son
					if (background) {
						if ((pid = fork()) == -1) {
							perror("fork");
							exit(1);
						}
						if (pid) {//son
							free_command_and_files();
							for (int j = 0; j <= words_count; j++) {
								free(argv[j]);
							}
							free(argv);
							exit(0);
						}
					}
					if (prog_num == 1) {//son(grandson)
						int fd;
						if (background) {
							if ((fd = open("/dev/null", O_RDONLY)) == -1) {
								fprintf(stderr, "Can't open file /dev/null\n");
								exit(4);
							}
							dup2(fd, 0);
						}
						else if (input_file) {
							if ((fd = open(input_file, O_RDONLY)) == -1) {
								fprintf(stderr, "Can't open file %s\n", input_file);
								exit(4);
							}
							dup2(fd, 0);
						}
					} 
					else {
						dup2(fd_left[0], 0);
						close(fd_left[1]);
					}
					if (command[i] != '|') {//last program
						if (output_file) {
							int fd;
							if (out_mode) {//append
								fd = open(output_file, O_WRONLY | O_CREAT | O_APPEND, 0666);
							}
							else {
								fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
							}
							if (fd == -1) {
								fprintf(stderr, "Can't open file %s\n", output_file);
								exit(4);
							}
							dup2(fd, 1);
						}
					}
					else {
						dup2(fd_right[1], 1);
					}
					free_command_and_files();
					execvp(argv[0], argv);
					printf("Can't execute program %s\n", argv[0]);
					exit(1);
				}//father
				for (int j = 0; j <= words_count; j++) {
					free(argv[j]);
				}
				free(argv);
				if (command[i] != '|') {
					break;
				}
				close(fd_left[0]);
				close(fd_left[1]);
				fd_left[0] = fd_right[0];
				fd_left[1] = fd_right[1];
				if (pipe(fd_right) == -1) {
					perror("pipe");
					exit(1);
				}			
				new_prog = 1;
				words_count = 0;
				k = 0;
				continue;
			}
			if (new_prog) {
				for (int z = i; z < (len - 1); ++z) {//найти кол-во слов в "простой команде"
					if (special(command[z])) {
						break;
					}
					if (command[z] != ' ') {
						if (command[z + 1] == ' ') {
							++words_count;
						}
						else if (!command[z + 1] || special(command[z + 1])) {
							++words_count;
							break;
						}
					}
				}
				argv = (char**) malloc((words_count + 1) * sizeof(char*));
				argv[words_count] = NULL;
				new_prog = 0;
			}
			if (k < words_count) {
				argv[k++] = get_word(command, &i);
			} 
		}
		for (int j = 0; j < 2; ++j) {
			close(fd_left[j]);
			close(fd_right[j]);
		}
		while (wait(NULL) != -1);
		free_command_and_files();
	}
    return 0;
}

char special(char c) {
	return (c == '|') || (c == '<') || (c == '>') || (c == '&');
}

void free_command_and_files(void) {
	free(command);
	free(input_file);
	free(output_file);
}

char* get_word(char* str, int* i) {
	char* word = NULL;
	int from = -1;
	while(str[*i] && !special(str[*i])) {
		if ((from == -1) && (str[*i] != ' ')) {
			from = *i;
		}
		if ((from != -1) && (str[*i] == ' ')) {
			break;
		}
		++(*i);
	}
	word = (char*) malloc(*i - from + 1);
	int j = 0;
	for (int k = from; k < *i; ++k) {
		word[j++] = str[k];
	}
	word[j] = '\0';
	--(*i);
	return word;
}

char* get_command(void) {
	char c;
	char* a = (char*)malloc(CHUNK_SIZE);
	long long len = CHUNK_SIZE;
	long long i = 0;
    while ((read(0, &c, 1) > 0) && (c != '\n')) {
		a[i++] = c;
		if (i == len) {
			len += CHUNK_SIZE;
			char* tmp = (char*)malloc(len);
			memcpy(tmp, a, i);
			free(a);
			a = tmp;
		}
	}
	a[i] = '\0';
	return a;
}