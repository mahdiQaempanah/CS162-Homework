#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>


#define FALSE 0
#define TRUE 1
#define EXECUTE_FAILED -19839587
#define INPUT_STRING_SIZE 80
#define OUTPUT_STRING_SIZE 160
#define BUFFER_SIZE 100

#include "io.h"
#include "parse.h"
#include "process.h"
#include "shell.h"

int cmd_quit(tok_t arg[]);

int cmd_help(tok_t arg[]);

int cmd_pwd(tok_t arge[]);

int cmd_cd(tok_t arge[]);

int cmd_run_program(tok_t arge[]);

int cmd_wait(tok_t arge[]);

/* Command Lookup table */
typedef int cmd_fun_t (tok_t args[]); /* cmd functions take token array and return int */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_quit, "quit", "quit the command shell"},
  {cmd_pwd, "pwd", "print folder path"},
  {cmd_cd, "cd", "go to new folder"},
  {cmd_wait, "wait", "wait until all background process finished"}
};

int cmd_quit(tok_t arg[]) {
  printf("Bye\n");
  exit(0);
  return 1;
}

int cmd_help(tok_t arg[]) {
  int i;
  for (i=0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); i++) {
    printf("%s - %s\n",cmd_table[i].cmd, cmd_table[i].doc);
  }
  return 1;
}

int cmd_pwd(tok_t arge[]) {
  char cwd[OUTPUT_STRING_SIZE];
  if(getcwd(cwd, sizeof(cwd)) != NULL) {
      printf("%s\n", cwd);
      return 0;
  }else{
      perror("internal error\n");
      return -1;
  }
}

int cmd_cd(tok_t arge[]) {
   if(chdir(arge[0]) == -1) {
      printf("invalid path\n");
      return -1;
   }
   return 1;
}

int cmd_wait(tok_t arge[]) {
   while (wait(NULL) != -1)
   {
    continue;
   }
}

// void reset_io(process* p){
//   dup2(p->preStdin, STDIN_FILENO);
//   dup2(p->preStdout, STDOUT_FILENO);
// }

void sig_handler(int signo){
  if (signo == SIGINT){
    printf("\nInterrupted with Ctrl+C\n");
  }
  else if (signo == SIGQUIT){
    printf("\nQuited with Ctrl+\\\n");
  } else if (signo == SIGTSTP) {
    printf("\nStopped with pid: %d\n", getpid());
  }
}

void reset_signals(){
  signal(SIGINT,SIG_IGN);
  signal(SIGQUIT,SIG_IGN);
  signal(SIGTSTP,SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
}

int lookup(char cmd[]) {
  int i;
  for (i=0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); i++) {
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0)) return i;
  }
  return -1;
}

int execute_program(process* p){
  pid_t pid;
  int status;
  pid = fork();
  if(pid == -1){
    return -1; 
  }
  else if(pid == 0){
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    p->pid = getpid();
    if(p->input_file!=NULL){
      int fd = open(p->input_file, O_RDONLY);
      p->stdin = fd;
      dup2(p->stdin, STDIN_FILENO);
      close(fd);
    }
    if (p->output_file!=NULL) {
      printf("%s", p->output_file);
      int fd = open(p->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      p->stdout = fd;
      dup2(p->stdout, STDOUT_FILENO);
      close(fd);
    }
    execv(p->argv[0], p->argv);
    p->completed = TRUE;
    exit(EXECUTE_FAILED); 
  } 
  else{ //parent
    p->pid = getpid();
    p->tmodes = first_process->tmodes;
    setpgid(pid, pid); 

    if (!p->background) {
      tcsetpgrp(STDIN_FILENO, pid);
      waitpid(pid, &status, 2);
      tcsetpgrp(STDIN_FILENO, shell_pgid);
      return WEXITSTATUS(status); 
    }
    else{
      return 0;
    }
  }
}

void init_shell()
{
  /* Check if we are running interactively */
  shell_terminal = STDIN_FILENO;

  /** Note that we cannot take control of the terminal if the shell
      is not interactive */
  shell_is_interactive = isatty(shell_terminal);

  if(shell_is_interactive){

    /* force into foreground */
    while(tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp()))
      kill( - shell_pgid, SIGTTIN);
      
    shell_pgid = getpid();
    /* Put shell in its own process group */
    if(setpgid(shell_pgid, shell_pgid) < 0){
      perror("Couldn't put the shell in its own process group");
      exit(1);
    }

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);
    tcgetattr(shell_terminal, &shell_tmodes);
  }
  
  reset_signals();
  first_process = malloc(sizeof(process));
  first_process->pid = getpid();
  first_process->argc = 0;
  first_process->completed = FALSE;
  first_process->stopped = FALSE;
  first_process->background = FALSE;
  first_process->tmodes = shell_tmodes;
  first_process->stdin = dup(0);
  first_process->stdout = dup(1);
  first_process->stderr = dup(2);
}

int is_executable(char* file_path){
  struct stat sb;
  return (stat(file_path, &sb) == 0 && (sb.st_mode & S_IXUSR));
}

int launch_process(process *p)
{
  char program[BUFFER_SIZE];
  char path[BUFFER_SIZE];
  strcpy(program, p->argv[0]);
  int is_execute = FALSE;
  if(is_executable(program)){
    is_execute = TRUE;
  }
  
  tok_t* candidate_paths;
  const char* PATH = getenv("PATH");
  strcpy(path, PATH);
  candidate_paths = getToks(path);
  for(int i = 0; candidate_paths[i] != NULL && !is_execute; i++){
      sprintf(program, "%s/%s", candidate_paths[i], p->argv[0]);
      if(is_executable(program)){
        is_execute = TRUE;
        break;
      }
  }
  if(is_execute){
    p->argv[0] = program;
    execute_program(p);
  }
  // reset_io(p);
  return (1-is_execute)*EXECUTE_FAILED;
}

/**
 * Add a process to our process list
 */
void add_process(process* p)
{
  process* po = first_process;
  for(;po->next!=NULL; po = po->next)
    continue;
  po->next = p;
  p->prev = po;
}

/**
 * Creates a process given the inputString from stdin
 */
process* create_process(tok_t* t)
{ 
  if(t[0] == NULL)
    return NULL;
   
  
  process* p = malloc(sizeof(process));
  p->argv = t;
  p->background = FALSE;
  p->completed = FALSE;
  p->stopped = FALSE;
  p->stdin = dup(0);
  p->stdout = dup(1);
  p->stderr = dup(2);

  char *output_file; 
  int output_op_index = -1;
  int input_op_index = -1;


  for(int i = 0; t[i] != NULL; i++){
    p->argc = i+1;
    if(!strcmp(t[i], "<")){
      input_op_index = i;
      t[i] = NULL;
      p->input_file = (char *) malloc(BUFFER_SIZE * sizeof(char));
      strcpy(p->input_file, t[i+1]);
    }
    else if(!strcmp(t[i], ">")){
      output_op_index = i;
      t[i] = NULL;
      p->output_file = (char *) malloc(BUFFER_SIZE * sizeof(char));
      strcpy(p->output_file, t[i+1]);
    }
  }

  if (!strcmp(p->argv[p->argc-1], "&")){
    p->background = TRUE;
    p->argv[p->argc - 1] = NULL;
  }
  return p;
}

int shell (int argc, char *argv[]) {
  char *s = malloc(INPUT_STRING_SIZE+1);			/* user input string */
  tok_t *t;			/* tokens parsed from input */
  int lineNum = 0;
  int fundex = -1;
  pid_t pid = getpid();		/* get current processes PID */
  pid_t ppid = getppid();	/* get parents PID */
  pid_t cpid, tcpid, cpgid;

  init_shell();

  // printf("%s running as PID %d under %d\n",argv[0],pid,ppid);

  // fprintf(stdout, "%d: ", lineNum);
  while ((s = freadln(stdin))){
    t = getToks(s); /* break the line into tokens */
    fundex = lookup(t[0]); /* Is first token a shell literal */
    if(fundex >= 0) cmd_table[fundex].fun(&t[1]);
    else {
      process* p = create_process(&t[0]);
      if(p != NULL)
    {
        add_process(p);
        int status = launch_process(p);
        if(status == EXECUTE_FAILED)
          printf("command not found\n");
      }
    }
    // fprintf(stdout, "%d: ", ++lineNum);
  }
  return 0;
}
