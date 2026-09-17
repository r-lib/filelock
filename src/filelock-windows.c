
#include <R.h>
#include <Rinternals.h>

#include <windows.h>

#include "filelock.h"

#define FILELOCK_INTERRUPT_INTERVAL 200

int filelock__utf8_to_utf16_alloc(const char* s, WCHAR** ws_ptr);

void filelock__check_interrupt_fn(void *dummy) {
  R_CheckUserInterrupt();
}

int filelock__is_interrupt_pending(void) {
  return !(R_ToplevelExec(filelock__check_interrupt_fn, NULL));
}

void filelock__error(const char *str, const char *path, DWORD errorcode) {
  LPVOID lpMsgBuf;
  char *msg;
  size_t len;

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

  /* Strip the trailing whitespace (CRLF) of the system error message */
  len = strlen(msg);
  while (len > 0 && (msg[len - 1] == '\r' || msg[len - 1] == '\n' ||
		     msg[len - 1] == ' ')) {
    msg[--len] = '\0';
  }

  error("Can't %s `%s`: %s (Windows error %d).",
	str, path, msg, (int) errorcode);
}

void filelock__finalizer(SEXP x) {
  filelock__list_t *ptr = (filelock__list_t*) R_ExternalPtrAddr(x);

  if (!ptr) return;

  ptr->refcount -= 1;
  if (!ptr->refcount) {
    HANDLE *file = &ptr->file;
    OVERLAPPED ov = { 0 };
    UnlockFileEx(*file, 0, 1, 0, &ov); /* ignore errors */
    CloseHandle(*file);		       /* ignore errors */
    filelock__list_remove(ptr->path);
  }

  R_ClearExternalPtr(x);
}

int filelock__lock_now(HANDLE file, int exclusive, int *locked) {
  DWORD dwFlags = LOCKFILE_FAIL_IMMEDIATELY;
  OVERLAPPED ov = { 0 };
  if (exclusive) dwFlags |= LOCKFILE_EXCLUSIVE_LOCK;
  if (! LockFileEx(file, dwFlags, 0, 1, 0, &ov)) {
    DWORD error = GetLastError();
    *locked = 0;
    if (error == ERROR_LOCK_VIOLATION) {
      return 0;
    } else {
      return error;
    }
  } else {
    *locked = 1;
    return 0;
  }
}

int filelock__lock_wait(HANDLE file, int exclusive, const char *path) {
  DWORD dwFlags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
  BOOL res;
  OVERLAPPED ov = { 0 };

  ov.hEvent = CreateEvent(NULL, 0, 0, NULL);
  res = LockFileEx(file, dwFlags, 0, 1, 0, &ov);

  if (!res) {
    DWORD err = GetLastError();
    while (1) {
      DWORD wres;
      if (err != ERROR_IO_PENDING) {
	filelock__error("lock file", path, err);
      }

      wres = WaitForSingleObject(ov.hEvent, FILELOCK_INTERRUPT_INTERVAL);
      if (wres == WAIT_TIMEOUT) {
	/* we'll try again */
      } else if (wres == WAIT_OBJECT_0) {
	CloseHandle(ov.hEvent);
	return 0;
      } else if (wres == WAIT_FAILED) {
	CancelIo(file);
	CloseHandle(ov.hEvent);
	filelock__error("lock file", path, GetLastError());
      }

      /* Check for interrupt and try again */
      if (filelock__is_interrupt_pending()) {
	CancelIo(file);
	CloseHandle(ov.hEvent);
	UnlockFileEx(file, 0, 1, 0, &ov); /* ignore errors */
	CloseHandle(file);		  /* ignore errors */
	error("Locking interrupted for file `%s`.", path);
      }
    }
  }

  CloseHandle(ov.hEvent);
  return 0;
}

int filelock__lock_timeout(HANDLE file, int exclusive, int timeout,
			   int *locked, const char *path) {

  DWORD dwFlags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
  BOOL res;
  int timeleft = timeout;
  OVERLAPPED ov = { 0 };

  /* This is the default, a timeout */
  *locked = 0;

  ov.hEvent = CreateEvent(NULL, 0, 0, NULL);
  res = LockFileEx(file, dwFlags, 0, 1, 0, &ov);

  if (!res) {
    DWORD err = GetLastError();
    while (timeleft > 0) {
      DWORD wres;
      int waitnow;

      if (err != ERROR_IO_PENDING) {
	filelock__error("lock file", path, err);
      }

      waitnow = timeleft < FILELOCK_INTERRUPT_INTERVAL ? timeleft :
	FILELOCK_INTERRUPT_INTERVAL;
      wres = WaitForSingleObject(ov.hEvent, waitnow);
      if (wres == WAIT_TIMEOUT) {
	/* we'll try again */
      } else if (wres == WAIT_OBJECT_0) {
	*locked = 1;
	break;
      } else {
	CancelIo(file);
	CloseHandle(ov.hEvent);
	filelock__error("lock file", path, GetLastError());
      }

      /* Check for interrupt and try again */
      if (filelock__is_interrupt_pending()) {
	CancelIo(file);
	CloseHandle(ov.hEvent);
	UnlockFileEx(file, 0, 1, 0, &ov); /* ignore errors */
	CloseHandle(file);		  /* ignore errors */
	error("Locking interrupted for file `%s`.", path);
      }
      timeleft -= FILELOCK_INTERRUPT_INTERVAL;
    }
  } else {
    *locked = 1;
  }

  CancelIo(file);
  CloseHandle(ov.hEvent);
  return 0;
}

SEXP filelock_lock(SEXP path, SEXP exclusive, SEXP timeout) {
  const char *c_path = CHAR(STRING_ELT(path, 0));
  int c_exclusive = LOGICAL(exclusive)[0];
  int c_timeout = INTEGER(timeout)[0];
  int ret, locked = 1;		/* assume the best :) */
  HANDLE file;
  WCHAR *wpath;

  /* Check if this file was already locked. */
  filelock__list_t *node = filelock__list_find(c_path);
  if (node) {
    if ((c_exclusive && node->exclusive) ||
	(!c_exclusive && !node->exclusive)) {
      return filelock__make_lock_handle(node);
    } else if (c_exclusive) {
      error("File `%s` already has a shared lock.", c_path);
    } else {
      error("File `%s` already has an exclusive lock.", c_path);
    }
  }

  filelock__utf8_to_utf16_alloc(c_path, &wpath);

  file = CreateFileW(
    /* lpFilename = */            wpath,
    /* dwDesiredAccess = */       GENERIC_READ | GENERIC_WRITE,
    /* dwShareMode = */           FILE_SHARE_READ | FILE_SHARE_WRITE,
    /* lpSecurityAttributes = */  NULL,
    /* dwCreationDisposition = */ OPEN_ALWAYS,
    /* dwFlagsAndAttributes = */  FILE_FLAG_OVERLAPPED,
    /* hTemplateFile = */         NULL);

  if (file == INVALID_HANDLE_VALUE) {
    filelock__error("open lock file", c_path, GetLastError());
  }

  /* Give it a try, fail immediately */
  if (c_timeout == 0) {
    ret = filelock__lock_now(file, c_exclusive, &locked);

  /* Wait indefintely */
  } else if (c_timeout == -1) {
    ret = filelock__lock_wait(file, c_exclusive, c_path);

  /* Finite timeout */
  } else {
    ret = filelock__lock_timeout(file, c_exclusive,
				 c_timeout, &locked, c_path);
  }

  if (ret) {
    filelock__error("lock file", c_path, ret);
  }

  if (!locked) {
    return R_NilValue;
  } else {
    return filelock__list_add(c_path, file, c_exclusive);
  }
}

SEXP filelock_unlock(SEXP lock) {
  void *ptr = R_ExternalPtrAddr(VECTOR_ELT(lock, 0));
  const char *c_path;
  filelock__list_t *node;

  if (!ptr) return ScalarLogical(1);

  c_path = CHAR(STRING_ELT(VECTOR_ELT(lock, 1), 0));
  node = filelock__list_find(c_path);

  /* It has to be there.... */
  if (node) {
    node->refcount -= 1;
    if (!node->refcount) {
      const char *c_path = CHAR(STRING_ELT(VECTOR_ELT(lock, 1), 0));
      OVERLAPPED ov = { 0 };
      UnlockFileEx(node->file, 0, 1, 0, &ov); /* ignore errors */
      CloseHandle(node->file);		      /* ignore errors */
      filelock__list_remove(c_path);
    }
  }

  R_ClearExternalPtr(VECTOR_ELT(lock, 0));

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
