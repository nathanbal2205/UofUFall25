from numpy import *

# C = MatMatOuter(A,B)
# This computes the matrix-matrix product C = A*B (via outer products) where
# A is an m-by-p matrix, B is a p-by-n matrix.

def MatMatOuter(A, B):

  (m,p) = A.shape
  (p,n) = B.shape

  C = zeros((m,n))
  for k in range(p):
    C = C + outer(A[:,k],B[k,:])
  
  return C