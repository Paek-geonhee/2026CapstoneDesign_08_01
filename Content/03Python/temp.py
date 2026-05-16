import os
import sys

# CUDA 13.2 경로 확인 후 수정
cuda_bin_path = r'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2\bin'

if os.path.exists(cuda_bin_path):
    os.add_dll_directory(cuda_bin_path)

import cupy as cp
import cupyx.scipy.sparse.csgraph as csg

# 이제 확인해 보세요
print("Dijkstra available:", "dijkstra" in dir(csg))