# Multiple, incompatible lock types

    Code
      lock(tmp, exclusive = FALSE)
    Condition
      Error in `lock()`:
      ! File already has an exclusive lock

---

    Code
      lock(tmp, exclusive = TRUE)
    Condition
      Error in `lock()`:
      ! File already has a shared lock

# unlock() needs lock object

    Code
      unlock(1)
    Condition
      Error in `unlock()`:
      ! `unlock()` needs a lock object, not a file name

