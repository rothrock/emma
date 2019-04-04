/*
Copyright (c) 2011 Joseph Rothrock (rothrock@rothrock.org)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <signal.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <libgen.h>
#include "longlong.h"

/*
  Here is the order for setting up a networked connection:
  socket()
  bind()
  listen()
  accept()
*/

#define BACKLOG 25
#define BLOCK_SIZE 4096
#define MAX_BLOCKS 1073741824
#define BLOCK_BITMAP_BYTES 134217728

#define MSG_SIZE 1024
#define RECV_WINDOW 512
#define IDX_ENTRY_SIZE 256
#define NODE_KEYS 10
#define KEY_LEN (IDX_ENTRY_SIZE - 2*(sizeof(int)) - sizeof(int64_t))
#define MAX_ARGS 100

struct response_struct {
  unsigned int status;
  char* msg;
};

struct block_ptr { // Pointer to an object in the db file.
  int64_t  block_offset;
  int      blocks;
};

struct btree_node { 
  char keys[NODE_KEYS][KEY_LEN]; 
  struct block_ptr child_ptrs[NODE_KEYS + 1]; 
}; 


// Globals
sem_t*          BLOCK_BITMAP_LOCK;
char            *SHM_BLOCK_BITMAP;
int             BLOCK_BITMAP_FD;
int             DB_FD;


// Function signatures
int       start_listening(char* host, char* port, int backlog);
void      sigchld_handler(int s);
void      sigterm_handler_parent(int s);
void      sigterm_handler_child(int s);
int       srv(int accept_fd, int listen_fd);
int       extract_command(char *token_vector[], int token_count);
int       tokenize_command(char* msg, char* token_vector[]);
int       bit_array_set(char bit_array[], int bit);
int       bit_array_test(const char bit_array[], int bit);
int       bit_array_clear(char bit_array[], int bit);
int       create_block_reservation(int blocks_needed);
void      cleanup_and_exit(int retval);
void      usage(char *argv);
struct response_struct insert_command(char* token_vector[], int token_count);
struct response_struct find_command(char* token_vector[], int token_count);
struct response_struct delete_command(char* token_vector[], int token_count);
struct response_struct keys_command(char* token_vector[], int token_count);
int       prepare_send_msg(struct response_struct response, char** send_msg);
