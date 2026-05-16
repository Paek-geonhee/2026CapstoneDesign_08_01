import cupy as cp

# CuPy가 인식하는 CUDA 정보 출력
print("CUDA Runtime Version:", cp.cuda.runtime.runtimeGetVersion())
print("CUDA Driver Version: ", cp.cuda.runtime.driverGetVersion())

# 간단한 행렬 연산 테스트
x = cp.array([1, 2, 3])
print("GPU 연산 결과:", x * 2)

import time
import numpy as np
from AppearanceManifold.MDS import *

# 테스트용 가상 데이터 (N=5000)
N = 5000
D_test = np.random.rand(N, N)
D_test = (D_test + D_test.T) # 대칭 행렬 생성

# CPU 시간 측정
start = time.time()
Z_cpu = Get_MDS_graph_CPU(D_test, dim=3)
print(f"CPU 소요 시간: {time.time() - start:.4f} sec")

# GPU 시간 측정 (CuPy 설치 가정)
try:
    import cupy as cp
    # 처음 호출 시 CUDA 컨텍스트 초기화 시간이 걸리므로 한 번 실행 후 측정
    _ = Get_MDS_graph_GPU(D_test[:100, :100], dim=3) 
    
    start = time.time()
    Z_gpu = Get_MDS_graph_GPU(D_test, dim=3)
    print(f"GPU 소요 시간: {time.time() - start:.4f} sec")
except ImportError:
    print("CuPy가 설치되지 않았습니다.")