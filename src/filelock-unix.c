
#include <R.h>
#include <Rinternals.h>

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <string.h>

static int filelock_dummy_object = 0;

struct sigaction filelock_old_sa;

void filelock_finalizer(SEXP x) {
  void *ptr = R_ExternalPtrAddr(x);
  SEXP des;

  if (!ptr) return;

  des = R_ExternalPtrTag(x);
  close(INTEGER(des)[0]);

  R_ClearExternalPtr(x);
}

void filelock_alarm_callback (int signum) {
  /* Restore signal handler */
  sigaction(SIGALRM, &filelock_old_sa, 0);
}

SEXP filelock_lock(SEXP path, SEXP exclusive, SEXP timeout) {
  struct flock lck;
  const char *c_path = CHAR(STRING_ELT(path, 0));
  int c_exclusive = LOGICAL(exclusive)[0];
  int c_timeout = INTEGER(timeout)[0];
  int filedes, ret;
  SEXP ptr;

  lck.l_type = c_exclusive ? F_WRLCK : F_RDLCK;
  lck.l_whence = SEEK_SET;
  lck.l_start = 0;
  lck.l_len = 0;

  filedes = open(c_path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  if (filedes == -1) error("Cannot open lock file: %s", strerror(errno));

  /* One shot only, do not block if cannot lock */
  if (c_timeout == 0) {
    ret = fcntl(filedes, F_SETLK, &lck);
    if (ret == -1) {
      close(filedes);
      if (errno == EAGAIN || errno == EACCES) return R_NilValue;
      error("Cannot lock file: '%s': %s", c_path, strerror(errno));
    }

  /* Wait indefinitely */
  } else if (c_timeout == -1) {
    ret = fcntl(filedes, F_SETLKW, &lck);

    /* Handle timeout and errors */
    if (ret == -1) {
      close(filedes);
      error("Cannot lock file: '%s': %s", c_path, strerror(errno));
    }

  /* Finite timeout, we need a signal for this */
  } else {
    struct itimerval timer;
    struct sigaction sa;
    timer.it_value.tv_sec = c_timeout / 1000;
    timer.it_value.tv_usec = (c_timeout % 1000) * 1000;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;

    memset(&sa, 0, sizeof (sa));
    sa.sa_handler = &filelock_alarm_callback;
    sigaction(SIGALRM, &sa, &filelock_old_sa);

    setitimer(ITIMER_REAL, &timer, 0);
    ret = fcntl(filedes, F_SETLKW, &lck);

    /* Handle timeout and errors */
    if (ret == -1) {
      close(filedes);
      if (errno == EINTR) return R_NilValue;
      error("Cannot lock file: '%s': %s", c_path, strerror(errno));
    }
  }

  ptr = PROTECT(R_MakeExternalPtr(&filelock_dummy_object,
				  ScalarInteger(filedes), R_NilValue));
  R_RegisterCFinalizerEx(ptr, filelock_finalizer, 0);

  UNPROTECT(1);
  return ptr;
}

SEXP filelock_unlock(SEXP lock) {
  void *ptr = R_ExternalPtrAddr(lock);

  if (ptr) {
    SEXP des = R_ExternalPtrTag(lock);
    close(INTEGER(des)[0]);
    R_ClearExternalPtr(lock);
  }

  return ScalarLogical(1);
}
