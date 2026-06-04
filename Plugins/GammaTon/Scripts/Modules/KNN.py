from sklearn.neighbors import NearestNeighbors
import numpy as np
from scipy.sparse import csr_matrix
from scipy.sparse.csgraph import connected_components
from scipy.sparse.csgraph import minimum_spanning_tree
from scipy.spatial import distance_matrix
""" 
Description
Isomap을 기반으로 Manifold Graph를 구성함.

원본 차원 위에서 유클리드 거리를 통해 직접적인 거리를 구하지 않고, 곡면 위에서의 측지선 거리를 측정하기 위한
사전 작업으로 정의됨.

KNN 규칙을 기반으로 epsilon을 적용해서 adaptive하게 인접 노드를 연결.
- K개의 가장 인접한 노드를 연결 후보로 설정하고 이 중에서 epsilon 규칙을 위반하는 노드는 후보에서 제외
- epsilon 규칙 : 연결 후보 노드 간의 평균 거리의 1.5배에 해당하는 값보다 먼 거리는 규칙 위반
- 결론적으로 K개 이하의 노드 연결을 보장함.
- 풍화 시퀀스의 방향성을 만들기 위해 연결 노드의 풍화 정도를 바탕으로 가중치 조정
    -> 풍화 점수가 높은 노드일 경우 가중치를 작게, 반대의 경우 가중치를 크게 적용 

"""

def Ensure_Connected(edge_src, edge_dst, edge_weight, N, data_7d):

    src = np.array(edge_src)
    dst = np.array(edge_dst)
    wei = np.array(edge_weight)
    
    graph = csr_matrix((wei, (src, dst)), shape=(N, N))
    n_comp, labels = connected_components(graph, directed=False)
    
    if n_comp > 1:
        print(f"🔄 단절된 그래프 감지 ({n_comp}개). MST로 연결 강제 보정...")
        
        # 전체 데이터에 대한 최소 스패닝 트리 생성 (거리 기반)
        # 데이터가 클 경우를 대비해 distance_matrix 대신 샘플링 고려 가능
        dist_mat = distance_matrix(data_7d, data_7d) # data_7d는 외부 변수 혹은 인자로 전달받아야 함
        mst = minimum_spanning_tree(csr_matrix(dist_mat))
        
        mst_src, mst_dst = mst.nonzero()
        mst_wei = mst.data * 2.0 # MST 엣지는 KNN보다 우선순위가 낮도록 가중치 증폭
        
        # 기존 리스트에 추가
        edge_src.extend(mst_src.tolist())
        edge_dst.extend(mst_dst.tolist())
        edge_weight.extend(mst_wei.tolist())
        
    return edge_src, edge_dst, edge_weight

def compute_weather_score(samples_7d):

    """
    samples_7d: (N,7)

    Returns
    -------
    weather_score : (N,)
    """

    diffuse = samples_7d[:, 0:3]
    specular = samples_7d[:, 3:6]
    roughness = samples_7d[:, 6]

    # =====================================================
    # 1. Brightness
    # =====================================================

    brightness = diffuse.mean(axis=1)

    # =====================================================
    # 2. Saturation
    # =====================================================

    max_c = diffuse.max(axis=1)
    min_c = diffuse.min(axis=1)

    saturation = (
        (max_c - min_c)
        / (max_c + 1e-6)
    )

    # =====================================================
    # 3. Specular intensity
    # =====================================================

    spec_intensity = specular.mean(axis=1)

    # =====================================================
    # 4. Roughness
    # =====================================================

    rough_term = np.sqrt(
        np.clip(roughness, 0, 1)
    )

    # =====================================================
    # 5. Weather score
    # =====================================================

    weather_score = (
        0.40 * rough_term +
        0.30 * (1.0 - spec_intensity) +
        0.20 * (1.0 - saturation) +
        0.10 * (1.0 - brightness)
    )

    # normalize
    weather_score -= weather_score.min()

    weather_score /= (
        weather_score.max() + 1e-8
    )

    return weather_score.astype(np.float32)

# -----------------------------------Test Only (Deprecated) -----------------------------------

from sklearn.neighbors import NearestNeighbors
import numpy as np


def Get_KNN_Graph_Adaptive(
    samples_7d,
    K=16,
    epsilon_scale=1.5
):
    """
    Parameters
    ----------
    samples_7d : np.array(N,7)

    K : int
        Maximum neighbor candidates

    epsilon_scale : float
        Adaptive radius multiplier

    Returns
    -------
    edge_src : np.array(E,)
    edge_dst : np.array(E,)
    edge_weight : np.array(E,)
    weather_score : np.array(N,)
    """
    N = samples_7d.shape[0]
    weather_score = compute_weather_score(samples_7d)

    # 1. KNN Search (기존 방식 유지)
    nn = NearestNeighbors(n_neighbors=K + 1, algorithm='kd_tree')
    nn.fit(samples_7d)
    distances, indices = nn.kneighbors(samples_7d)
    distances, indices = distances[:, 1:], indices[:, 1:]

    edge_src, edge_dst, edge_weight = [], [], []

    # 2. 그래프 생성 및 가중치 계산
    for i in range(N):
        local_dists = distances[i]
        local_inds = indices[i]
        dp = np.mean(local_dists)
        epsilon = epsilon_scale * dp
        score_i = weather_score[i]

        for dist, j in zip(local_dists, local_inds):
            if dist > epsilon: continue # 엣지 생성 제한

            score_j = weather_score[j]
            delta = score_j - score_i
            progression_factor = (1.0 - 0.35 * delta) if delta >= 0 else (1.0 + 1.5 * abs(delta))
            
            # [수정] 가중치가 음수가 되거나 너무 커지지 않도록 클램핑
            weight = dist * np.clip(progression_factor, 0.1, 5.0)

            edge_src.append(i)
            edge_dst.append(j)
            edge_weight.append(weight)

    # 3. [핵심] 연결성 체크 및 단절 구간 강제 연결
    graph = csr_matrix((edge_weight, (edge_src, edge_dst)), shape=(N, N))
    n_comp, labels = connected_components(graph, directed=True, connection='weak')

    if n_comp > 1:
        # 단절된 컴포넌트들을 강제로 잇기 위해 각 컴포넌트의 대표 노드(centroid)를 찾아 연결
        for comp_id in range(n_comp):
            nodes_in_comp = np.where(labels == comp_id)[0]
            # 인접 컴포넌트로 향하는 아주 약한 엣지 추가 (단절 방지용 브릿지)
            target_comp = (comp_id + 1) % n_comp
            target_node = np.where(labels == target_comp)[0][0]
            source_node = nodes_in_comp[0]
            
            edge_src.append(source_node)
            edge_dst.append(target_node)
            edge_weight.append(100.0) # 매우 큰 가중치를 주어 최후의 수단으로만 사용하게 함

    edge_src, edge_dst, edge_weight = Ensure_Connected(edge_src, edge_dst, edge_weight, N, samples_7d)

    return (
        np.array(edge_src, dtype=np.int32),
        np.array(edge_dst, dtype=np.int32),
        np.array(edge_weight, dtype=np.float32),
        weather_score
    )








# def Get_KNN_graph(samples_7d, K=8, extra_ratio=4, epsilon_scale=1.5):
#     """
#     Params
#     ----------
#     samples_7d      : np.array(N,7)
#     K               : int
#     extra_ratio     : int
#     epsilon_scale   : float

#     return
#     edge_src        : np.array(E,E)
#     edge_dst        : np.array(E,E)
#     edge_weight     : np.array(E,E)
#     weather_score   : np.array(N,7)
#     ----------
#     """

#     N = samples_7d.shape[0]

#     weather_score = compute_weather_score(samples_7d)

#     extra_k = min(K * extra_ratio, N - 1)

#     nn = NearestNeighbors(n_neighbors=extra_k + 1, algorithm='auto')

#     nn.fit(samples_7d)

#     distances, indices = nn.kneighbors(samples_7d)

#     # remove self
#     distances = distances[:, 1:]
#     indices = indices[:, 1:]

#     edge_src = []
#     edge_dst = []
#     edge_weight = []

#     for i in range(N):

#         local_dists = distances[i]
#         local_inds = indices[i]

#         dp = np.mean(local_dists[:K])

#         epsilon = epsilon_scale * dp

#         score_i = weather_score[i]

#         for dist, j in zip(local_dists, local_inds):

#             if dist > epsilon:
#                 break

#             score_j = weather_score[j]

#             delta = score_j - score_i


#             # 풍화가 진행되는 방향일수록 가중치를 작게 적용, 반대일 결우 가중치를 크게 적용하여
#             # Knn 연결 가능성을 조정. 최종적으로 연결되는 노드는 가능한 풍화도가 높은 노드 위주로 구성
#             # -> 역방향 trajectory 방지
#             if delta >= 0:
#                 # forward progression
#                 progression_factor = 1.0 - 0.35 * delta

#             else:
#                 # backward penalty
#                 progression_factor = 1.0 + 1.5 * abs(delta)

#             weight = dist * progression_factor

#             edge_src.append(i)
#             edge_dst.append(j)
#             edge_weight.append(weight)

#     return (
#         np.array(edge_src, dtype=np.int32),
#         np.array(edge_dst, dtype=np.int32),
#         np.array(edge_weight, dtype=np.float32),
#         weather_score
#     )
