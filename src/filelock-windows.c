
#include <R.h>
#include <Rinternals.h>

#include <windows.h>

#define FILELOCK_INTERRUPT_INTERVAL 200

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

void filelock__finalizer(SEXP x) {
  HANDLE ptr = (HANDLE) R_ExternalPtrAddr(x);
  OVERLAPPED ov = {0};
  if (!ptr) return;
  UnlockFileEx(ptr, 0, 1, 0, &ov); /* ignore errors */
  CloseHandle(ptr);		   /* ignore errors */
  R_ClearExternalPtr(x);
}

int filelock__lock_now(HANDLE file, int exclusive, int *locked) {
  OVERLAPPED ov = { 0 };
  DWORD dwFlags = LOCKFILE_FAIL_IMMEDIATELY;
  if (exclusive) dwFlags |= LOCKFILE_EXCLUSIVE_LOCK;
  if (! LockFileEx(file, dwFlags, 0, 1, 0, &ov)) {
    DWORD error = GetLastError();
    if (error == ERROR_LOCK_VIOLATION) {
      *locked = 0;
      return 0;
    } else {
      return error;
    }
  } else {
    *locked = 1;
    return 0;
  }
}

int filelock__lock_wait(HANDLE file, int exclusive) {
  OVERLAPPED ov = { 0 };
  DWORD dwFlags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
  BOOL res;

  while (1) {
    ov.hEvent = CreateEvent(NULL, 0, 0, NULL);
    res = LockFileEx(file, dwFlags, 0, 1, 0, &ov);
    if (!res) {
      DWORD error = GetLastError();
      DWORD wres;
      if (error != ERROR_IO_PENDING) filelock__error("Locking file: ", error);

      wres = WaitForSingleObject(ov.hEvent, FILELOCK_INTERRUPT_INTERVAL);
      CloseHandle(ov.hEvent);
      if (wres == WAIT_TIMEOUT) {
	/* we'll try again */
      } else if (wres == WAIT_OBJECT_0) {
	return 0;
      } else {
	filelock__error("Locking file (timeout): ", GetLastError());
      }
    } else {
      return 0;
    }

    /* Check for interrupt and try again */
    R_CheckUserInterrupt();
  }

  return 0;
}

int filelock__lock_timeout(HANDLE file, int exclusive, int timeout,
			   int *locked) {
  OVERLAPPED ov = { 0 };
  DWORD dwFlags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
  BOOL res;
  int timeleft = timeout;

  /* This is the default, a timeout */
  *locked = 0;

  while (timeleft > 0) {

    int waitnow = timeleft < FILELOCK_INTERRUPT_INTERVAL ? timeleft :
      FILELOCK_INTERRUPT_INTERVAL;

    ov.hEvent = CreateEvent(NULL, 0, 0, NULL);

    res = LockFileEx(file, dwFlags, 0, 1, 0, &ov);
    if (!res) {
      DWORD error = GetLastError();
      DWORD wres;
      if (error != ERROR_IO_PENDING) filelock__error("Locking file: ", error);

      wres = WaitForSingleObject(ov.hEvent, waitnow);
      CloseHandle(ov.hEvent);
      if (wres == WAIT_TIMEOUT) {
	/* we'll try again */
      } else if (wres == WAIT_OBJECT_0) {
	*locked = 1;
	return 0;
      } else {
	filelock__error("Locking file (timeout): ", GetLastError());
      }
    } else {
      *locked = 1;
      return 0;
    }

    /* Check for interrupt and try again */
    R_CheckUserInterrupt();
    timeleft -= FILELOCK_INTERRUPT_INTERVAL;
  }

  return 0;
}

SEXP filelock_lock(SEXP path, SEXP exclusive, SEXP timeout) {
  const char *c_path = CHAR(STRING_ELT(path, 0));
  int c_exclusive = LOGICAL(exclusive)[0];
  int c_timeout = INTEGER(timeout)[0];
  SEXP ptr, result;
  int ret, locked = 1;		/* assume the best :) */

  /* Need overlapped I/O for timeouts */
  DWORD attr = c_timeout > 0 ? FILE_FLAG_OVERLAPPED : FILE_ATTRIBUTE_NORMAL;

  HANDLE filehandle = CreateFile(
    /* lpFilename = */            c_path,
    /* dwDesiredAccess = */       GENERIC_READ | GENERIC_WRITE,
    /* dwShareMode = */           FILE_SHARE_READ | FILE_SHARE_WRITE,
    /* lpSecurityAttributes = */  NULL,
    /* dwCreationDisposition = */ OPEN_ALWAYS,
    /* dwFlagsAndAttributes = */  attr,
    /* hTemplateFile = */         NULL);

  if (filehandle == INVALID_HANDLE_VALUE) {
    filelock__error("Opening file: ", GetLastError());
  }

  /* We create the result object here, so that the finalizer
     will close the file handle on error or interrupt. */

  ptr = PROTECT(R_MakeExternalPtr(filehandle, R_NilValue, R_NilValue));
  R_RegisterCFinalizerEx(ptr, filelock__finalizer, 0);

  result = PROTECT(allocVector(VECSXP, 2));
  SET_VECTOR_ELT(result, 0, ptr);
  SET_VECTOR_ELT(result, 1, path);

  /* Give it a try, fail immediately */
  if (c_timeout == 0) {
    ret = filelock__lock_now(filehandle, c_exclusive, &locked);

  /* Wait indefintely */
  } else if (c_timeout == -1) {
    ret = filelock__lock_wait(filehandle, c_exclusive);

  /* Finite timeout */
  } else {
    ret = filelock__lock_timeout(filehandle, c_exclusive,
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
  HANDLE ptr = (HANDLE) R_ExternalPtrAddr(VECTOR_ELT(lock, 0));

  if (ptr) {
    OVERLAPPED ov = {0};
    UnlockFileEx(ptr, 0, 1, 0, &ov); /* ignore errors */
    CloseHandle(ptr);		     /* ignore errors */
    R_ClearExternalPtr(VECTOR_ELT(lock, 0));
  }

  return ScalarLogical(1);
}
