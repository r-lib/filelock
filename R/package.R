
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
  assert_that(is_string(path))
  assert_that(is_flag(exclusive))
  assert_that(is_timeout(timeout))
  .Call(c_filelock_lock, normalizePath(path), exclusive, timeout)
}

#' @export

unlock <- function(path) {
  assert_that(is_string(path))
  .Call("c_filelock_unlock", normalizePath(path))
}
