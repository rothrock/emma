/*
Copyright (c) 2019 Joseph Rothrock (rothrock@rothrock.org)

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

#include "emma.h"

char DATA_HOME[4096] = "/var/emma";

int main(int argc, char* argv[]) {

  struct sockaddr incoming;
  socklen_t addr_size = sizeof(incoming);
  int listen_fd, accept_fd;
  char* port = "4080";
  char* host = "::1";
  char db_file[4096];
  char block_bitmap_file[4096];
  int chld;
  int ch;


  // parse our cmd line args
  while ((ch = getopt(argc, argv, "d:h:p:")) != -1) {
    switch (ch) {

      case 'd':
        sprintf(DATA_HOME, "%s", optarg);
        break;

      case 'h':
        host = optarg;
        break;

      case 'p':
        port = optarg;
        break;

     case '?':

     default:
       usage(argv[0]);
     }
  }
  argc -= optind;
  argv += optind;

  sprintf(db_file, "%s/db", DATA_HOME);
  sprintf(block_bitmap_file, "%s/block_bitmap", DATA_HOME);


  // Coordinate exclusive access to the db block bitmap.
  if ((BLOCK_BITMAP_LOCK = sem_open("block_bitmap_lock", O_CREAT, 0666, 1)) == SEM_FAILED) {
    perror("semaphore init failed");
    exit(-1);
  }
  sem_post(BLOCK_BITMAP_LOCK);


  // Memory-map our block bitmap file creating it if necessary.
  // The block bitmap keeps track of free/busy blocks in the db file.
  if ((BLOCK_BITMAP_FD = open(block_bitmap_file, O_RDWR | O_CREAT, 0666)) == -1) {
    fprintf(stderr, "Couldn't open db block bitmap file %s\n", block_bitmap_file);
    perror(NULL);
    exit(-1);
  }
  if ((SHM_BLOCK_BITMAP = mmap((caddr_t)0, BLOCK_BITMAP_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, BLOCK_BITMAP_FD, 0)) == MAP_FAILED) {
    perror("Problem mmapping the block bitmap");
    exit(-1);
  }


  // register a function to reap our dead children
  signal(SIGCHLD, sigchld_handler);

  // Register a function to kill our children.
  // We'll unregister this function in our children.
  signal(SIGTERM, sigterm_handler_parent);

  // Open our database file
  if ((DB_FD = open(db_file, O_RDWR | O_CREAT, 0666)) == -1) {
    fprintf(stderr, "Couldn't open database file named %s\n", db_file);
    perror(NULL);
    exit(-1);
  }

  // Demonize ourself.
  if ((chld = fork()) != 0 ) {printf("%d\n",chld); return(0);};

  // Start listening
  if ((listen_fd = start_listening(host, port, BACKLOG)) == -1) {
    fprintf(stderr, "Call to start_listening failed\n");
    perror(NULL);
    exit(-1);
  }

  fprintf(stderr, "Started listening.\n");

  while (1) {

    // Accept new connection.
    if ((accept_fd = accept(listen_fd, (struct sockaddr *)&incoming, &addr_size)) == -1) {
      fprintf(stderr, "Call to accept() failed.\n");
      return(-1);
    }

    fcntl(accept_fd, F_SETFD, O_NONBLOCK);

    // Start a child with the new connection.
    if ((chld = fork()) == 0 ){
      srv(accept_fd, listen_fd);
    } else {
      close(accept_fd);
    }

  }

  return(0);

} // end main

void usage(char *argv) {
  fprintf(stderr, "usage: %s [-h listen_addr] [-p listen_port] [-d /path/to/db/directory]\n", argv);
  exit(-1);
}

