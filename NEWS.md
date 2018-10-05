
# 1.0.1.9000

* `lock()` now removes the timer on Unix, to avoid crashes in
  non-interactive R sessions, when a SIGALRM is delivered after the process
  acquired the lock.

# 1.0.1

First public release.
