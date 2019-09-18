
context("filelock")

test_that("can create a shared lock", {

  tmp <- tempfile()
  expect_silent({
    lck <- lock(tmp, exclusive = FALSE)
    unlock(lck)
  })
})

test_that("can create an exclusive lock", {

  tmp <- tempfile()
  expect_silent({
    lck <- lock(tmp, exclusive = TRUE)
    unlock(lck)
  })
})

test_that("an exclusive lock really locks", {

  tmp <- tempfile()
  lck <- lock(tmp, exclusive = TRUE)

  res <- callr::r_safe(
    function(path) filelock::lock(path, timeout = 0),
    list(path = tmp),
    timeout = 3,
    spinner = FALSE
  )

  expect_null(res)
  unlock(lck)
})

test_that("can release a lock", {

  tmp <- tempfile()
  lck <- lock(tmp, exclusive = TRUE)

  res <- callr::r_safe(
    function(path) filelock::lock(path, timeout = 0),
    list(path = tmp),
    timeout = 3,
    spinner = FALSE
  )

  expect_null(res)
  unlock(lck)

  res <- callr::r_safe(
    function(path) filelock::lock(path, timeout = 0),
    list(path = tmp),
    timeout = 3,
    spinner = FALSE
  )

  ## By the time it gets here, it will be unlocked, because it is
  ## an external pointer, so we cannot save it to file, and the child
  ## process finishes, anyway.
  expect_equal(class(res), "filelock_lock")
})

test_that("printing the lock", {

  tmp <- tempfile()
  lck <- lock(tmp, exclusive = TRUE)

  expect_output(print(lck), "Lock on")
  expect_output(print(lck), basename(normalizePath(tmp)), fixed = TRUE)

  unlock(lck)
  expect_output(print(lck), "Unlocked lock on")
  expect_output(print(lck), basename(normalizePath(tmp)), fixed = TRUE)
})

test_that("finalizer works", {

  tmp <- tempfile()
  lck <- lock(tmp, exclusive = TRUE)

  rm(lck)
  gc()

  res <- callr::r_safe(
    function(path) filelock::lock(path, timeout = 0),
    list(path = tmp),
    timeout = 3,
    spinner = FALSE
  )

  expect_equal(class(res), "filelock_lock")
})

test_that("timeout", {

  tmp <- tempfile()
  lck <- lock(tmp, exclusive = TRUE)

  tic <- Sys.time()

  res <- callr::r_safe(
    function(path) filelock::lock(path, timeout = 1000),
    list(path = tmp),
    timeout = 3,
    spinner = FALSE
  )

  tac <- Sys.time()

  expect_null(res)
  expect_true(tac - tic >= as.difftime(1, units = "secs"))
})

test_that("timeout 2", {

  ## They don't like tests with timings on CRAN
  skip_on_cran()

  tmp <- tempfile()

  px1_opts <- callr::r_process_options(
    func = function(path) {
      lck <- filelock::lock(path)
      Sys.sleep(1)
      filelock::unlock(lck)
    },
    args = list(path = tmp)
  )
  px1 <- callr::r_process$new(px1_opts)

  px2_opts <- callr::r_process_options(
    func = function(path) filelock::lock(path, timeout = 2000),
    args = list(path = tmp)
  )
  px2 <- callr::r_process$new(px2_opts)

  px2$wait(timeout = 5000)
  if (! px2$is_alive()) {
    res <- px2$get_result()
    expect_equal(class(res), "filelock_lock")
  } else {
    px1$kill()
    px2$kill()
    stop("Process did not finish, something is wrong")
  }
})

test_that("wait forever", {

  ## Thy don't like tests with timings on CRAN
  skip_on_cran()

  tmp <- tempfile()

  px1_opts <- callr::r_process_options(
    func = function(path) {
      lck <- filelock::lock(path)
      Sys.sleep(10)
    },
    args = list(path = tmp)
  )
  px1 <- callr::r_process$new(px1_opts)

  px2_opts <- callr::r_process_options(
    func = function(path) filelock::lock(path, timeout = Inf),
    args = list(path = tmp)
  )
  px2 <- callr::r_process$new(px2_opts)

  px1$kill()
  px2$wait(timeout = 2000)
  if (!px2$is_alive()) {
     expect_true(px2$get_exit_status() == 0)
  } else {
    px2$kill()
    stop("psx2 still running, something is wrong")
  }

})

test_that("wait forever, lock released", {

  tmp <- tempfile()

  ## This process just finishes normally, and that releases the lock
  px1_opts <- callr::r_process_options(
    func = function(path) {
      lck <- filelock::lock(path)
      Sys.sleep(1)
    },
    args = list(path = tmp)
  )
  px1 <- callr::r_process$new(px1_opts)

  px2_opts <- callr::r_process_options(
    func = function(path) filelock::lock(path, timeout = Inf),
    args = list(path = tmp)
  )
  px2 <- callr::r_process$new(px2_opts)

  px2$wait(timeout = 3000)
  if (! px2$is_alive()) {
    res <- px2$get_result()
    expect_equal(class(res), "filelock_lock")
  } else {
    px1$kill()
    px2$kill()
    stop("Process did not finish, something is wrong")
  }
})

test_that("locking the same file twice", {
  tmp <- tempfile()

  expect_silent({
    lck <- lock(tmp, exclusive = TRUE)
  })

  expect_silent({
    lck2 <- lock(tmp, exclusive = TRUE)
  })

  expect_identical(lck, lck2)

  unlock(lck)
  unlock(lck2)
  expect_identical(lck, lck2)
})

test_that("lock reference counting", {
  tmp <- tempfile()

  ## Two locks of the same kind
  expect_silent({
    lck <- lock(tmp, exclusive = TRUE)
    lck2 <- lock(tmp, exclusive = TRUE)
    unlock(lck)
  })

  ## File is still locked
  res <- callr::r_safe(
    function(path) filelock::lock(path, timeout = 0),
    list(path = tmp),
    timeout = 3,
    spinner = FALSE
  )
  expect_null(res)

  ## Now it is unlocked
  unlock(lck2)
  res <- callr::r_safe(
    function(path) filelock::lock(path, timeout = 0),
    list(path = tmp),
    timeout = 3,
    spinner = FALSE
  )
  expect_equal(class(res), "filelock_lock")

  ## Relock
  expect_silent({
    lck3 <- lock(tmp, exclusive = TRUE)
  })

  ## Now it is locked again
  res <- callr::r_safe(
    function(path) filelock::lock(path, timeout = 0),
    list(path = tmp),
    timeout = 3,
    spinner = FALSE
  )
  expect_null(res)

  unlock(lck3)
})

test_that("Multiple locks", {

  tmp <- tempfile()
  lck <- lock(tmp, exclusive = TRUE)
  lck2 <- lock(tmp, exclusive = TRUE)
  unlock(lck)

  expect_output(print(lck), "Unlocked lock")
  expect_output(print(lck2), "^Lock")
})

test_that("Relocking does not affect unlocked locks", {
  tmp <- tempfile()


  lck <- lock(tmp, exclusive = TRUE)
  lck2 <- lock(tmp, exclusive = TRUE)
  unlock(lck)

  ## Relock
  lck3 <- lock(tmp, exclusive = TRUE)

  expect_output(print(lck), "Unlocked lock")
  expect_output(print(lck2), "^Lock")
  expect_output(print(lck3), "^Lock")

  unlock(lck2)
  unlock(lck3)
})

test_that("Multiple, incompatible lock types", {

  tmp <- tempfile()
  lck <- lock(tmp, exclusive = TRUE)
  expect_error(lock(tmp, exclusive = FALSE))
  unlock(lck)

  lck <- lock(tmp, exclusive = FALSE)
  expect_error(lock(tmp, exclusive = TRUE))
  unlock(lck)
})

test_that("UTF-8 filenames", {

  tmp <- paste(tempfile(), "-\u00fc.lock")

  ## We need to test it the file system supports UTF-8/Unicode file names
  good <- tryCatch(
    {
      cat("hello\n", file = tmp)
      if (readLines(tmp) != "hello") stop("Not good")
      unlink(tmp)
      TRUE
    },
    error = function(e) FALSE
  )
  if (identical(good, FALSE)) skip("FS does not support Unicode file names")

  expect_silent(l <- lock(tmp))
  expect_equal(Encoding(l[[2]]), "UTF-8")
  expect_silent(unlock(l))
})

## This used to fail on Windows
test_that("non-exclusive lock with timeout", {
  lockfile <- tempfile()
  l <- lock(lockfile, exclusive = FALSE, timeout = 1000)
  expect_s3_class(l, "filelock_lock")
  expect_true(unlock(l))
})
