from sklearn.neighbors import NearestNeighbors
import numpy as np
#from WeatheringScore import compute_weather_score

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


def Get_KNN_graph(samples_7d, K=8, extra_ratio=4, epsilon_scale=1.5):
    """
    Params
    ----------
    samples_7d      : np.array(N,7)
    K               : int
    extra_ratio     : int
    epsilon_scale   : float

    return
    edge_src        : np.array(E,E)
    edge_dst        : np.array(E,E)
    edge_weight     : np.array(E,E)
    weather_score   : np.array(N,7)
    ----------
    """

    N = samples_7d.shape[0]

    weather_score = compute_weather_score(samples_7d)

    extra_k = min(K * extra_ratio, N - 1)

    nn = NearestNeighbors(n_neighbors=extra_k + 1, algorithm='auto')

    nn.fit(samples_7d)

    distances, indices = nn.kneighbors(samples_7d)

    # remove self
    distances = distances[:, 1:]
    indices = indices[:, 1:]

    edge_src = []
    edge_dst = []
    edge_weight = []

    for i in range(N):

        local_dists = distances[i]
        local_inds = indices[i]

        dp = np.mean(local_dists[:K])

        epsilon = epsilon_scale * dp

        score_i = weather_score[i]

        for dist, j in zip(local_dists, local_inds):

            if dist > epsilon:
                break

            score_j = weather_score[j]

            delta = score_j - score_i


            # 풍화가 진행되는 방향일수록 가중치를 작게 적용, 반대일 결우 가중치를 크게 적용하여
            # Knn 연결 가능성을 조정. 최종적으로 연결되는 노드는 가능한 풍화도가 높은 노드 위주로 구성
            # -> 역방향 trajectory 방지
            if delta >= 0:
                # forward progression
                progression_factor = 1.0 - 0.35 * delta

            else:
                # backward penalty
                progression_factor = 1.0 + 1.5 * abs(delta)

            weight = dist * progression_factor

            edge_src.append(i)
            edge_dst.append(j)
            edge_weight.append(weight)

    return (
        np.array(edge_src, dtype=np.int32),
        np.array(edge_dst, dtype=np.int32),
        np.array(edge_weight, dtype=np.float32),
        weather_score
    )




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
    K=8,
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

    # =====================================================
    # 1. weather score
    # =====================================================

    weather_score = compute_weather_score(samples_7d)

    # =====================================================
    # 2. KNN search
    # =====================================================

    nn = NearestNeighbors(
        n_neighbors=K + 1,
        algorithm='auto'
    )

    nn.fit(samples_7d)

    distances, indices = nn.kneighbors(samples_7d)

    # remove self neighbor
    distances = distances[:, 1:]
    indices = indices[:, 1:]

    # =====================================================
    # 3. graph construction
    # =====================================================

    edge_src = []
    edge_dst = []
    edge_weight = []

    for i in range(N):

        local_dists = distances[i]
        local_inds = indices[i]

        # -------------------------------------------------
        # adaptive epsilon
        # -------------------------------------------------

        dp = np.mean(local_dists)

        epsilon = epsilon_scale * dp

        score_i = weather_score[i]

        # -------------------------------------------------
        # candidate neighbors
        # -------------------------------------------------

        for dist, j in zip(local_dists, local_inds):

            # remove overly distant neighbors
            if dist > epsilon:
                continue

            score_j = weather_score[j]

            delta = score_j - score_i

            # =============================================
            # progression weighting
            # =============================================

            if delta >= 0:

                # forward progression
                progression_factor = (
                    1.0 - 0.35 * delta
                )

            else:

                # backward penalty
                progression_factor = (
                    1.0 + 1.5 * abs(delta)
                )

            weight = dist * progression_factor

            # =============================================
            # append edge
            # =============================================

            edge_src.append(i)
            edge_dst.append(j)
            edge_weight.append(weight)

    # =====================================================
    # 4. return
    # =====================================================

    return (
        np.array(edge_src, dtype=np.int32),
        np.array(edge_dst, dtype=np.int32),
        np.array(edge_weight, dtype=np.float32),
        weather_score
    )


import heapq


def Get_KNN_Graph_Adaptive_Streaming(
    samples_7d,
    K=8,
    epsilon_scale=1.5
):

    N = samples_7d.shape[0]

    # =====================================================
    # 1. weather score
    # =====================================================

    weather_score = compute_weather_score(samples_7d)

    # =====================================================
    # 2. graph buffers
    # =====================================================

    edge_src = []
    edge_dst = []
    edge_weight = []

    # =====================================================
    # 3. streaming KNN
    # =====================================================

    for i in range(N):

        xi = samples_7d[i]

        # -------------------------------------------------
        # maxheap for Top-K smallest distances
        #
        # stored as:
        # (-dist, neighbor_index)
        # -------------------------------------------------

        topk_heap = []

        for j in range(N):

            if i == j:
                continue

            xj = samples_7d[j]

            # =============================================
            # euclidean distance
            # =============================================

            dist = np.linalg.norm(xi - xj)

            # =============================================
            # heap fill phase
            # =============================================

            if len(topk_heap) < K:

                heapq.heappush(
                    topk_heap,
                    (-dist, j)
                )

                continue

            # =============================================
            # current worst candidate
            # =============================================

            current_max_dist = -topk_heap[0][0]

            # =============================================
            # replace if better
            # =============================================

            if dist < current_max_dist:

                heapq.heapreplace(
                    topk_heap,
                    (-dist, j)
                )

        # =================================================
        # 4. extract neighbors
        # =================================================

        local_neighbors = []

        while topk_heap:

            neg_dist, j = heapq.heappop(topk_heap)

            local_neighbors.append(
                (-neg_dist, j)
            )

        local_neighbors.reverse()

        local_dists = np.array(
            [x[0] for x in local_neighbors],
            dtype=np.float32
        )

        local_inds = np.array(
            [x[1] for x in local_neighbors],
            dtype=np.int32
        )

        # =================================================
        # 5. adaptive epsilon
        # =================================================

        dp = np.mean(local_dists)

        epsilon = epsilon_scale * dp

        score_i = weather_score[i]

        # =================================================
        # 6. graph construction
        # =================================================

        for dist, j in zip(local_dists, local_inds):

            if dist > epsilon:
                continue

            score_j = weather_score[j]

            delta = score_j - score_i

            # =============================================
            # progression weighting
            # =============================================

            if delta >= 0:

                progression_factor = (
                    1.0 - 0.35 * delta
                )

            else:

                progression_factor = (
                    1.0 + 1.5 * abs(delta)
                )

            weight = dist * progression_factor

            edge_src.append(i)
            edge_dst.append(j)
            edge_weight.append(weight)

    # =====================================================
    # 7. return
    # =====================================================

    return (
        np.array(edge_src, dtype=np.int32),
        np.array(edge_dst, dtype=np.int32),
        np.array(edge_weight, dtype=np.float32),
        weather_score
    )