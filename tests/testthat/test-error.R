
test_that("expect_error works on gi actions", {
  a = try(stop("Hallo, welt", call. = FALSE))
  expect_s3_class(a, "try-error")
})
