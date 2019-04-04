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

void release_block_reservation(int block_offset, int blocks_used) {

  sem_wait(BLOCK_BITMAP_LOCK);

  for (int j = 0; j < blocks_used; j++) 
		bit_array_clear(SHM_BLOCK_BITMAP, block_offset + j);

  sem_post(BLOCK_BITMAP_LOCK);

}

int create_block_reservation(int blocks_needed) {
  // Finds an area of free blocks in our database file.

  bool found = false;
  int retval = -1;
	int i, j;

  sem_wait(BLOCK_BITMAP_LOCK);

  for (j = 0; j < MAX_BLOCKS; j++) {
    for (i = 0; i < blocks_needed; i++) {
      if (bit_array_test(SHM_BLOCK_BITMAP, i + j) != 0) {// didn't find a contiguous block
        j += i;
        break;
      }
    }
    if (i == blocks_needed) {
      found = true;
      break;
    }
  }

  if (found) {
		// Found a good set of blocks. Mark them as used.
    for (i = 0; i < blocks_needed; i++) 
      bit_array_set(SHM_BLOCK_BITMAP, i + j);
    retval = j;
  }

 // commit the whole block bitmap to disk
  msync(SHM_BLOCK_BITMAP, BLOCK_BITMAP_BYTES, MS_SYNC);
  sem_post(BLOCK_BITMAP_LOCK);

  return(retval);
}


// Returns a memory pointer to the object read from the DB or
// NULL if the object cannot be retrieved. The caller is
// responsible for freeing the buffer.
char* read_obj(struct block_ptr obj) {
  char*     buffer;
  int       byte_count = obj.blocks * BLOCK_SIZE;
  int64_t		byte_offset = obj.block_offset * BLOCK_SIZE;
  int       bytes_read = 0;

  // Allocate a zero-padded buffer for the object.
  if ((buffer = malloc(byte_count)) == NULL) {
    perror("malloc failed in read_record()");
    return NULL;
  }
  memset(buffer, '\0', byte_count);

	// Read the object from disk into the buffer.
  if ((bytes_read = pread(DB_FD, (void*)buffer, byte_count, byte_offset)) == -1) {
    perror("pread failed in read_record");
    free(buffer);
    return NULL;
  }

  return buffer;
}


int delete_obj(struct block_ptr obj) {
  void*       buffer;
  int         byte_count = 0;
  int64_t     byte_offset;

  byte_count = obj.blocks * BLOCK_SIZE;
  byte_offset = obj.block_offset * BLOCK_SIZE;

  // Make a buffer of zeros as big as the object to be erased.
  if ((buffer = malloc(byte_count)) == NULL) {
    perror("malloc failed in delete_record()");
    return -1;
  }
  memset(buffer, '\0', byte_count);

	// Write over the object in the DB with the zeroed buffer.
  int rc = pwrite(DB_FD, buffer, byte_count, byte_offset);
	free(buffer);

	if (rc == -1) {
    perror("pwrite failed in delete_record");
    return -1;
  }

  // Mark these blocks as usable again.
  release_block_reservation(obj.block_offset, obj.blocks);
  return 0;
}

int write_obj(struct block_ptr *ptr, const void *obj, const int s) {

  void *buffer;
  int block_offset;
  int64_t byte_offset;
	int blocks = s / BLOCK_SIZE;
	if (s % BLOCK_SIZE != 0) blocks++;
  int byte_count = blocks * BLOCK_SIZE;

	// Find us some free space in the db file.
  if ((block_offset = create_block_reservation(blocks)) == -1) {
    fprintf(stderr, "Failed to reserve space in the block bitmap.\n");
    return -1;
  }

	// Create a zero-padded buffer at least as big as the object.
	// Copy the object to be stored into the buffer.
  if ((buffer = malloc(byte_count)) == NULL) {
    perror("malloc failed in write_record()");
    release_block_reservation(block_offset, blocks);
    return -1;
  }
  memset(buffer, '\0', byte_count);
  memcpy(buffer, obj, s);

  // Write the buffer to the appropriate location in our file.
  byte_offset = block_offset * BLOCK_SIZE;
  int rc = pwrite(DB_FD, buffer, byte_count, byte_offset);
	free(buffer);

	if (rc == -1) {
    perror("pwrite failed in write_record");
    release_block_reservation(block_offset, blocks);
    return -1;
  }

	// Write was successful. Update the pased-in block pointer.
	ptr->block_offset = block_offset;
	ptr->blocks = blocks;

  return 0;
}



int bit_array_set(char bit_array[], int bit) {

  int  byte_offset = floor(bit/8);
  int   bit_offset = bit % 8;
  int   cmp = 1 << bit_offset;

  return(bit_array[byte_offset] |= cmp);
}


int bit_array_test(const char bit_array[], int bit) {
  // returns > 0 if set. returns 0 if the bit is clear.

  int   byte_offset = floor(bit/8);
  int   bit_offset = bit % 8;
  int   cmp = 1 << bit_offset;

  return(bit_array[byte_offset] & cmp);
}

int bit_array_clear(char bit_array[], int bit) {

  int  byte_offset = floor(bit/8);
  int  bit_offset = bit % 8;
  int  cmp = 1 << bit_offset;

  return(bit_array[byte_offset] &= (~cmp));
}
