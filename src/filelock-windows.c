
#include <R.h>
#include <Rinternals.h>

#include <windows.h>

#define FILELOCK_INTERRUPT_INTERVAL 200

typedef struct {
  HANDLE file;
} filelock__handle_t;

void filelock__check_interrupt_fn(void *dummy) {
  R_CheckUserInterrupt();
}

int filelock__is_interrupt_pending() {
  return !(R_ToplevelExec(filelock__check_interrupt_fn, NULL));
}

void filelock__error(const char *str, DWORD errorcode) {
  LPVOID lpMsgBuf;
  char *msg;

  FormatMessage(
    FORMAT_MESSAGE_ALLOCATE_BUFFER |
    FORMAT_MESSAGE_FROM_SYSTEM |
    FORMAT_MESSAGE_IGNORE_INSERTS,
    NULL,
    errorcode,
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
    (LPTSTR) &lpMsgBuf,
    0, NULL );

  msg = R_alloc(1, strlen(lpMsgBuf) + 1);
  strcpy(msg, lpMsgBuf);
  LocalFree(lpMsgBuf);

  error("Filelock error, %s %s", str, msg);
}

void filelock__destroy_handle(filelock__handle_t *handle) {
  HANDLE *file;
  OVERLAPPED ov = { 0 };

  if (!handle) return;

  file = &handle->file;

  UnlockFileEx(*file, 0, 1, 0, &ov); /* ignore errors */
  CloseHandle(*file);		     /* ignore errors */

  free(handle);
}

void filelock__finalizer(SEXP x) {
  filelock__handle_t *handle = (filelock__handle_t*) R_ExternalPtrAddr(x);
  filelock__destroy_handle(handle);
  R_ClearExternalPtr(x);
}

int filelock__lock_now(filelock__handle_t *handle, int exclusive, int *locked) {
  DWORD dwFlags = LOCKFILE_FAIL_IMMEDIATELY;
  HANDLE *file = &handle->file;
  OVERLAPPED ov = { 0 };
  if (exclusive) dwFlags |= LOCKFILE_EXCLUSIVE_LOCK;
  REprintf("locking now\n");
  if (! LockFileEx(*file, dwFlags, 0, 1, 0, &ov)) {
    DWORD error = GetLastError();
    REprintf("no: %d\n", error);
    *locked = 0;
    if (error == ERROR_LOCK_VIOLATION) {
      return 0;
    } else {
      return error;
    }
  } else {
    REprintf("yes!\n");
    *locked = 1;
    return 0;
  }
}

int filelock__lock_wait(filelock__handle_t *handle, int exclusive) {
  DWORD dwFlags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
  BOOL res;
  HANDLE *file = &handle->file;
  OVERLAPPED ov = { 0 };

  ov.hEvent = CreateEvent(NULL, 0, 0, NULL);
  res = LockFileEx(*file, dwFlags, 0, 1, 0, &ov);

  if (!res) {
    DWORD err = GetLastError();
    while (1) {
      DWORD wres;
      if (err != ERROR_IO_PENDING) filelock__error("Locking file: ", err);

      wres = WaitForSingleObject(ov.hEvent, FILELOCK_INTERRUPT_INTERVAL);
      REprintf("wait: %d\n", wres);
      if (wres == WAIT_TIMEOUT) {
	/* we'll try again */
	REprintf("try again");
      } else if (wres == WAIT_OBJECT_0) {
	CloseHandle(ov.hEvent);
	return 0;
      } else if (wres == WAIT_FAILED) {
	CancelIo(*file);
	CloseHandle(ov.hEvent);
	filelock__error("Locking file (timeout): ", GetLastError());
      }

      /* Check for interrupt and try again */
      if (filelock__is_interrupt_pending()) {
	CancelIo(*file);
	CloseHandle(ov.hEvent);
	filelock__destroy_handle(handle);
	error("Locking interrupted", 1);
      }
    }
  }

  CloseHandle(ov.hEvent);
  return 0;
}

int filelock__lock_timeout(filelock__handle_t *handle,
			   int exclusive, int timeout,
			   int *locked) {

  DWORD dwFlags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
  BOOL res;
  int timeleft = timeout;
  HANDLE *file = &handle->file;
  OVERLAPPED ov = { 0 };

  /* This is the default, a timeout */
  *locked = 0;

  ov.hEvent = CreateEvent(NULL, 0, 0, NULL);
  res = LockFileEx(*file, dwFlags, 0, 1, 0, &ov);

  if (!res) {
    DWORD err = GetLastError();
    while (timeleft > 0) {
      DWORD wres;
      int waitnow;

      if (err != ERROR_IO_PENDING) filelock__error("Locking file: ", err);

      waitnow = timeleft < FILELOCK_INTERRUPT_INTERVAL ? timeleft :
	FILELOCK_INTERRUPT_INTERVAL;
      wres = WaitForSingleObject(ov.hEvent, waitnow);
      if (wres == WAIT_TIMEOUT) {
	/* we'll try again */
      } else if (wres == WAIT_OBJECT_0) {
	*locked = 1;
	break;
      } else {
	CancelIo(*file);
	CloseHandle(ov.hEvent);
	filelock__error("Locking file (timeout): ", GetLastError());
      }

      /* Check for interrupt and try again */
      if (filelock__is_interrupt_pending()) {
	CancelIo(*file);
	CloseHandle(ov.hEvent);
	filelock__destroy_handle(handle);
	error("Locking interrupted");
      }
      timeleft -= FILELOCK_INTERRUPT_INTERVAL;
    }
  }

  CancelIo(*file);
  CloseHandle(ov.hEvent);
  return 0;
}

SEXP filelock_lock(SEXP path, SEXP exclusive, SEXP timeout) {
  const char *c_path = CHAR(STRING_ELT(path, 0));
  int c_exclusive = LOGICAL(exclusive)[0];
  int c_timeout = INTEGER(timeout)[0];
  SEXP ptr, result;
  int ret, locked = 1;		/* assume the best :) */
  filelock__handle_t *handle;

  handle = malloc(sizeof(filelock__handle_t));
  if (!handle) error("Cannot allocate memory for file locking");

  handle->file = CreateFile(
    /* lpFilename = */            c_path,
    /* dwDesiredAccess = */       GENERIC_READ | GENERIC_WRITE,
    /* dwShareMode = */           FILE_SHARE_READ | FILE_SHARE_WRITE,
    /* lpSecurityAttributes = */  NULL,
    /* dwCreationDisposition = */ OPEN_ALWAYS,
    /* dwFlagsAndAttributes = */  FILE_FLAG_OVERLAPPED,
    /* hTemplateFile = */         NULL);

  if (handle->file == INVALID_HANDLE_VALUE) {
    filelock__error("Opening file: ", GetLastError());
  }

  /* We create the result object here, so that the finalizer
     will close the file handle on error or interrupt. */

  ptr = PROTECT(R_MakeExternalPtr(handle, R_NilValue, R_NilValue));
  R_RegisterCFinalizerEx(ptr, filelock__finalizer, 0);

  result = PROTECT(allocVector(VECSXP, 2));
  SET_VECTOR_ELT(result, 0, ptr);
  SET_VECTOR_ELT(result, 1, path);

  /* Give it a try, fail immediately */
  if (c_timeout == 0) {
    ret = filelock__lock_now(handle, c_exclusive, &locked);

  /* Wait indefintely */
  } else if (c_timeout == -1) {
    ret = filelock__lock_wait(handle, c_exclusive);

  /* Finite timeout */
  } else {
    ret = filelock__lock_timeout(handle, c_exclusive,
				 c_timeout, &locked);
  }

  if (ret) {
    filelock__error("Lock file: ", ret);
  }

  if (!locked) {
    UNPROTECT(2);
    return R_NilValue;
  }

  UNPROTECT(2);
  return result;
}

SEXP filelock_unlock(SEXP lock) {
  filelock__handle_t *handle = (HANDLE) R_ExternalPtrAddr(VECTOR_ELT(lock, 0));
  filelock__destroy_handle(handle);
  R_ClearExternalPtr(VECTOR_ELT(lock, 0));
  return ScalarLogical(1);
}
