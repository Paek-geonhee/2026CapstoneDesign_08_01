import numpy as np
from scipy.sparse.linalg import eigsh

import matplotlib.pyplot as plt
import cupy as cp

import cupy as cp

""" 
Description
Manifold Graph와 측지선 거리 행렬 바탕으로 Multidimentional Scaling 적용

측지선 거리값을 최대한 유지한 상태로 저차원 공간으로 축소함.
"""

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

    #if mode=="normal":
    #    return Get_MDS_graph_CPU(D,dim)

    #if mode=="gpu":
    #    return Get_MDS_graph_GPU(D,dim)

    N = D.shape[0]

    D = np.asarray(D, dtype=np.float64)

    finite_mask = np.isfinite(D)

    D = D.copy()

    D[~finite_mask] = np.max(D[finite_mask])

    # =====================================================
    # Double centering WITHOUT H matrix
    # =====================================================

    D2 = D * D

    row_mean = np.mean(D2, axis=1, keepdims=True)

    col_mean = np.mean(D2, axis=0, keepdims=True)

    grand_mean = np.mean(D2)

    B = -0.5 * (D2 - row_mean - col_mean + grand_mean)

    # =====================================================
    # numerical stabilization
    # =====================================================

    np.nan_to_num(B, copy=False)

    B = 0.5 * (B + B.T)

    # =====================================================
    # TOP-K eigensolver
    # =====================================================

    evals, evecs = eigsh(B, k=dim, which='LA')

    idx = np.argsort(evals)[::-1]

    evals = evals[idx]
    evecs = evecs[:, idx]

    Z = evecs * np.sqrt(np.maximum(evals, 0))

    return Z


def Get_MDS_graph_optimized(D, dim=3):
    """
    최적화된 MDS 구현: 메모리 복사 최소화 및 연산 효율화

    -> 딱히 성능이 개선되지 않음. 기존 구현이 이미 상당히 최적화되어 있었음.
    """
    # 1. 입력 복사 최소화 (inplace 연산 활용)
    D = np.ascontiguousarray(D, dtype=np.float64)
    
    # 2. NaN/Inf 처리 (최적화된 방식)
    mask = ~np.isfinite(D)
    if np.any(mask):
        D[mask] = np.max(D[~mask])
    
    # 3. Double Centering 연산 최적화
    # D2를 미리 할당하여 재사용
    D2 = np.square(D, out=D) 
    
    row_mean = np.mean(D2, axis=1, keepdims=True)
    col_mean = np.mean(D2, axis=0, keepdims=True)
    grand_mean = np.mean(D2)
    
    # B 구성: 메모리 효율을 위해 직접 연산
    B = -0.5 * (D2 - row_mean - col_mean + grand_mean)
    
    # 4. 수치 안정화 (대칭성 확보)
    np.nan_to_num(B, copy=False)
    B = 0.5 * (B + B.T)
    
    # 5. Eigsh 최적화
    # 'which=LA'와 함께 'ncv' 파라미터를 조절하여 수렴 속도 향상
    # ncv가 너무 작으면 느리고, 너무 크면 메모리를 많이 먹음
    evals, evecs = eigsh(B, k=dim, which='LA', ncv=min(D.shape[0], max(2*dim+1, 20)))
    
    # 정렬 (eigsh는 기본적으로 오름차순 반환)
    idx = np.argsort(evals)[::-1]
    evals = evals[idx]
    evecs = evecs[:, idx]
    
    # 결과 계산
    Z = evecs * np.sqrt(np.maximum(evals, 0))
    
    return Z


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








# --------------------------------------- deprecated -----------------------------------------

def visualize_MDS_Graph(Z, data_7d):
    """
    Params
    ----------
    Z               : np.array(N,3)
    data_7d         : np.array(N,7)
    """
     
    fig = plt.figure()
    ax = fig.add_subplot(111, projection='3d')

    ax.scatter(Z[:, 0], Z[:, 1], Z[:, 2], s=2)

    plt.show()

def visualize_MDS_Graph_Edges(Z,  edge_src, edge_dst, data_7d):
    """
    Params
    ----------
    Z               : np.array(N,3)
    edge_src        : np.array(N,N)
    edge_dst        : np.array(N,N)
    data_7d         : np.array(N,7)
    """
     
    fig = plt.figure(figsize=(10, 10))
    ax = fig.add_subplot(111, projection='3d')

    # scatter points
    ax.scatter(
        Z[:, 0],
        Z[:, 1],
        Z[:, 2],
        s=2
    )

    # draw graph edges
    for src, dst in zip(edge_src, edge_dst):

        x = [Z[src, 0], Z[dst, 0]]
        y = [Z[src, 1], Z[dst, 1]]
        z = [Z[src, 2], Z[dst, 2]]

        ax.plot(
            x,
            y,
            z,
            alpha=0.25,
            linewidth=0.5
        )

    plt.show()

def visualize_MDS_Graph_Edges_2D(Z,  edge_src, edge_dst, data_7d):
    """
    Params
    ----------
    Z               : np.array(N,3)
    edge_src        : np.array(N,N)
    edge_dst        : np.array(N,N)
    data_7d         : np.array(N,7)
    """
     
    fig = plt.figure(figsize=(10, 10))
    ax = fig.add_subplot(111, projection='3d')

    # scatter points
    ax.scatter(
        Z[:, 0],
        Z[:, 1],
        s=2
    )

    # draw graph edges
    for src, dst in zip(edge_src, edge_dst):

        x = [Z[src, 0], Z[dst, 0]]
        y = [Z[src, 1], Z[dst, 1]]

        ax.plot(
            x,
            y,
            alpha=0.25,
            linewidth=0.5
        )

    plt.show()



def Get_MDS_graph_GPU(D, dim=3):
    """
    GPU Accelerated Classical MDS

    Parameters
    ----------
    D : np.array(N,N)
        Distance matrix

    dim : int
        Output embedding dimension

    Returns
    -------
    Z : np.array(N, dim)
    """

    # -----------------------------
    # CPU -> GPU
    # -----------------------------
    D = cp.asarray(D)

    N = D.shape[0]

    # -----------------------------
    # inf 처리
    # -----------------------------
    finite_mask = cp.isfinite(D)

    max_val = cp.max(D[finite_mask])

    D = D.copy()

    D[~finite_mask] = max_val

    # -----------------------------
    # Centering matrix
    # -----------------------------
    H = cp.eye(N) - cp.ones((N, N)) / N

    # -----------------------------
    # Gram matrix
    # -----------------------------
    D2 = D ** 2

    B = -0.5 * (H @ D2 @ H)

    # -----------------------------
    # Numerical stabilization
    # -----------------------------
    B = cp.nan_to_num(B)

    B = 0.5 * (B + B.T)

    # -----------------------------
    # Dense symmetric eigendecomposition
    # -----------------------------
    evals, evecs = cp.linalg.eigh(B)

    # descending sort
    idx = cp.argsort(evals)[::-1]

    evals = evals[idx][:dim]
    evecs = evecs[:, idx][:, :dim]

    # -----------------------------
    # Embedding
    # -----------------------------
    Z = evecs * cp.sqrt(cp.maximum(evals, 0))

    # GPU -> CPU
    return cp.asnumpy(Z)