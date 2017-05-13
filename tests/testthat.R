
if (Sys.getenv("NOT_CRAN", "") != "") {
  library(testthat)
  library(filelock)
  test_check("filelock")
}
