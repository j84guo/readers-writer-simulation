#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

// Allows multiple readers, only one writer
struct RWLock {
  int readers;
  pthread_mutex_t mutex;
  pthread_mutex_t door;
  sem_t empty;
};

int rwlock_init(struct RWLock *lock)
{
  lock->readers = 0;
  if (pthread_mutex_init(&lock->mutex, NULL) ||
      pthread_mutex_init(&lock->door, NULL) ||
      sem_init(&lock->empty, 0, 1) == -1) {
    return -1;
  }
  return 0;
}

int rwlock_destroy(struct RWLock *lock)
{
  int res = 0;
  if (pthread_mutex_destroy(&lock->mutex)) {
    res = -1;
  }
  if (pthread_mutex_destroy(&lock->door)) {
    res = -1;
  }
  if (sem_destroy(&lock->empty) == -1) {
    res = -1;
  }
  return res;
}

void rwlock_rlock(struct RWLock *lock)
{
  // Enter room
  pthread_mutex_lock(&lock->door);
  pthread_mutex_unlock(&lock->door);

  // If first reader to arrive, wait for room empty
  pthread_mutex_lock(&lock->mutex);
  if (!lock->readers) {
    sem_wait(&lock->empty);
    printf("first reader acquired room\n");
  }
  lock->readers++;
  pthread_mutex_unlock(&lock->mutex);
}

void rwlock_runlock(struct RWLock *lock)
{
  // If last reader to leave, notify room empty
  pthread_mutex_lock(&lock->mutex);
  printf("decrementing lock->readers: %d\n", lock->readers);
  lock->readers--;
  if (!lock->readers) {
    printf("last reader leaving room\n");
    sem_post(&lock->empty);
  }
  pthread_mutex_unlock(&lock->mutex);
}

void rwlock_wlock(struct RWLock *lock)
{
  // Block door and wait for room empty so you don't starve
  pthread_mutex_lock(&lock->door);
  sem_wait(&lock->empty);
}

void rwlock_wunlock(struct RWLock *lock)
{
  // When exiting, unblock the door and notify room empty
  pthread_mutex_unlock(&lock->door);
  sem_post(&lock->empty);
}

void syserr(char *msg)
{
  perror(msg);
  exit(1);
}

void *run_reader(void *ptr)
{
  struct RWLock *lock = ptr;

  while (1) { 
    sleep(rand() % 10);
    rwlock_rlock(lock);
    printf("reader entered\n");
    sleep(rand() % 3);
    rwlock_runlock(lock);
  }
}

void run_writer(struct RWLock *lock)
{
  rwlock_wlock(lock);
  printf("writer entered: %d\n", lock->readers);
  sleep(20);
  rwlock_wunlock(lock);
}

// We ignore sem_wait/post errors, which mainly arise from EINTR. We also
// ignore pthread_mutex_lock/unlock errors, which should not occur in well
// formed code.
//
// Process exists when we return and child threads are destroyed. As an
// exercise, we may cancel then join readers on EOF, although this would
// require tedious usage of pthread_cleanup_push/pop.
int main()
{
  struct RWLock lock;
  if (rwlock_init(&lock) == -1) {
    syserr("rwlock_init");
  }

  srand(time(NULL));
  int num_readers = 5;
  pthread_t readers[num_readers];

  for (int i=0; i<num_readers; i++) {
    if (pthread_create(&readers[i], NULL, run_reader, &lock)) {
      syserr("pthread_create");
    }
  }

  char buf[1024];
  while (1) {
    if (fgets(buf, sizeof(buf), stdin)) {
      run_writer(&lock);
    } else if (ferror(stdin)) {
      syserr("fgets");
    } else {
      break;
    }
  }
}
