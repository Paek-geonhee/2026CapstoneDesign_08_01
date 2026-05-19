from scipy.sparse import csr_matrix
import numpy as np
from scipy.sparse.csgraph import dijkstra


import cugraph
import cudf
import cupy as cp

def Get_Geodesic_Distance_Matrix(edge_src, edge_dst, edge_weight, N):

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

    rows = np.concatenate([edge_src, edge_dst]).astype(np.float32)

    cols = np.concatenate([edge_dst, edge_src]).astype(np.float32)

    data = np.concatenate([edge_weight, edge_weight]).astype(np.float32)



    adj = csr_matrix((data, (rows, cols)), shape=(N, N))



    geodesic = dijkstra(adj, directed=False)

    return geodesic


def Get_Geodesic_Distance_Matrix_GPU(
    edge_src,
    edge_dst,
    edge_weight,
    N
):

    """
    GPU accelerated geodesic distance matrix

    Parameters
    ----------
    edge_src : (E,)
    edge_dst : (E,)
    edge_weight : (E,)
    N : int

    Return
    ------
    geodesic : np.array(N,N)
    """

    # =====================================================
    # 1. bidirectional graph
    # =====================================================

    src = np.concatenate([edge_src, edge_dst]).astype(np.int32)
    dst = np.concatenate([edge_dst, edge_src]).astype(np.int32)
    weight = np.concatenate([edge_weight, edge_weight]).astype(np.float32)

    # =====================================================
    # 2. cudf graph dataframe
    # =====================================================

    gdf = cudf.DataFrame({
        "src": src,
        "dst": dst,
        "weight": weight
    })

    # =====================================================
    # 3. create graph
    # =====================================================

    G = cugraph.Graph(directed=False)

    G.from_cudf_edgelist(
        gdf,
        source="src",
        destination="dst",
        edge_attr="weight"
    )

    # =====================================================
    # 4. all-pairs shortest path
    # =====================================================

    geodesic_gpu = cp.full((N, N), cp.inf, dtype=cp.float32)
    
    existing_nodes = set(gdf["src"].unique().to_arrow().to_pylist())

    for source in range(N):
        if source not in existing_nodes:
            continue

        dist_df = cugraph.sssp(G, source=source)

        # 🔥 핵심 수정: .to_cupy() 대신 cp.from_dlpack()을 사용하여 Numba 버그를 우회합니다.
        # cuDF DataFrame/Series는 __dlpack__을 지원하므로 아래와 같이 바로 변환이 가능합니다.
        vertices = cp.from_dlpack(dist_df["vertex"]).astype(cp.int32)
        distances = cp.from_dlpack(dist_df["distance"])

        # 무작위 순서의 vertex라도 인덱스에 맞게 정확히 들어갑니다.
        geodesic_gpu[source, vertices] = distances

    return geodesic_gpu.get()
