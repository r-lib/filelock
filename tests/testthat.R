
if (Sys.getenv("NOT_CRAN", "") != "") {
  Sys.setenv(R_TESTS = "")
  library(testthat)
  library(filelock)
  test_check("filelock")
}
