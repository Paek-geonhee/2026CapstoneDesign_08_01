import cupy as cp
import cupyx.scipy.sparse.csgraph as csg
from cupyx.scipy.sparse import csr_matrix

# 1. csgraph 기능 확인
print("Available in csg:", [f for f in dir(csg) if not f.startswith('_')])

# 2. linalg(cuSOLVER) 기능 확인
try:
    a = cp.array([[1, 2], [2, 1]], dtype=cp.float32)
    w, v = cp.linalg.eigh(a)
    print("eigh 테스트 성공! 고유값:", w)
except Exception as e:
    print("eigh 테스트 실패:", e)

    