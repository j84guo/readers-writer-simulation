#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

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
  }
  lock->readers++;
  pthread_mutex_unlock(&lock->mutex);
}

void rwlock_runlock(struct RWLock *lock)
{
  // If last reader to leave, notify room empty
  pthread_mutex_lock(&lock->mutex);
  lock->readers--;
  if (!lock->readers) {
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
    printf("reader entered: %d\n", lock->readers);
    sleep(rand() % 10);
    rwlock_runlock(lock);
  }
}

void run_writer(struct RWLock *lock)
{
  rwlock_wlock(lock);
  printf("writer entered: %d\n", lock->readers);
  sleep(5);
  rwlock_wunlock(lock);
}

// We ignore sem_wait/post errors, which mainly arise from EINTR. We also
// ignore pthread_mutex_lock/unlock errors, which should not occur in well
// formed code.
int main()
{
  struct RWLock lock;
  rwlock_init(&lock);

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
