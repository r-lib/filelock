
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
    timeout = 1,
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
    timeout = 1,
    spinner = FALSE
  )

  expect_null(res)
  unlock(lck)

  res <- callr::r_safe(
    function(path) filelock::lock(path, timeout = 0),
    list(path = tmp),
    timeout = 1,
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
    timeout = 1,
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
    timeout = 2,
    spinner = FALSE
  )

  tac <- Sys.time()

  expect_null(res)
  expect_true(tac - tic >= as.difftime(1, units = "secs"))
})

test_that("timeout 2", {

  ## Thy don't like tests with timings on CRAN
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
    func = function(path) filelock::lock(path, timeout = 3000),
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

  px2$wait(timeout = 2000)
  if (px2$is_alive()) {
    px1$kill()
    px2$kill()

    ## Killed process has exit status 1 on windows
    if (.Platform$OS.type == "windows") {
      expect_true(px2$get_exit_status() == 1)
    } else {
      expect_true(px2$get_exit_status() < 0)
    }
  } else {
    px1$kill()
    px2$kill()
    stop("Process already done, something is wrong")
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
