from numpy import *

# C = MatMatSaxpy(A,B)
# Computes matrix multiplication by noting that the jth column of C equals
# A times the jth column of B.  This known as saxpy operation.

def MatMatSaxpy(A,B):

  (m,p) = A.shape
  (p,n) = B.shape

  C = zeros((m,n))
  for j in range(n):
    for k in range(m):
      C[:,j] = C[:,j] + A[:,k] * B[k,j]
      
  return C