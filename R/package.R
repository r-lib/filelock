
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

  .Call(c_filelock_lock, normalizePath(path, mustWork = FALSE), exclusive,
        as.integer(timeout))
}

#' @export

unlock <- function(lock) {
  .Call(c_filelock_unlock, lock)
}
