# PROJECT SHELL

## Introduction

This project is to implement a shell. My code is in the file executor.c

What's been provided is a command parser that reads a command provided at the command prompt and creates a tree.
My job is to process the created tree so that the command represented by the tree is executed. 


## Basic Structures
The command tree is a binary tree where a C structure represents a tree node (see the file command.h).
Each node has a (enum) type. 
I implemented commands associated with the NONE, AND, PIPE and SUBSHELL nodes. 
Each node has a left and right subtree (which are null if not subtree is present). 
A NONE node is just a leaf of the tree. 

Each node has an argv array that has the command arguments.
t->argv[0] has the command to execute, and the rest of t->argv will have command line arguments.

The input field is a string that represents a file used for input redirection and the output field represents the file used for output redirection. 
t->input is the file used for input redirection; 
t->output is the file used for output redirection. 


 
