
#include <R.h>
#include <Rinternals.h>

SEXP filelock_lock(SEXP path, SEXP exclusive, SEXP timeout) {
  return R_NilValue;
}

SEXP filelock_unlock(SEXP lock) {
  return R_NilValue;
}
