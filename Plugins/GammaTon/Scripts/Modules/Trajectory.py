import numpy as np
import matplotlib.pyplot as plt
from scipy.sparse.csgraph import dijkstra
from scipy.sparse import coo_matrix


def build_weathering_trajectory(embedded, edge_src, edge_dst, edge_weight, start_idx, end_idx):
    """
    Parameters
    ----------
    embedded            : (N,3)

    edge_src            : (E,)
    edge_dst            : (E,)
    edge_weight         : (E,)

    start_idx           : int
    end_idx             : int

    Returns
    -------
    trajectory_points   : (T,3)
    trajectory_indices  : (T,)
    """

    # 마지막 노드를 기준으로, 최단 측지선을 유지하는 역방향 경로를 탐색하고, 최종적으로 경로를 뒤집어 실경로를 확보
    # T는 해당 경로를 구성하는 인덱스(노드)의 개수를 의미

    N = embedded.shape[0]
    graph = coo_matrix((edge_weight, (edge_src, edge_dst)), shape=(N, N)).tocsr()
    
    # 1. 무방향 그래프로 변환하여 연결성 확보
    undirected_graph = graph.maximum(graph.transpose())
    
    # 2. Dijkstra 수행
    dist, predecessors = dijkstra(undirected_graph, directed=False, indices=start_idx, return_predecessors=True)

    path = []
    current = end_idx

    # 3. 안전한 경로 재구성 루프
    # 목표 지점에 도달할 수 없는 경우를 대비한 안전 장치
    if dist[end_idx] == np.inf:
        print(f"⚠️ 경로 완전 단절: start({start_idx})에서 end({end_idx})로의 경로가 없습니다. 강제 복구 실행.")
        # 현재 도달 가능한 노드 중 목표 지점과 가장 가까운 노드를 찾아 시작점으로 간주
        reachable = np.where(dist != np.inf)[0]
        if len(reachable) > 0:
            current = reachable[np.argmin(np.linalg.norm(embedded[reachable] - embedded[end_idx], axis=1))]
        else:
            raise ValueError("그래프가 완전히 단절되어 경로를 생성할 수 없습니다.")

    while current != start_idx:
        # predecessors가 -9999인 경우(경로 끊김) 예외 처리
        if predecessors[current] == -9999:
            print(f"⚠️ 중간 노드 끊김 감지: {current}. 가장 가까운 도달 가능 노드로 점프합니다.")
            
            # 도달 가능한 노드들 중 목표(start_idx)와 거리가 가장 가까운 노드 선택
            reachable = np.where(dist != np.inf)[0]
            current = reachable[np.argmin(np.linalg.norm(embedded[reachable] - embedded[current], axis=1))]
            continue # 재시도

        path.append(current)
        current = predecessors[current]
        
    path.append(start_idx)
    path.reverse()
    
    path = np.array(path, dtype=np.int32)
    trajectory_points = embedded[path]

    return trajectory_points, path

    # N = embedded.shape[0]


    # graph = coo_matrix((edge_weight,(edge_src, edge_dst)), shape=(N, N)).tocsr()
    
    # # 수정: 경로 탐색 시 무방향 그래프로 변환 (graph + graph.T)
    # undirected_graph = graph.maximum(graph.transpose())
    
    # dist, predecessors = dijkstra(undirected_graph, directed=False, indices=start_idx, return_predecessors=True)

    # path = []

    # current = end_idx

    # while current != start_idx:
    #     if current == -9999:
    #         print(f"끊김 발생! 끊긴 노드 인덱스: {current}, 현재까지 경로: {path}")
    #         raise ValueError("Trajectory path disconnected")

    #     path.append(current)

    #     current = predecessors[current]

    # path.append(start_idx)

    # path.reverse()

    # path = np.array(path)

    # trajectory_points = embedded[path]

    # return trajectory_points, path

def resample_trajectory(
    trajectory_points,
    num_points=64
):
    
    """
    Parameters
    ----------
    trajectory_points   : (T, 3)
    num_points          : int

    Returns
    -------
    out                 : (T, 3)
    """

    seg = trajectory_points[1:]- trajectory_points[:-1]

    seg_len = np.linalg.norm(seg, axis=1)

    cumulative = np.concatenate([[0],np.cumsum(seg_len)])

    total_len = cumulative[-1]

    sample_t = np.linspace(0,total_len,num_points)

    out = np.zeros((num_points, 3),dtype=np.float32)

    for i, t in enumerate(sample_t):

        idx = np.searchsorted(cumulative, t) - 1

        idx = np.clip(idx, 0, len(seg_len)-1)

        t0 = cumulative[idx]
        t1 = cumulative[idx + 1]

        alpha = (t - t0) / (t1 - t0 + 1e-8)
        

        p0 = trajectory_points[idx]
        p1 = trajectory_points[idx + 1]

        out[i] = p0 * (1 - alpha)+ p1 * alpha

    return out

def visualize_trajectory(
    embedded,
    trajectory,
    elev=30,
    azim=120
):
    """
    Parameters
    ----------
    embedded            : (N, 3)
    trajectory          : (T, 3)
    elev                : int
    azim                : int

    Returns
    -------
    None
    """

    fig = plt.figure(figsize=(8,8))

    ax = fig.add_subplot(111, projection='3d')

    # 1. MDS scatter
    ax.scatter(embedded[:,0],embedded[:,1],embedded[:,2],s=2,alpha=0.3,c = "red")

    # 2. trajectory line
    ax.plot(trajectory[:,0], trajectory[:,1], trajectory[:,2], linewidth=4)

    # 3. start / end point
    ax.scatter(trajectory[0,0], trajectory[0,1], trajectory[0,2], s=30, marker='o')
    ax.scatter(trajectory[-1,0], trajectory[-1,1], trajectory[-1,2], s=30,marker='^')

    ax.view_init(elev=elev,azim=azim)

    plt.show()