from scipy.sparse import csr_matrix as scsr_matrix

import numpy as np
from scipy.sparse.csgraph import dijkstra as sdijkstra

import cupy as cp
from cupyx.scipy.sparse import csr_matrix
import cupyx.scipy.sparse.csgraph as csg


def Get_Geodesic_Distance_Matrix(edge_src, edge_dst, edge_weight, N, mode="normal"):

    """
    Parameters
    ----------
    edge_src : (E,)
    edge_dst : (E,)
    edge_weight : (E,)
    N : int
    mode : str('normal', 'cupy', 'opt')

    Return
    ------
    geodesic : np.array(E,)
    """

    if mode == "normal":
        return Get_Geodesic_Distance_Matrix_normal(edge_src, edge_dst, edge_weight, N)
    
    if mode == "cupy":
        return Get_Geodesic_Distance_Matrix_GPU(edge_src, edge_dst, edge_weight, N)
    
    if mode == "opt":
        return Optimized_Get_Geodesic_CPU(edge_src, edge_dst, edge_weight, N)



def Get_Geodesic_Distance_Matrix_normal(edge_src, edge_dst, edge_weight, N):

    """
    Parameters
    ----------
    edge_src : (E,)
    edge_dst : (E,)
    edge_weight : (E,)
    N : int

    Return
    ------
    geodesic : np.array(E,)
    """

    rows = np.concatenate([edge_src, edge_dst]).astype(cp.int32)
    cols = np.concatenate([edge_dst, edge_src]).astype(cp.int32)
    data = np.concatenate([edge_weight, edge_weight]).astype(cp.float32)

    adj = scsr_matrix((data, (rows, cols)), shape=(int(N), int(N)))

    geodesic = sdijkstra(adj, directed=False)

    return geodesic


def Get_Geodesic_Distance_Matrix_GPU(edge_src, edge_dst, edge_weight, N):
    # 1. 데이터를 GPU 메모리로 전송하며 타입 변환
    src_gpu = cp.array(edge_src, dtype=cp.int32)
    dst_gpu = cp.array(edge_dst, dtype=cp.int32)
    weight_gpu = cp.array(edge_weight, dtype=cp.float32)

    # 2. 양방향 엣지 구성을 위한 결합 (여기서 이미 CuPy 배열이므로 cp.array() 중복 호출 불필요)
    rows = cp.concatenate([src_gpu, dst_gpu])
    cols = cp.concatenate([dst_gpu, src_gpu])
    data = cp.concatenate([weight_gpu, weight_gpu])

    # 3. GPU에서 CSR 행렬 생성 (N은 반드시 파이썬 int형이어야 함)
    adj = csr_matrix((data, (rows, cols)), shape=(int(N), int(N)))

    # 4. GPU 가속 Dijkstra 실행
    # cupyx.scipy.sparse.csgraph.dijkstra를 사용합니다.
    try:
        # 최신 버전에서는 주로 이 경로를 사용합니다.
        geodesic = csg.dijkstra(adj, directed=False)
    except AttributeError:
        # 만약 위에서 에러가 난다면 아래 공용 함수를 사용합니다.
        geodesic = csg.shortest_path(adj, directed=False, method='dijkstra')

    # 5. 결과를 다시 CPU(numpy)로 반환
    return geodesic


def Optimized_Get_Geodesic_CPU(edge_src, edge_dst, edge_weight, N):
    # 인덱스는 정수형으로 처리 (성능 및 메모리 이점)
    rows = np.concatenate([edge_src, edge_dst]).astype(cp.int32)
    cols = np.concatenate([edge_dst, edge_src]).astype(cp.int32)
    data = np.concatenate([edge_weight, edge_weight]).astype(cp.float32)

    rows_gpu = cp.array(rows)
    cols_gpu = cp.array(cols)
    data_gpu = cp.array(data)

    adj = scsr_matrix((data, (rows, cols)), shape=(N, N))
    
    # 팁: 모든 경로가 필요한 게 아니라면 indices=[0, 1, 2...] 형태로 필요한 시작점만 지정 가능
    geodesic = sdijkstra(adj, directed=False, unweighted=False)
    
    return geodesic