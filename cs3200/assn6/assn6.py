import numpy as np
import time
from matrix_direct import *
from matrix_dot import *
from matrix_outer import *
from matrix_saxpy import *
from matrix_vector import *

def benchmark_matrix_multiplication(size):
    """
    Benchmark multiple matrix multiplication routines for square matrices of a given size.

    Creates two random matrices of size `size x size`, measures execution time for
    MatMatDirect, MatMatDot, MatMatOuter, MatMatSaxpy, and MatMatVec, and prints the timings.
    """
    m = n = p = size

    print(f"\nMatrix size: {size} x {size}")

    A = np.random.rand(m, p)
    B = np.random.rand(p, n)

    # Matrix Dot
    start = time.time()
    MatMatDot(A, B)
    end = time.time()
    t = end - start
    print(f"Dot: {t:.4f} seconds")

    # Matrix Outer
    start = time.time()
    MatMatOuter(A, B)
    end = time.time()
    t = end - start
    print(f"Outer: {t:.4f} seconds")

    # Matrix SAXPY
    start = time.time()
    MatMatSaxpy(A, B)
    end = time.time()
    t = end - start
    print(f"Saxpy: {t:.4f} seconds")

    # Matrix Vector
    start = time.time()
    MatMatVec(A, B)
    end = time.time()
    t = end - start
    print(f"Vec: {t:.4f} seconds")

    # Matrix Direct
    start = time.time()
    MatMatDirect(A, B)
    end = time.time()
    t = end - start
    print(f"Direct: {t:.4f} seconds")


if __name__ == "__main__":
    sizes = [512, 1024, 4096, 8192]
    for size in sizes:
        benchmark_matrix_multiplication(size)