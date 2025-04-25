#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>  // for open(), O_WRONLY, O_CREAT
#include <ctype.h> //help check for whitespace
#include <stdbool.h> //for bools
#include <sys/wait.h> //for waitpid

#define CMDLINE_MAX 512

#define MISSING_COMMAND "Error: missing command\n"
#define NO_INPUT "Error: no input file\n"
#define NO_OUTPUT "Error: no output file\n"
#define MISLOCATED_INPUT "Error: mislocated input redirection\n"
#define MISLOCATED_OUTPUT "Error: mislocated output redirection\n"
#define OUTPUT_UNOPENED "Error: cannot open output file\n"
#define INPUT_UNOPENED "Error: cannot open input file\n"

struct Command {
    char sub_command[CMDLINE_MAX]; //will hold the full command line for each command (used for tokenization)
    char *arguments[16]; //the command name is arguments[0]
    char output[32]; //if output redirection is needed (max token size is 32)
    char input[32]; //if input redirection is needed
    int read_fd;
    int write_fd;
    pid_t pid; //pid provided by the child when the command is executed
    bool need_background_command; //will become true if there is an ambersand attached && it's the last command
};

struct Background {
    pid_t pids[4]; //up to 4 pids if the background process is a pipeline
    char command_string[CMDLINE_MAX]; //this holds the string of what the background process is
    int background_commands; //to keep track of how many commands are part of the background process
    bool currently_executing; //true if yes false if no
};

void check_background_processes(struct Background* background_process){
    int status; 
    pid_t pid_array[4]; //up to 4 commands in a pipeline
    int exit_codes[4];
    int completed_processes = 0;

    /*Check to see if the background process is completed*/
    for(int i = 0; i < background_process->background_commands; i++){
        pid_array[i] = waitpid(background_process->pids[i], &status, WNOHANG);
        if(pid_array[i] > 0){ //process actually finished
            completed_processes++;
            exit_codes[i] = WEXITSTATUS(status);
        }else { //not completed yet
            pid_array[i] = -1;
            return;
        }
    }

    if(completed_processes == background_process->background_commands){ //if all commands are ready
        fprintf(stderr, "+ completed '%s' ", background_process->command_string);
        for(int i = 0; i < background_process->background_commands; i++){
            fprintf(stderr, "[%d]", exit_codes[i]);
            background_process->pids[i] = -1; // reset PID
        }
        fprintf(stderr, "\n");
        background_process->command_string[0] = '\0'; // clear the command string
        background_process->currently_executing = false; //reset the variable to false
        background_process->background_commands = 0;
    }
    return;
}

int count_arguments(char *cmd){
    /* Count the number of arguments (to check if there's more than 16) */
    int arguments = 0;
    bool isWord = false;
    for(int i = 0; cmd[i] != '\0'; i++){
        if(!isspace(cmd[i])){
            if(isWord == false){
            arguments++;
            isWord = true;
            }
        }
        else{                                           
            isWord = false;
        }
    }
    if(arguments > 16){
        fprintf(stderr, "Error: too many process arguments\n");
        return 1;
    }
    return 0;
}

char* remove_whitespace(char *cmd_string){
    char* whitespace_string = cmd_string;
    char *no_whitespace_string = malloc(strlen(cmd_string) + 1);
    char *output_ptr = no_whitespace_string;

    while (*whitespace_string!= '\0') { //while the input string has characters
        if (!isspace((unsigned char)*whitespace_string)) { //check if the character is not a whitespace
            *output_ptr = *whitespace_string; //copy the non-whitespace character to the output string
            output_ptr++;
        }
        whitespace_string++; //move on to the next character
    }

    *output_ptr = '\0'; //null-terminate the output string
    return no_whitespace_string;
    
}

/*Finds errors left to right, and also looks for input and output redirection*/
bool detect_errors(char *cmd, struct Command *commands){

    char* non_whitespace_cmd = remove_whitespace(cmd);
    if(*non_whitespace_cmd == '\0'){
        commands[0].need_background_command = false;
        return false; //no input
    }

    char *command_pointer = non_whitespace_cmd; //pointer to our string
    bool command_start = true; //expecting the first characters besides white space to be a command
    int cmd_length = strlen(non_whitespace_cmd); //find length of the string command


    while(*command_pointer != '\0'){ //while the string still has characters

        if(command_start == true){ //expecting a letter, not symbols
            if(*command_pointer == '<' || *command_pointer == '|' || *command_pointer == '>' || *command_pointer == '&'){
                fprintf(stderr, "%s", MISSING_COMMAND);
                return true;
            }
            command_start = false;
        }

        if(*command_pointer == '>'){ //expecting output
            command_pointer++;
                
            if(*command_pointer == '<' || *command_pointer == '|' || *command_pointer == '>' || *command_pointer == '&' || *command_pointer == '\0'){
                fprintf(stderr, "%s", NO_OUTPUT);
                return true;
            }
            
                //how can i find a way to extract just the output file name now, or should i wait to do this via tokenization?
        }

        if(*command_pointer == '<'){ //expecting input
            command_pointer++;
                
            if(*command_pointer == '<' || *command_pointer == '|' || *command_pointer == '>' || *command_pointer == '&' || *command_pointer == '\0'){
               fprintf(stderr, "%s", NO_INPUT);
                return true;
            }
                //how can i find a way to extract just the output file name now, or should i wait to do this via tokenization?
        }

        if(*command_pointer == '|'){
            command_pointer++;
            if(*command_pointer == '<' || *command_pointer == '|' || *command_pointer == '>' || *command_pointer == '&' || *command_pointer == '\0'){
                fprintf(stderr, "%s", MISSING_COMMAND);
                return true;
            }
        }

        if(*command_pointer == '&'){
            if((command_pointer - non_whitespace_cmd) == (cmd_length - 1)){ //check if we are at the end of the string
                commands[0].need_background_command = true;
                free(non_whitespace_cmd);
                return false;
            }
            else{ //ambersand was not at the end of the string
                fprintf(stderr, "Error: mislocated background sign\n");
                return true;
            }
        }
        else{
            commands[0].need_background_command = false;
        }

        command_pointer++;
    }

    free(non_whitespace_cmd);
    return false;
}

int parse_input_output_files(struct Command *commands, int num_commands){

    for(int i = 0; i < num_commands; i++){
        for(int j = 0; commands[i].arguments[j] != NULL; j++){

            char *arg = commands[i].arguments[j];
            char *output_symbol = strchr(arg, '>');
            char *input_symbol = strchr(arg, '<');

            /*if we have a output redirection in the argument*/
            if(output_symbol != NULL){ 

                //if it's not the last command
                if(i != num_commands - 1){ 
                    fprintf(stderr, "%s", MISLOCATED_OUTPUT);
                    return -1;
                }

                /*Attempt to find where the > is*/
                char *filename = NULL;

                //case 1; argument is just ">" by itself
                if(strcmp(arg, ">") == 0){ 
                    strcpy(commands[i].output, commands[i].arguments[j + 1]); //set the output equal to the file name (which is after the ">" symbol)
                    free(commands[i].arguments[j]);                           //free the empty argument where ">" used to be
                    free(commands[i].arguments[j + 1]);                       //free the argument with the file name
                    commands[i].arguments[j]= NULL;              
                    commands[i].arguments[j + 1] = NULL;
                }
                
                
                //case 2: " >file.txt"
                else if(arg[0] == '>'){
                    filename = arg + 1;
                    strcpy(commands[i].output, filename);
                    free(commands[i].arguments[j]);
                    commands[i].arguments[j] = NULL;
                }
                    
                //case 3: "cmd> file.txt" || "cmd>file.txt"
                else if(output_symbol != arg){
                    filename = output_symbol + 1;

                     //case: "cmd> file.txt"
                    if(*filename == '\0'){
                        strcpy(commands[i].output, commands[i].arguments[j + 1]);
                        free(commands[i].arguments[j+1]);
                        commands[i].arguments[j+1] = NULL;
                    }

                    //case: "cmd>file.txt"
                    else{
                        strcpy(commands[i].output, filename);
                    }

                    //remove everything after >
                    *output_symbol = '\0';
                    //free(commands[i].arguments[j]);
                    //commands[i].arguments[j] = strdup(arg);
                }

                commands[i].write_fd = open(commands[i].output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if(commands[i].write_fd < 0){
                    fprintf(stderr, "%s", OUTPUT_UNOPENED);
                    return -1;
                }
                break; // go to next i
            }
            else{
                commands[i].output[0] = '\0';
            }

            /*if we have an input redirection in the argument*/
            if(input_symbol != NULL){ 

                //if it's not the first command
                if(i != 0){ 
                    fprintf(stderr, "%s", MISLOCATED_INPUT);
                    return -1;
                }

                /*Attempt to find where the < is*/
                char *filename = NULL;

                //case 1; argument is just "<" by itself
                if(strcmp(arg, "<") == 0){ 
                    strcpy(commands[i].input, commands[i].arguments[j + 1]); //set the input equal to the file name (which is after the "<" symbol)
                    free(commands[i].arguments[j]);                          //free the empty argument where "<" used to be
                    free(commands[i].arguments[j + 1]);                      //free the argument with the file name
                    commands[i].arguments[j] = NULL;
                    commands[i].arguments[j + 1] = NULL;
                }

                //case 2: " <file.txt"
                else if(arg[0] == '<'){
                    filename = arg + 1;
                    strcpy(commands[i].input, filename);
                    free(commands[i].arguments[j]); //free the argument with "<file.txt"
                    commands[i].arguments[j] = NULL;
                }

                //case 3: "cmd< file.txt" || "cmd<input.txt"
                else if(input_symbol != arg){
                    filename = input_symbol + 1;

                    //case: "cmd< file.txt"
                    if(*filename == '\0'){
                        strcpy(commands[i].input, commands[i].arguments[j + 1]); //set the input equal to the file name (which is in the next argument)
                        free(commands[i].arguments[j + 1]);                       //free the argument with the file name
                        commands[i].arguments[j + 1] = NULL;
                    }

                    //case: "cmd<input.txt"
                    else{
                        strcpy(commands[i].input, filename); //set the input equal to the file name (which follows the "<" in the same argument)
                    }

                    //remove everything after <
                    *input_symbol = '\0';
                    //free(commands[i].arguments[j]);         //free the old full argument
                    //commands[i].arguments[j] = strdup(arg); //replace with cleaned-up version (command only)
                }

                commands[i].read_fd = open(commands[i].input, O_RDONLY);
                if(commands[i].read_fd < 0){
                    fprintf(stderr, "%s", INPUT_UNOPENED);
                    return -1;
                }
                break; // go to next i
            }
            else{
                commands[i].input[0] = '\0';
            }
        }
    }
    return 0;
}

int extract_tokens(char *cmd, struct Command *commands, struct Background *background_process){
    /*GOAL: extracting all the command(s) and their respective arguments*/
    int num_commands = 0;

    if(detect_errors(cmd, commands) == true){
        return -1; //exit, in order to reprompt the shell
    }

    char *token_ambersand = strtok(cmd, "&"); //removes the ambersands

    char *token_commands = strtok(token_ambersand, "|"); //seperate the commands, if there is piping

    /*Extract up to four commands (if there is piping)*/
    while(token_commands != NULL && num_commands < 4){
        strcpy(commands[num_commands].sub_command, token_commands); //copies the sub command so we can tokenize the arguments for each section later
        if(count_arguments(commands[num_commands].sub_command) == 1){
            return -2; //error, the command has more than 16 arguments
        }
        token_commands = strtok(NULL, "|"); //move to the next command
        num_commands++;
    }
        
    /*Collect all the arguments for every command*/
    char delimiter[] = " \t\n\v\f\r"; //ignore all forms of white space
    char *token_arguments;

    for(int i = 0; i < num_commands; i++){
        token_arguments = strtok(commands[i].sub_command, delimiter); //tokenize the arguments
        int num_arguments = 0;

        while(token_arguments != NULL){ //while there is still arguments
            commands[i].arguments[num_arguments] = malloc(strlen(token_arguments) + 1);
            strcpy(commands[i].arguments[num_arguments], token_arguments);
            num_arguments++;
            token_arguments = strtok(NULL, delimiter);  //move on to the next argument
        }
        commands[i].arguments[num_arguments] = NULL;  //null-terminate
    }

    /*assigns the input and output fils to the command if needed*/
    if(parse_input_output_files(commands, num_commands) == -1){
        return -1; //if error
    };

    if(commands[0].need_background_command == true){
        background_process->background_commands = num_commands;
        background_process->currently_executing = true;
    }

    return num_commands;
}

void singular_command(struct Command command, char *cmd_copy, struct Background *background_process){
    /* Builtin exit command */
    if (!strcmp(command.arguments[0], "exit")) { //strcmp compares the user input cmd with "exit". If they match it will exit
        if(background_process->currently_executing == true){ //check if a background command is currently executing before we can exit
            fprintf(stderr, "Error: active job still running\n");
            fprintf(stderr, "+ completed '%s' [%d]\n", cmd_copy, 1);
            return;
        }   
        fprintf(stderr, "Bye...\n");
        fprintf(stderr, "+ completed '%s' [%d]\n", cmd_copy, 0);
        exit(0);
    }
    if(!strcmp(command.arguments[0], "cd")){
        if(chdir(command.arguments[1]) != 0){ //used chdir to change parent directory without forking. Command.argument[1] because 1 argument will be given only
            fprintf(stderr,"Error: cannot cd into directory\n");
            fprintf(stderr, "+ completed '%s' [%d]\n", cmd_copy, 1);
            return;
        } 
        fprintf(stderr, "+ completed '%s' [%d]\n", cmd_copy, 0);
        return;
    }
    if(!strcmp(command.arguments[0], "pwd")){
        char path[CMDLINE_MAX]; //cwd needs buffer
        getcwd(path, CMDLINE_MAX);//gives pwd, same as cwd
        fprintf(stdout, "%s\n", path);
        fprintf(stderr, "+ completed '%s' [%d]\n", cmd_copy, 0);
        return;
    }

    /* Execute other commands that aren't built in, and check for errors */
    pid_t pid = fork();
    if(pid == 0){ //this is the child

        if(strcmp(command.input, "\0")){ //if input redirection has file
            dup2(command.read_fd, STDIN_FILENO);
            close(command.read_fd);
        }
        if(strcmp(command.output, "\0")){ //if output redirection has file
            dup2(command.write_fd, STDOUT_FILENO);
            close(command.write_fd);
        }

        if(execvp(command.arguments[0], command.arguments) == -1){
            fprintf(stderr, "Error: command not found\n");
            exit(1);
        }
    }
    else if(pid > 0){ //this is the parent
        if(command.need_background_command == true){
            if(background_process->pids[0] == -1){ //if pid hasnt been set yet
                background_process->pids[0] = pid; //keep record of this pid so we could return to it and see if it's finished
                strncpy(background_process->command_string, cmd_copy, CMDLINE_MAX - 1);
                background_process->command_string[CMDLINE_MAX - 1] = '\0'; //null-terminate the end
                command.need_background_command = false; //don't need to enter this if statement anymore
                return;
            }
        }
        else{
            int status;
            waitpid(pid, &status, 0);

            if(background_process->currently_executing == true){
                check_background_processes(background_process);
            }

            fprintf(stderr, "+ completed '%s' [%d]\n", cmd_copy, WEXITSTATUS(status));
            return;
        }
    }
}

void pipeline(struct Command *commands, int num_commands, char *cmd_copy, struct Background *background_process){

    int exit_codes[num_commands]; //array to store exit codes
    int pipes[num_commands - 1][2]; //if we have N commands, we only need N - 1 pipes
                                    // e.g "echo Hello world | grep Hello" there's 2 commands, only need 1 pipe
                                    //the 2 means that every pipe will need 2 numbers (to create the read and the write sections)

    for(int i = 0; i < num_commands - 1; i++){
        pipe(pipes[i]); //create the R and W for every pipe we need
    }

    for(int i = 0; i < num_commands; i++){ //create a child for every command we need

        pid_t pid = fork();
        if(pid == 0){
            //if the first pipe has an input redirection
            if(strcmp(commands[0].input, "\0") && (i == 0)){
                dup2(commands[0].read_fd, STDIN_FILENO);
                close(commands[0].read_fd);
            }

            //if not the first pipe, redirect input to be read from the previous pipe
            if( i > 0 ){
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            
            //if the last pipe has output redirection
            if(strcmp(commands[num_commands - 1].output, "\0") && (i == num_commands - 1)){
                dup2(commands[num_commands - 1].write_fd, STDOUT_FILENO);
                close(commands[num_commands - 1].write_fd);
            }

            //if not the last pipe, redirect output to go to the written portion of your pipe
            if( i < num_commands - 1 ){
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            //close all pipes in the child
            for (int j = 0; j < num_commands - 1; j++){
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            //execute the command for this section of the pipe
            if(execvp(commands[i].arguments[0], commands[i].arguments) == -1){
                fprintf(stderr, "Error: command not found\n");
                exit(1);
            }
        }
        else{
            commands[i].pid = pid;

            if(i < num_commands - 1){ //close the write end of the pipe for the current commands
                close(pipes[i][1]);   //(since it won't need to write anymore)
            }

            if(i > 0){ //close the read end of the previous pipe (since we read from it already)
                close(pipes[i - 1][0]);
            }

            if(commands[0].need_background_command == true){
                if( background_process->pids[0] == -1){ //if pid for command 1 hasnt been set
                    background_process->pids[0] = pid; //keep record of this pid so we could return to it and see if it's finished
                    strncpy(background_process->command_string, cmd_copy, CMDLINE_MAX - 1);
                    background_process->command_string[CMDLINE_MAX - 1] = '\0'; //null-terminate the end
                }
                else if(background_process->pids[0] == commands[0].pid){ //if the pid of the first command in the background process is the same as this current function
                    background_process->pids[i] = pid;               // run first command pid, then we are in the right process to keep updating the background pids
                }
            }
        }
    }

    if(commands[0].need_background_command == true && background_process->pids[num_commands - 1] == commands[num_commands - 1].pid){
        commands[0].need_background_command = false; //don't need to enter these if statements anymore (to not overwrite info with future sshell inputs)
        return;
    }
    else{
        //wait for processes/children to finish
        for (int i = 0; i < num_commands; i++) {
            int status;
            waitpid(commands[i].pid, &status, 0);
            exit_codes[i] = WEXITSTATUS(status);
        }

        if(background_process->currently_executing == true){
            check_background_processes(background_process);
        }

        /*Completion message*/
        fprintf(stderr, "+ completed '%s' ", cmd_copy);
        for (int i = 0; i < num_commands; i++) {
            fprintf(stderr, "[%d]", exit_codes[i]); //print all the exit codes
        }
        fprintf(stderr, "\n");
        return;
    }

}


int main()
{
    char cmd[CMDLINE_MAX];
    char *eof;
    struct Command commands[4]; //up to 4 possible commands via pipelining
    struct Background background_process; //only one background job can be given at a time
    background_process.currently_executing = false; //initialize to false
    background_process.pids[0] = -1; //pid hasnt been initialized yet
    commands[0].need_background_command = false;

    while (1) {

        char *nl;

        /* Print prompt */
        printf("sshell@ucd$ ");
        fflush(stdout);

        /* Get command line */
        eof = fgets(cmd, CMDLINE_MAX, stdin); //reads user input from stdin into the cmd buffer (up to CMDLINE_MAX characters)
        if (!eof) //if fgets returns null for eof 
            /* Make EOF equate to exit */
            strncpy(cmd, "exit\n", CMDLINE_MAX); // exit is set as the ecommand

        /* Print command line if stdin is not provided by terminal */
        if (!isatty(STDIN_FILENO)) { //checks if the input is coming from a terminal, otherwise (from a file, etc.) it prints it out
            printf("%s", cmd);
            fflush(stdout);
        }

        /* Remove trailing newline from command line */
        nl = strchr(cmd, '\n');
        if (nl)
            *nl = '\0';

        char cmd_copy[CMDLINE_MAX];
        strncpy(cmd_copy, cmd, CMDLINE_MAX); //saving the original buffer line to print out for later

        /* Extract the tokens | Checks for parsing errors as well*/
        int num_commands = extract_tokens(cmd, commands, &background_process); //extract the commands, and their respective arguments; returns the amount of commands extracted
        if(num_commands < 0){ continue; } //if this is negative, it means an error occured and we should reprompt the shell instead of executing a command

        if(num_commands == 1){
            singular_command(commands[0], cmd_copy, &background_process); //only execute one command (which means we can execute built-in commands too)
        } else if (num_commands > 1){
            pipeline(commands, num_commands, cmd_copy, &background_process); //execute the pipeline (no built-in commands)
        } else if(num_commands == 0){
            if(background_process.currently_executing == true){
                check_background_processes(&background_process); //check to see if it ended
            }   
        }

        for(int i = 0; i < num_commands; i++) {
            for(int j = 0; commands[i].arguments[j] != NULL; j++) { //check before freeing
                    free(commands[i].arguments[j]);
                    commands[i].arguments[j] = NULL; //prevent double-free
            }
        }
    }

    return EXIT_SUCCESS;
}

/*Sources*/
// https://www.gnu.org/software/libc/manual/html_node/Classification-of-Characters.html --> isspace() function in the ctype library
// https://www.w3schools.com/c/c_break_continue.php --> for continue command
// https://www.geeksforgeeks.org/string-tokenization-in-c/ --> How to tokenize strings
// https://www.geeksforgeeks.org/isspace-in-c/ looked into isspace() again and also to see different types of white space
// https://www.sanfoundry.com/c-tutorials-difference-between-character-string/ I had to look stuff up to debug; I was using double quotes rather than singular quotes
// https://www.geeksforgeeks.org/c-program-to-find-the-length-of-a-string/ I forgot the function on how to find the length of a string 
// https://www.geeksforgeeks.org/strchr-in-c/ I was looking for a function that can detect a specific character in a string 
// https://stackoverflow.com/questions/1726302/remove-spaces-from-a-string-in-c looking for ways to remove whitespace
// https://www.geeksforgeeks.org/strcmp-in-c/ looking for ways to compare 2 strings to see if theyre equal
// https://stackoverflow.com/questions/15110642/how-to-fdopen-as-open-with-the-same-mode-and-flags I wanted an idea on how to write my call to open for write
// https://www.geeksforgeeks.org/input-output-system-calls-c-create-open-close-read-write/
// https://www.geeksforgeeks.org/pointer-array-array-pointer/