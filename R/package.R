
#' Portable File Locking
#'
#' Place an exclusive or shared lock on a file. It uses `LockFile` on
#' Windows and `fcntl` locks on Unix-like systems.
#'
#' @docType package
#' @useDynLib filelock, .registration = TRUE, .fixes = "c_"
#' @name filelock
NULL

#' @export

lock <- function(path, exclusive = TRUE, timeout = Inf) {

  stopifnot(is_string(path))
  stopifnot(is_flag(exclusive))
  stopifnot(is_timeout(timeout))

  ## Inf if encoded as -1 in our C code
  if (timeout == Inf) timeout <- -1L

  structure(
    .Call(c_filelock_lock, normalizePath(path, mustWork = FALSE), exclusive,
          as.integer(timeout)),
    class = "filelock_lock"
  )
}

#' @export

unlock <- function(lock) {
  .Call(c_filelock_unlock, lock)
}

#' @export

print.filelock_lock <- function(x, ...) {
  unlocked <- .Call(c_filelock_is_unlocked, x)
  cat(
    if (unlocked) "Unlocked lock on " else "Lock on ",
    sQuote(x[[2]]), "\n",
    sep = ""
  )
  invisible(x)
}
