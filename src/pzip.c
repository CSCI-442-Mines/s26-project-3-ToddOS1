#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pzip.h"

pthread_mutex_t lock; // This will be used when writing to char_frequency
pthread_mutex_t threadNumber; // This will wait for the thread to know its own id before it is changed
pthread_barrier_t synchronize;

struct commonVariables{
  int n_threads;
  char *input_chars;
  int input_chars_size;
	struct zipped_char *zipped_chars;
  int *zipped_chars_count;
	int *char_frequency;
  int thread_number;
  int characters_per_thread;
  volatile int *thread_slot_size;
};

static void *zipThread(void *passedVariables_void){
  // Translates back from void to struct ptr
  struct commonVariables *passedVariables = (struct commonVariables*) passedVariables_void;

  // Save the local thread number and unlock
  int local_thread_number = passedVariables->thread_number;
  pthread_mutex_unlock(&threadNumber); // This will unlock the main thread

  // Create a local array of zipped chars
  struct zipped_char *local_zipped_chars;
  local_zipped_chars = malloc(passedVariables->characters_per_thread * sizeof(*local_zipped_chars)); // Preallocate
  if (local_zipped_chars == NULL) {
      fprintf(stderr, "malloc failed\n");
  }

  // Specify beginning and ending index for the array of characters for the thread to analyze
  int beginningIndex = local_thread_number * passedVariables->characters_per_thread;
  int endingIndex = beginningIndex + passedVariables->characters_per_thread;

  // Create temporary variables for loop
  int zipped_char_array_index = 0;

  // Initialize the array index to starting value (helps simplify my looping logic)
  local_zipped_chars[zipped_char_array_index].character = passedVariables->input_chars[beginningIndex];
  local_zipped_chars[zipped_char_array_index].occurence = 0;

  for(int i = beginningIndex; i < endingIndex; i++){
    if(local_zipped_chars[zipped_char_array_index].character == passedVariables->input_chars[i]){ // If we've already seen the character just add to count
      local_zipped_chars[zipped_char_array_index].occurence++;
    } else { // Increase the size of the array if there is a different letter
      zipped_char_array_index++;
      //local_zipped_chars = realloc(local_zipped_chars, (zipped_char_array_index + 1) * sizeof(*local_zipped_chars)); // This is weird, it allocates just one more for the array
      local_zipped_chars[zipped_char_array_index].character = passedVariables->input_chars[i];
      local_zipped_chars[zipped_char_array_index].occurence = 1;
    }
  }

  // This will check if we know the sizes of everything up to the current thread
  int known = 0;
  int final_index = 0; // allocate up here to save on time
  passedVariables->thread_slot_size[local_thread_number] = zipped_char_array_index + 1;

  while(known == 0){ // Wait here until all other threads know their size
    known = 1;
    for(int i = 0; i <= local_thread_number; i++){
      if(passedVariables->thread_slot_size[i] == 0){
        known = 0;
        break;
      }
    }
  }

  // Calculate the index starting location for each thread
  for(int i = 0; i < local_thread_number; i++){
    final_index += passedVariables->thread_slot_size[i];
  }

  // Go to global index baed on final_index
  for(int i = 0; i <= zipped_char_array_index; i++){
    passedVariables->zipped_chars[final_index + i].character = local_zipped_chars[i].character; // Adds our local zipped_chars to global index
    passedVariables->zipped_chars[final_index + i].occurence = local_zipped_chars[i].occurence;
  }

  // Modify data that is globally accessed
  pthread_mutex_lock(&lock);
  for(int i = 0; i <= zipped_char_array_index; i++){
    passedVariables->char_frequency[local_zipped_chars[i].character - 'a'] += local_zipped_chars[i].occurence;
  }

  *(passedVariables->zipped_chars_count) += zipped_char_array_index + 1;
  pthread_mutex_unlock(&lock);

  free(local_zipped_chars);
  pthread_barrier_wait(&synchronize); // Wait at end here

  return NULL;
}


/**
 * pzip() - zip an array of characters in parallel
 *
 * Inputs:
 * @n_threads:		   The number of threads to use in pzip
 * @input_chars:		   The input characters (a-z) to be zipped
 * @input_chars_size:	   The number of characaters in the input file
 *
 * Outputs:
 * @zipped_chars:       The array of zipped_char structs
 * @zipped_chars_count:   The total count of inserted elements into the zipped_chars array
 * @char_frequency: Total number of occurences
 *
 * NOTE: All outputs are already allocated. DO NOT MALLOC or REASSIGN THEM !!!
 *
 */
void pzip(int n_threads, char *input_chars, int input_chars_size,
	  struct zipped_char *zipped_chars, int *zipped_chars_count,
	  int *char_frequency)
{
  // Array for storing thread IDs
  pthread_t threadIDs[n_threads];

  // This portion will allow me to pass everthing that we want into the helper function
  struct commonVariables passedVariables;
  passedVariables.n_threads = n_threads; // Probably can depricate
  passedVariables.input_chars = input_chars;
  passedVariables.input_chars_size = input_chars_size; // Probably can depricate
  passedVariables.zipped_chars = zipped_chars;
  passedVariables.zipped_chars_count = zipped_chars_count;
  passedVariables.char_frequency = char_frequency;
  passedVariables.characters_per_thread = input_chars_size / n_threads;
  int thread_slot_size[n_threads];
  for(int i = 0; i < n_threads; i++){
    thread_slot_size[i] = 0; // Sets all mallocs to 0 initially
  }
  passedVariables.thread_slot_size = thread_slot_size;

  // Initialize barrier/mutex before I create threads
  pthread_barrier_init(&synchronize, NULL, n_threads + 1);
  pthread_mutex_init(&lock, NULL);
  pthread_mutex_init(&threadNumber, NULL);
  pthread_mutex_lock(&threadNumber); // We do this so that we can't unlock in the thread before we lock
  // Loop for number of threads I need
  for(int i = 0; i < n_threads; i++){
    passedVariables.thread_number = i;
    if(pthread_create(&threadIDs[i], NULL, zipThread, &passedVariables) != 0){
      fprintf(stderr, "pthread_create failed\n");
    }
    pthread_mutex_lock(&threadNumber);
  }

  // Wait until all threads are complete
  pthread_barrier_wait(&synchronize);

  // Join everything back together
  for(int i = 0; i < n_threads; i++){
    pthread_join(threadIDs[i], NULL);
  }
}
