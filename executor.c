#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sysexits.h>
#include <err.h>
#include <unistd.h>  /* fork() */
#include <sys/types.h>  /* pid_t */
#include <sys/wait.h>  /* wait() */
#include <fcntl.h>
#include "command.h"
#include "executor.h"

#define OPEN_FLAGS (O_WRONLY | O_TRUNC | O_CREAT)
#define DEF_MODE 0664
#define MAX_STR_LEN 1024

/* function prototypes */
int execute_aux(struct tree *t, int parent_input_fd, int parent_ouput_fd); 
static void check_sys_call(int fd, const char *sys_call);
int execute_NONE(struct tree *t, int parent_input_fd, int parent_ouput_fd);
int execute_AND(struct tree *t, int parent_input_fd, int parent_ouput_fd);
int execute_OR(struct tree *t, int parent_input_fd, int parent_ouput_fd);
int execute_SEMI(struct tree *t, int parent_input_fd, int parent_ouput_fd);
int execute_PIPE(struct tree *t, int parent_input_fd, int parent_ouput_fd);
int execute_SUBSHELL(struct tree *t, int parent_input_fd, int parent_ouput_fd);

/* ========================================================================= */

/* execute() will be called every time there's a command input */
int execute(struct tree *t) {
   if(t == NULL) { /* base case */
      return 0;
   } else { /* recursive call */
      return execute_aux(t, STDIN_FILENO, STDOUT_FILENO);
      /* the root node has stdin and stdout as fd */
   }
}

/* Return 0 if execution success, Return 1 or other integers if failure */
int execute_aux(struct tree *t, int parent_input_fd, int parent_ouput_fd) {
   if (t->conjunction == NONE) {
      return execute_NONE(t, parent_input_fd, parent_ouput_fd);
   } else if (t->conjunction == AND) {
      return execute_AND(t, parent_input_fd, parent_ouput_fd);
   } else if (t->conjunction == OR) {
      return execute_OR(t, parent_input_fd, parent_ouput_fd);
   } else if (t->conjunction == SEMI) {
      return execute_SEMI(t, parent_input_fd, parent_ouput_fd);
   } else if (t->conjunction == PIPE) {
      return execute_PIPE(t, parent_input_fd, parent_ouput_fd);
   } else if (t->conjunction == SUBSHELL) {
      return execute_SUBSHELL(t, parent_input_fd, parent_ouput_fd);
   } else {
      return 1;
   }
}

/* The NONE conjunction is always a leaf node */
int execute_NONE(struct tree *t, int parent_input_fd, int parent_ouput_fd) {

   /* deal with shell commands (exit, cd) first */
   if (strcmp(t->argv[0], "exit") == 0) {
      exit(0);

   } else if (strcmp(t->argv[0], "cd") == 0) {
      if (t->argv[1] == 0) { /* just "cd" command alone */
         char *loc = getenv("HOME");
         if (chdir(loc) == -1) {
            perror(loc);
            return EXIT_FAILURE;
         }
      } else { /* "cd" plus directory name */
         if (chdir(t->argv[1]) == -1) {
            perror(t->argv[1]);
            return EXIT_FAILURE;
         }
      }
      /* return to the execute_aux() function that called it */
      return 0; 

   } else { /* linux command --> fork, then let child dup2 & execvp */
      int result = fork();  

      if (result < 0) {
         perror("fork error");
         return 1;

      } else if (result > 0) {   /* parent */
         int status;

         wait(&status); /* waiting & reaping */

         if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return 0;
         } else {
            return 1;
         }

      } else {    /* child */
         int input_fd, output_fd, status;

         if(t->input != NULL){
            input_fd = open(t->input, O_RDONLY);
            check_sys_call(input_fd, "open");

            status = dup2(input_fd, STDIN_FILENO); /* dup2 input redirect */
            check_sys_call(status, "dup2");

            status = close(input_fd);
            check_sys_call(status, "close");
         } else {
            input_fd = parent_input_fd;
         }
         
         if(t->output != NULL){
            output_fd = open(t->output, OPEN_FLAGS, DEF_MODE);
            check_sys_call(output_fd, "open");
            
            status = dup2(output_fd, STDOUT_FILENO); /* dup2 output redirect */
            check_sys_call(status, "dup2");

            status = close(output_fd);
            check_sys_call(status, "close");
         } else {
            output_fd = parent_ouput_fd;
         }
         
         execvp(t->argv[0], t->argv); /* exec* call */

         /* won't reach this point, unless exec* call failed */
         fprintf(stderr, "Failed to execute %s\n", t->argv[0]);
         fflush(stdout);
         exit(EX_OSERR);
      }
   }
}

/* The AND node uses parent's input & output fd; no need to fork or dup2 */
int execute_AND(struct tree *t, int parent_input_fd, int parent_ouput_fd) {
  
   /* if left child executed successfully */
   if (execute_aux(t->left, parent_input_fd, parent_ouput_fd) == 0) {

      /* then execute the right child */
      return execute_aux(t->right, parent_input_fd, parent_ouput_fd);

   } else { /* left child failed */
      return EXIT_FAILURE;
   }
}

/* The PIPE node "|" needs two processes, one for each subtree */
/* parent(curr node) sends left subtree the write end of the pipe */
/* parent(curr node) sends right subtree the read end of the pipe */
int execute_PIPE(struct tree *t, int parent_input_fd, int parent_ouput_fd) {
   int pipe_fd[2], input_fd, output_fd, result;

   /* check for ambiguous output redirect */
   if (t->left->output != NULL) {
      /* if identified */
      printf("Ambiguous output redirect.\n");

      return EXIT_FAILURE; /* stop processing the pipe conjunction */

   /* check for ambiguous input redirect */
   } else if (t->right->input != NULL) {
      /* if identified */
      printf("Ambiguous input redirect.\n");

      return EXIT_FAILURE; /* stop processing the pipe conjunction */
   }
   
   /* Must pipe() before fork()!!!!!!  so that fd table is copied */
   check_sys_call(pipe(pipe_fd), "pipe");
   check_sys_call(result = fork(), "fork");

   if (result == 0) {  /* child code: left subtree --> write end of pipe */

      /* closing read end */
      check_sys_call(close(pipe_fd[0]), "close");

      /* send output to pipe's write end */
      check_sys_call(dup2(pipe_fd[1], STDOUT_FILENO), "dup2");

      /* closing write end after dup2 */
      check_sys_call(close(pipe_fd[1]), "close");

      if(t->input != NULL){
         input_fd = open(t->input, O_RDONLY);
         check_sys_call(input_fd, "open");
         result = dup2(input_fd, STDIN_FILENO); /* dup2 input redirect */
         check_sys_call(result, "dup2");
         result = close(input_fd);
         check_sys_call(result, "close");
      } else {
         input_fd = parent_input_fd;
      }
      exit(execute_aux(t->left, input_fd, pipe_fd[1])); /* end child process */

   } else if (result > 0) { /* parent: wait for left child first */
      int status; 

      /* closing write end */
      check_sys_call(close(pipe_fd[1]), "close");

      wait(&status); /* reaping */

      /* if left child was successful, then process right child */
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {

         /* get input from pipe */
         check_sys_call(dup2(pipe_fd[0], STDIN_FILENO), "dup2");
            
         /* closing read end after dup2 */
         check_sys_call(close(pipe_fd[0]), "close");
         
         if(t->output != NULL){
            output_fd = open(t->output, OPEN_FLAGS, DEF_MODE);
            check_sys_call(output_fd, "open");
            status = dup2(output_fd, STDOUT_FILENO); /* dup2 output redirect */
            check_sys_call(status, "dup2");
            status = close(output_fd);
            check_sys_call(status, "close");
         } else {
            output_fd = parent_ouput_fd;
         }
         return execute_aux(t->right, STDIN_FILENO, output_fd);
      } else {
         /* left child failed */
         return EXIT_FAILURE;
      }
   }
   return EXIT_FAILURE; /* to supress compiler warnings */
}

/* A subshell is a child process launched by a shell. Parentheses starts it */
/* Commands inside the parenthesis wn't affect parent shell's environment */
int execute_SUBSHELL(struct tree *t, int parent_input_fd, int parent_ouput_fd) {
   int result = fork();  

   if (result < 0) {
      perror("fork error");
      return 1;
   
   } else if (result > 0) { /* parent */
      int status;

      wait(&status); /* waiting & reaping */
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
         return 0;
      } else {
         return 1;
      }

   } else {    /* child */
      int status;
      int input_fd, output_fd;

      if(t->input != NULL){
         input_fd = open(t->input, O_RDONLY);
         check_sys_call(input_fd, "open");
         status = dup2(input_fd, STDIN_FILENO); /* dup2 input redirect */
         check_sys_call(status, "dup2");
         status = close(input_fd);
         check_sys_call(status, "close");
      } else {
         input_fd = parent_input_fd;
      }

      if(t->output != NULL){
         output_fd = open(t->output, OPEN_FLAGS, DEF_MODE);
         check_sys_call(output_fd, "open");
         status = dup2(output_fd, STDOUT_FILENO); /* dup2 output redirect */
         check_sys_call(status, "dup2");
         status = close(output_fd);
         check_sys_call(status, "close");
      } else {
         output_fd = parent_ouput_fd;
      }

      status = execute_aux(t->left, input_fd, output_fd);
      exit(status);
   }
}

/* The OR node "||" */
int execute_OR(struct tree *t, int parent_input_fd, int parent_ouput_fd) {

   return 0;
}

/* The SEMI node ";" */
int execute_SEMI(struct tree *t, int parent_input_fd, int parent_ouput_fd) {

   return 0;
}

/* Checks for system call errors: open(), close(), dup2(), pipe(), fork() */
/* for fork(): pid (instead of fd) will be the int parameter passed in */
static void check_sys_call(int fd, const char *sys_call) {
   if (fd < 0) {
      perror("system call failed\n"); 
      exit(EX_OSERR);
   }
   /* for wait(&status): check status in code */
   /* for exit(): check in code */
}

