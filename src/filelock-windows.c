
#include <R.h>
#include <Rinternals.h>

#include <windows.h>

void filelock_error(const char *str, DWORD errorcode) {
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

void filelock_finalizer(SEXP x) {
  HANDLE ptr = (HANDLE) R_ExternalPtrAddr(x);
  OVERLAPPED ov = {0};
  if (!ptr) return;
  UnlockFileEx(ptr, 0, 1, 0, &ov); /* ignore errors */
  CloseHandle(ptr);		   /* ignore errors */
  R_ClearExternalPtr(x);
}

int filelock_lock_now(HANDLE file, int exclusive, int *locked) {
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

int filelock_lock_wait(HANDLE file, int exclusive) {
  OVERLAPPED ov = { 0 };
  DWORD dwFlags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
  if (!LockFileEx(file, dwFlags, 0, 1, 0, &ov)) {
    return GetLastError();
  } else {
    return 0;
  }
}

int filelock_lock_timeout(HANDLE file, int exclusive, int timeout, int *locked) {
  OVERLAPPED ov = { 0 };
  DWORD dwFlags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
  BOOL res;

  ov.hEvent = CreateEvent(NULL, 0, 0, NULL);

  res = LockFileEx(file, dwFlags, 0, 1, 0, &ov);
  if (!res) {
    DWORD error = GetLastError();
    DWORD wres;
    if (error != ERROR_IO_PENDING) {
      CloseHandle(ov.hEvent);
      filelock_error("Locking file: ", error);
    }
    wres = WaitForSingleObject(ov.hEvent, timeout);
    if (wres == WAIT_TIMEOUT) {
      *locked = 0;
    } else if (wres == WAIT_OBJECT_0) {
      *locked = 1;
    } else {
      CloseHandle(ov.hEvent);
      filelock_error("Locking file (timeout): ", GetLastError());
    }
  }

  CloseHandle(ov.hEvent);
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
    filelock_error("Opening file: ", GetLastError());
  }

  /* Give it a try, fail immediately */
  if (c_timeout == 0) {
    ret = filelock_lock_now(filehandle, c_exclusive, &locked);

  /* Wait indefintely */
  } else if (c_timeout == -1) {
    ret = filelock_lock_wait(filehandle, c_exclusive);

  /* Finite timeout */
  } else {
    ret = filelock_lock_timeout(filehandle, c_exclusive, c_timeout, &locked);
  }

  if (ret) {
    CloseHandle(filehandle);
    filelock_error("Lock file: ", ret);
  }
  if (!locked) return R_NilValue;

  ptr = PROTECT(R_MakeExternalPtr(filehandle, R_NilValue, R_NilValue));
  R_RegisterCFinalizerEx(ptr, filelock_finalizer, 0);

  result = PROTECT(allocVector(VECSXP, 2));
  SET_VECTOR_ELT(result, 0, ptr);
  SET_VECTOR_ELT(result, 1, path);

  UNPROTECT(2);
  return ptr;
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
