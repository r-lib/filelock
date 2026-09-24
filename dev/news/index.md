# Changelog

## filelock (development version)

- Error messages now include the path of the lock file when locking
  fails, and follow the tidyverse error style, e.g. “Can’t open lock
  file `<path>`: Permission denied.”
  ([\#30](https://github.com/r-lib/filelock/issues/30)).

## filelock 1.0.3

CRAN release: 2023-12-11

- No user visible changes.

## filelock 1.0.2

CRAN release: 2018-10-05

- [`lock()`](https://r-lib.github.io/filelock/dev/reference/lock.md) now
  removes the timer on Unix, to avoid undefined behavior in
  non-interactive R sessions, when a SIGALRM is delivered after the
  process acquired the lock.

## filelock 1.0.1

CRAN release: 2018-02-07

First public release.
