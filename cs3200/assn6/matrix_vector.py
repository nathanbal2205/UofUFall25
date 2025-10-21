from numpy import *

# C = MatMatVec(A,B)
# This computes the matrix-matrix product C = A*B (via matrix-vector products) 
# where A is an m-by-p matrix, B is a p-by-n matrix. 

def MatMatVec(A,B):

  (m,p) = A.shape
  (p,n) = B.shape
  
  C = zeros((m,n))
  for j in range(n):
    C[:,j] = C[:,j] + dot(A, B[:,j])
    
  return C