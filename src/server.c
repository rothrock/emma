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
#include "status_codes.h"

void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

void sigterm_handler_parent(int s)
{
  // If we get a sigterm, kill everyone.
  if ((killpg(0, SIGTERM)) == -1) {
    perror("Can't kill off my children. Don't know why.");
    exit(-1);
  } else {
    exit(0);
  };
}

void sigterm_handler_child(int s)
{
  // Clean up and exit.
  fprintf(stderr, "Got signal %d\n", s);
  cleanup_and_exit(0);
}

void cleanup_and_exit(int retval) {
  sem_post(BLOCK_BITMAP_LOCK);
  msync(SHM_BLOCK_BITMAP, BLOCK_BITMAP_BYTES, MS_SYNC);
  close(DB_FD);
  exit(retval);
}


int tokenize_command(char* msg, char* token_vector[]) {
  char **token_ptr;
  int i = 0;
  for (token_ptr = token_vector; (*token_ptr = strsep(&msg, " ")) != NULL;)
    if (**token_ptr != '\0') {
        i++;
        if (++token_ptr >= &token_vector[MAX_ARGS])
          return -1;
    }
  return i;
}

int extract_command(char *token_vector[], int token_count) {

  char* commands[5] = { "quit",   // 0
                        "insert", // 1
                        "find",   // 2
                        "delete", // 3
                        "keys"    // 4
                      };
  int i = 0;
  if (token_count < 1) return -1;
  for (; i < 5; i++)
    if (strcmp(commands[i], token_vector[0]) == 0) return(i);
  return -1;
}

int srv(int accept_fd, int listen_fd) {

  char buffer[RECV_WINDOW] = "";   // recv buffer
  int msgbuflen = MSG_SIZE;
  char *msg; // Incoming message.
  char *send_msg; // Outgoing message.
  char *tmp_msg;
  void *msg_cursor;
  struct response_struct response;
  int msglen = 0; // length of the assembled message that we receive.
  int recvlen = 0; // how many bytes recv call returns.
  int responselen = 0;
  int offset;
  char* token_vector[MAX_ARGS] = {NULL};
  int token_count = 0;

  // Re-register the sigterm handler to our cleanup function.
  signal(SIGTERM, sigterm_handler_child);
  close(listen_fd); // Close this resource from our parent. We don't need it any more.

  while (1) {

    msglen = 0;
    msgbuflen = MSG_SIZE;
    msg = malloc(sizeof(char) * msgbuflen);
    msg_cursor = (void*)msg;
    bzero(msg, msgbuflen);


    // Wait for some data
    while (((recvlen = recv(accept_fd, (void*)buffer, RECV_WINDOW, MSG_PEEK)) == -1) && (errno == EAGAIN));
    if (recvlen == 0) {
      fprintf(stderr, "Client closed the connection.\n");
      close(accept_fd);
      cleanup_and_exit(0);
    };

    // Receive data from our buffered stream until we would block.
    while ((recvlen = recv(accept_fd, (void*)buffer, RECV_WINDOW, 0)) != -1) {
      if (recvlen == 0) {
        fprintf(stderr, "Client closed the connection.\n");
        close(accept_fd);
        cleanup_and_exit(0);
      };

      if (recvlen == -1) {
        fprintf(stderr, "Got error %d from recv.\n", errno);
        close(accept_fd);
        cleanup_and_exit(-1);
      };

      // Extend our message buffer if need be.
      if ((msglen += recvlen) > (msgbuflen)) {
        msgbuflen += msgbuflen;
        offset = msg_cursor - (void*)msg;
        tmp_msg = malloc(sizeof(char) * msgbuflen);
        bzero(tmp_msg, msgbuflen);
        memcpy(tmp_msg, msg, offset);
        msg_cursor = tmp_msg + offset;
        free(msg);
        msg = tmp_msg;
        fprintf(stderr, "msgbuflen expanded to %d\n", msgbuflen);
      }

      memcpy(msg_cursor, (void*)buffer, recvlen);
      msg_cursor += recvlen;
      if (memchr((void*)buffer, '\n', recvlen)) break; // Got a terminator character. Go process our message.

    }

    tmp_msg = msg;
    strsep(&tmp_msg, "\r\n");

    token_count = tokenize_command(msg, token_vector);

    switch (extract_command(token_vector, token_count))  {

      case 0: // quit
        cleanup_and_exit(0);
        break;

      case 1: // insert
        response = insert_command(token_vector, token_count);
        break;

      case 2: // find
        response = find_command(token_vector, token_count);
        break;

      case 3: // delete
        response = delete_command(token_vector, token_count);
        break;

      case 4: // keys
        response = keys_command(token_vector, token_count);
        break;

      default:
        if ((response.msg = malloc(sizeof(char) * MSG_SIZE)) == NULL) {
          perror(NULL);
          cleanup_and_exit(0);
        }
        bzero(response.msg, MSG_SIZE);
        sprintf(response.msg, "Unknown command.");
        response.status = 1;
    }

    responselen = prepare_send_msg(response, &send_msg);

    if((send(accept_fd, (void*)send_msg, responselen, 0) == -1)) perror("Send failed");
    free(msg);
    free(response.msg);
    free(send_msg);

  };

  return(0);
}

int prepare_send_msg(struct response_struct response, char** send_msg) {
  char status_msg[MSG_SIZE] = { '\0' };
  int responselen;

  sprintf(status_msg, "STATUS: %s\nSIZE: %d\n",
    STATUS_CODES[response.status],
    (int)strlen(response.msg));
  responselen = strlen(response.msg) + strlen(status_msg) + 2;
  if ((*send_msg = malloc(responselen)) == NULL) {
    perror(NULL);
    cleanup_and_exit(0);
  }
  *send_msg[0] = '\0';
  strcat(*send_msg, status_msg);
  strcat(*send_msg, response.msg);
  strcat(*send_msg, "\n\n");
  return responselen;
}

struct response_struct find_command(char* token_vector[], int token_count) {

  char key[KEY_LEN] = "";
  struct block_ptr ptr = {.block_offset = 0, .blocks = 0};
  struct response_struct response;
  response.status = 0;

  response.msg = malloc(sizeof(char) * MSG_SIZE);

  if (token_count == 1) {
    sprintf(response.msg, "Arguments missing.");
    response.status = 1;
    return response;
  }

  strcat(key, token_vector[1]);

	// ptr = btree_find(key)
	// response.msg = read_obj(ptr);
  sprintf(response.msg, "Not working yet. block_offset is %lld.", ptr.block_offset);
  response.status = 1;

  return response;
}

struct response_struct insert_command(char* token_vector[], int token_count) {

  char key[KEY_LEN] = "";
  struct block_ptr ptr = {.block_offset = 0, .blocks = 0};
  struct response_struct response;
  response.status = 0;

  response.msg = malloc(sizeof(char) * MSG_SIZE);

  if (token_count == 1) {
    sprintf(response.msg, "Arguments missing.");
    response.status = 1;
    return response;
  }

  strcat(key, token_vector[1]);
	// ptr = btree_find(key)
	// response.msg = read_obj(ptr);
  sprintf(response.msg, "Not working yet. block_offset is %lld.", ptr.block_offset);
  response.status = 1;

  return response;
}

struct response_struct delete_command(char* token_vector[], int token_count) {

  char key[KEY_LEN] = "";
  struct block_ptr ptr = {.block_offset = 0, .blocks = 0};
  struct response_struct response;
  response.status = 0;

  response.msg = malloc(sizeof(char) * MSG_SIZE);

  if (token_count == 1) {
    sprintf(response.msg, "Arguments missing.");
    response.status = 1;
    return response;
  }

  strcat(key, token_vector[1]);
	// ptr = btree_find(key)
	// response.msg = read_obj(ptr);
  sprintf(response.msg, "Not working yet. block_offset is %lld.", ptr.block_offset);
  response.status = 1;

  return response;
}

struct response_struct keys_command(char* token_vector[], int token_count) {

  char key[KEY_LEN] = "";
  struct block_ptr ptr = {.block_offset = 0, .blocks = 0};
  struct response_struct response;
  response.status = 0;

  response.msg = malloc(sizeof(char) * MSG_SIZE);

  if (token_count == 1) {
    sprintf(response.msg, "Arguments missing.");
    response.status = 1;
    return response;
  }

  strcat(key, token_vector[1]);
	// ptr = btree_find(key)
	// response.msg = read_obj(ptr);
  sprintf(response.msg, "Not working yet. block_offset is %lld.", ptr.block_offset);
  response.status = 1;

  return response;
}
