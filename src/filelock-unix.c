
#include <R.h>
#include <Rinternals.h>

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <string.h>

#include "filelock.h"

#define FILELOCK_INTERRUPT_INTERVAL 200

static int filelock_dummy_object = 0;

struct sigaction filelock_old_sa;

void filelock__finalizer(SEXP x) {
  void *ptr = R_ExternalPtrAddr(x);
  SEXP des;

  if (!ptr) return;

  des = R_ExternalPtrTag(x);
  close(INTEGER(des)[0]);

  R_ClearExternalPtr(x);
}

void filelock__alarm_callback (int signum) {
  /* Restore signal handler */
  sigaction(SIGALRM, &filelock_old_sa, 0);
}

int filelock__interruptible(int filedes, struct flock *lck,
			    const char *c_path,
			    int c_exclusive, int c_timeout) {

  struct itimerval timer;
  struct sigaction sa;
  int timeleft = c_timeout;
  int ret = 1;

  while (c_timeout < 0 || timeleft > 0) {

    /* If block forever, then always use the interrupt interval,
       If timeout and just a little time left, use that. */

    int waitnow = FILELOCK_INTERRUPT_INTERVAL;
    if (c_timeout >= 0 && timeleft < FILELOCK_INTERRUPT_INTERVAL) {
      waitnow = timeleft;
    }

    timer.it_value.tv_sec = waitnow / 1000;
    timer.it_value.tv_usec = (waitnow % 1000) * 1000;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;

    memset(&sa, 0, sizeof (sa));
    sa.sa_handler = &filelock__alarm_callback;
    sigaction(SIGALRM, &sa, &filelock_old_sa);

    setitimer(ITIMER_REAL, &timer, 0);
    ret = fcntl(filedes, F_SETLKW, lck);

    /* If ret != -1, then we have the lock, return.
       If -1, but not EINTR, then a real error happened. */
    if (ret != -1) { ret = 0; break; }
    if (ret == -1 && errno != EINTR) {
      error("Cannot lock file: '%s': %s", c_path, strerror(errno));
    }

    /* Otherwise, need to wait, check for interrupts, and start over */
    R_CheckUserInterrupt();
    timeleft -= FILELOCK_INTERRUPT_INTERVAL;
  }

  return ret;
}

SEXP filelock__make_lock_handle(const char *c_path, int filedes) {
  SEXP ptr, result;

  ptr = PROTECT(R_MakeExternalPtr(&filelock_dummy_object,
				  ScalarInteger(filedes), R_NilValue));
  R_RegisterCFinalizerEx(ptr, filelock__finalizer, 0);

  result = PROTECT(allocVector(VECSXP, 2));
  SET_VECTOR_ELT(result, 0, ptr);
  SET_VECTOR_ELT(result, 1, mkString(c_path));

  UNPROTECT(2);
  return result;
}

SEXP filelock_lock(SEXP path, SEXP exclusive, SEXP timeout) {
  struct flock lck;
  const char *c_path = CHAR(STRING_ELT(path, 0));
  int c_exclusive = LOGICAL(exclusive)[0];
  int c_timeout = INTEGER(timeout)[0];
  int filedes, ret;
  SEXP result;

  /* Check if this file was already locked. */
  filelock__list_t *node = filelock__list_find(c_path);
  if (node) return filelock__make_lock_handle(c_path, node->file);

  lck.l_type = c_exclusive ? F_WRLCK : F_RDLCK;
  lck.l_whence = SEEK_SET;
  lck.l_start = 0;
  lck.l_len = 0;

  filedes = open(c_path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  if (filedes == -1) error("Cannot open lock file: %s", strerror(errno));

  /* We create the retult object here, so that the finalizer
     will close the file descriptor on error or interrupt. */

  result = PROTECT(filelock__make_lock_handle(c_path, filedes));

  /* One shot only? Do not block if cannot lock */
  if (c_timeout == 0) {
    ret = fcntl(filedes, F_SETLK, &lck);
    if (ret == -1) {
      if (errno == EAGAIN || errno == EACCES) {
	UNPROTECT(1);
	return R_NilValue;
      }
      error("Cannot lock file: '%s': %s", c_path, strerror(errno));
    }

  } else {
    ret = filelock__interruptible(filedes, &lck, c_path, c_exclusive,
				  c_timeout);
  }

  /* Failed to acquire the lock */
  if (ret) {
    UNPROTECT(1);
    return R_NilValue;
  }

  if (filelock__list_add(c_path, filedes)) error("Not enough memory");

  UNPROTECT(1);
  return result;
}

SEXP filelock_unlock(SEXP lock) {
  void *ptr = R_ExternalPtrAddr(VECTOR_ELT(lock, 0));

  if (ptr) {
    SEXP des = R_ExternalPtrTag(VECTOR_ELT(lock, 0));
    const char *c_path = CHAR(STRING_ELT(VECTOR_ELT(lock, 1), 0));
    filelock__list_remove(c_path);
    close(INTEGER(des)[0]);
    R_ClearExternalPtr(VECTOR_ELT(lock, 0));
  }

  return ScalarLogical(1);
}

SEXP filelock_is_unlocked(SEXP lock) {
  void *ptr = R_ExternalPtrAddr(VECTOR_ELT(lock, 0));
  if (ptr) {
    const char *c_path = CHAR(STRING_ELT(VECTOR_ELT(lock, 1), 0));
    int inlist = filelock__list_find(c_path) != 0;
    return ScalarLogical(! inlist);
  } else {
    return ScalarLogical(1);
  }
}
