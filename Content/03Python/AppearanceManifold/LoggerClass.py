import json
import numpy as np

from GeodesicEvaluator import evaluate_geodesic_matrix
from KNNEvaluator import evaluate_knn_graph
from MDSEvaluator import evaluate_mds_embedding
from TrajectoryEvaluator import evaluate_trajectory


class PipelineLogger:

    def __init__(self):
        self.logs = {}

    def add(self, stage_name, values):
        self.logs[stage_name] = values

    def print_summary(self):
        print("=" * 80)
        print("Pipeline Quantitative Summary")
        print("=" * 80)

        for stage, values in self.logs.items():
            print(f"\n[{stage}]")

            for k, v in values.items():
                print(f"{k}: {v}")

    def save_json(self, save_path="pipeline_logs.json"):

        def convert(o):
            if isinstance(o, np.ndarray):
                return o.tolist()
            if isinstance(o, np.generic):
                return o.item()
            return o

        with open(save_path, "w", encoding="utf-8") as f:
            json.dump(self.logs, f, indent=4, ensure_ascii=False, default=convert)

        print(f"[Saved] {save_path}")


def evaluate_all_sequence(
    edge_src,
    edge_dst,
    edge_weight,
    samples_7d,
    geodesic_matrix,
    embedding_3d,
    trajectory_3d,
    file_name = "evaluation_logs"
):
    """
    전체 Manifold Pipeline 평가 수행

    Parameters
    ----------
    edge_src : np.ndarray(E,)
        그래프 edge source index

    edge_dst : np.ndarray(E,)
        그래프 edge destination index

    edge_weight : np.ndarray(E,)
        edge distance / weight

    samples_7d : np.ndarray(N, 7)
        원본 고차원 샘플 데이터

    knn_graph : Any
        KNN 그래프 객체 또는 인접 구조
        (현재는 edge 정보 기반 평가만 사용)

    geodesic_matrix : np.ndarray(N, N)
        Geodesic distance matrix

    embedding_3d : np.ndarray(N, 3)
        MDS 결과로 생성된 3D embedding

    trajectory_3d : np.ndarray(T, 3)
        생성된 weathering trajectory 경로
    """

    logger = PipelineLogger()

    # =========================================================
    # KNN GRAPH
    # =========================================================

    knn_result = evaluate_knn_graph(
        edge_src=edge_src,
        edge_dst=edge_dst,
        edge_weight=edge_weight,
        N=samples_7d.shape[0]
    )

    logger.add("KNN_GRAPH", knn_result)

    # =========================================================
    # GEODESIC DISTANCE
    # =========================================================

    geodesic_result = evaluate_geodesic_matrix(
        geodesic_matrix
    )

    logger.add("GEODESIC_MATRIX", geodesic_result)

    # =========================================================
    # MDS EMBEDDING
    # =========================================================

    mds_result = evaluate_mds_embedding(
        embedding_3d,
        geodesic_matrix
    )

    logger.add("MDS_EMBEDDING", mds_result)

    # =========================================================
    # TRAJECTORY
    # =========================================================

    trajectory_result = evaluate_trajectory(
        trajectory_3d
    )

    logger.add("TRAJECTORY", trajectory_result)

    # =========================================================
    # OUTPUT
    # =========================================================

    logger.print_summary()

    logger.save_json(file_name + ".json")