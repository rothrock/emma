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

int start_listening(char* host, char* port, int backlog) {
  int listen_fd = 0;
  struct addrinfo hints, *res, *p; // Parms for socket() and bind() calls.
  int rc;

  memset(&hints, 0, sizeof(hints)); // Zero out hints.
  hints.ai_family = AF_UNSPEC;      // use IPv4 or IPv6.
  hints.ai_socktype = SOCK_STREAM;  // Normal TCP/IP reliable, buffered I/O.

  // Use getaddrinfo to allocate and populate *res.
  if ((rc = getaddrinfo(host, port, &hints, &res)) != 0) {
    fprintf(stderr, "The getaddrinfo() call failed with %d\n", rc);
    return -1;
  }

  for (p = res; p != NULL; p = p->ai_next) {
    // Make a socket using the fleshed-out res structure:
    if ((listen_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      fprintf(stderr, "The socket call failed\n");
      return -1;
    }
    // Bind the file descriptor to the port we passed in to getaddrinfo():
    if (bind(listen_fd, res->ai_addr, res->ai_addrlen) == -1) {
      fprintf(stderr, "The bind() call failed. listen_fd is %d\n", listen_fd);
      return -1;
    }
    break; //We successfully bound to something. Stop looping.
  }

  // Start listening and put our listener in the connection list.
  if (listen(listen_fd, backlog) == -1) {
    perror("The listen() call failed.\n");
    return -1;
  }

  return(listen_fd);

}

