from numpy import *

# C = MatMatDirect(A,B)
# This is what Matlab uses to compute the matrix product C = A*B

def MatMatDirect(A,B):
  
  (m,p) = A.shape
  (p,n) = B.shape

  C = zeros((m,n))
  for j in range(n):
    for i in range(m):
      for k in range(p):
        C[i,j] = C[i,j] + A[i,k] * B[k,j]
        
  return C