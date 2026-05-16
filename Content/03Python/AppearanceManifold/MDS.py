import numpy as np
from scipy.sparse.linalg import eigsh
import matplotlib.pyplot as plt

import cupy as cp

def Get_MDS_graph(D, dim=3, mode ="normal"):

    """
    Params
    ----------
    D               : np.array(N,7)
    dim             : int

    return
    Z               : np.array(N,3)
    ----------
    """

    if mode=="normal":
        return Get_MDS_graph_CPU(D,dim)

    if mode=="gpu":
        return Get_MDS_graph_GPU(D,dim)


def Get_MDS_graph_GPU(D_cpu, dim=3):
    # 1. 데이터를 GPU로 전송
    D = cp.array(D_cpu)
    N = D.shape[0]

    # 2. 전처리 (무한대 값 처리)
    finite_mask = cp.isfinite(D)
    if not cp.all(finite_mask):
        max_val = cp.max(D[finite_mask])
        D[~finite_mask] = max_val

    # 3. Double Centering (H @ B @ H 최적화)
    # H = I - 1/N 연산을 직접 행렬 곱셈하는 대신, 산술적으로 처리하는 것이 메모리에 효율적입니다.
    D2 = D**2
    row_mean = cp.mean(D2, axis=1, keepdims=True)
    col_mean = cp.mean(D2, axis=0, keepdims=True)
    grand_mean = cp.mean(D2)
    
    # B = -0.5 * (D^2 - row_mean - col_mean + grand_mean)
    B = -0.5 * (D2 - row_mean - col_mean + grand_mean)

    # 4. 수치적 안정성을 위한 대칭화
    B = (B + B.T) * 0.5

    # 5. 고유값 분해 (GPU 가속)
    # eigh는 대칭 행렬에 최적화되어 있습니다.
    evals, evecs = cp.linalg.eigh(B)

    # 6. 상위 k개 선택 (eigh는 오름차순으로 정렬됨)
    idx = cp.argsort(evals)[::-1][:dim]
    evals = evals[idx]
    evecs = evecs[:, idx]

    # 7. 좌표 계산
    # 음수 고유값은 0으로 클리핑 (수치 에러 방지)
    Z = evecs * cp.sqrt(cp.maximum(evals, 0))

    # 8. 결과를 CPU로 반환
    return Z.get()

def Get_MDS_graph_CPU(D, dim=3):

    """
    Params
    ----------
    D               : np.array(N,7)
    dim             : int

    return
    Z               : np.array(N,3)
    ----------
    """

    N = D.shape[0]

    D = D.copy()

    finite_mask = np.isfinite(D)

    D[~finite_mask] = np.max(D[finite_mask])

    H = np.eye(N) - np.ones((N, N)) / N

    B = -0.5 * H @ (D**2) @ H

    B = np.nan_to_num(B)

    B = (B + B.T) * 0.5

    evals, evecs = eigsh(B,k=dim,which='LA')

    idx = np.argsort(evals)[::-1]

    evals = evals[idx]
    evecs = evecs[:, idx]

    Z = (evecs * np.sqrt(np.maximum(evals, 0)))

    return Z



def visualize_MDS_Graph(Z, data_7d):
    """
    Params
    ----------
    Z               : np.array(N,3)
    data_7d         : np.array(N,7)
    """
     

    roughness = data_7d[:, 6]
    colors = data_7d[:, 0:3]

    fig = plt.figure()
    ax = fig.add_subplot(111, projection='3d')

    ax.scatter(Z[:, 0], Z[:, 1], Z[:, 2], c=colors, s=1)

    plt.show()