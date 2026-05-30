import os
import numpy as np
import time
import threading
import struct
import unreal

# 연산용 모듈 (언리얼과 무관한 로직들)
from AppearanceManifold.Modules.geodesic_backend import compute_geodesic_distance_matrix
from AppearanceManifold.Modules.Utility import combine_to_7d_tensor, load_and_downsample, combine_to_7d, normalize_features
from AppearanceManifold.Modules.Trajectory import build_weathering_trajectory, resample_trajectory
from AppearanceManifold.Modules.KNN import Get_KNN_Graph_Adaptive
from AppearanceManifold.Modules.MDS import Get_MDS_graph

from AppearanceManifold.Modules._Reconstructor import build_weathering_interpolation_path, export_texture_set


def export_to_unreal_binary(trajectory, samples_7d, filepath):
    """
    언리얼 C++에서 FFileHelper::LoadFileToArray로 읽을 수 있는 바이너리 포맷
    구조: [T(int32)] [N(int32)] [trajectory_data(float*3*T)] [samples_data(float*7*N)]
    """
    T = trajectory.shape[0]
    N = samples_7d.shape[0]
    
    with open(filepath, "wb") as f:
        # 1. 헤더: 데이터 개수 저장
        f.write(struct.pack('ii', T, N))
        # 2. 본문: trajectory (T,3) -> float32
        f.write(trajectory.astype(np.float32).tobytes())
        # 3. 본문: samples_7d (N,7) -> float32
        f.write(samples_7d.astype(np.float32).tobytes())
        
    print(f"💾 [Exporter] 바이너리 파일 저장 완료: {filepath}")

def export_to_unreal_runtime_data(trajectory_samples, filepath):
    """
    Trajectory의 실제 물리적 데이터(T, 7)를 바이너리로 저장합니다.
    언리얼 C++은 이 파일만 로드하면 런타임 연산을 즉시 수행할 수 있습니다.
        
    구조: [T(int32)] [데이터(float * T * 7)]
    """
    T = trajectory_samples.shape[0]
        
    with open(filepath, "wb") as f:
            # 1. 궤적의 길이(T) 저장
        f.write(struct.pack('i', T))
            # 2. 실제 물리적 샘플 데이터(T, 7) 저장
        f.write(trajectory_samples.astype(np.float32).tobytes())
            
    print(f"💾 [Exporter] 런타임용 물리 데이터 저장 완료: {filepath} (Points: {T})")

class WeatheringPipeline:
    # 저장 경로를 OS 표준으로 설정 (언리얼 API 호출 없음)
    _SAVE_PATH = os.path.join(unreal.Paths.project_content_dir(), "_WeatheringResults", ".npy")
    _callback_registered = False
    _callback_handle = None

    # --------------------------------------------------------
    # Runtime manifold cache
    # --------------------------------------------------------

    _cached_trajectory = None
    _cached_path = None
    _cached_samples_7d = None
    _cached_embedded = None

    @staticmethod
    def start_weathering(tex_paths, file_name = "trajectory_runtime"):
        """파이프라인 시작 엔트리 포인트"""
        if os.path.exists(WeatheringPipeline._SAVE_PATH):
            os.remove(WeatheringPipeline._SAVE_PATH)
            
        # 1. 연산 스레드 시작
        thread = threading.Thread(target=WeatheringPipeline._worker, args=(tex_paths,file_name,))
        thread.start()
        
        # 2. 메인 스레드 감시자 등록 시 핸들 저장
        if WeatheringPipeline._callback_handle is None:
            WeatheringPipeline._callback_handle = unreal.register_slate_pre_tick_callback(WeatheringPipeline._check_status)
            WeatheringPipeline._callback_registered = True
        return "Pipeline Started"
    

    @staticmethod
    def run_interpolation(
        texA_paths,
        texB_paths,
        alpha,
        output_dir,
        file_name = "trajectory_runtime"
    ):
        """
        texA_paths :
            [BaseColor, Specular, Roughness]

        texB_paths :
            [BaseColor, Specular, Roughness]
        """

        try:

            # ----------------------------------------------------
            # Validate manifold cache
            # ----------------------------------------------------

            if (
                WeatheringPipeline._cached_path is None or
                WeatheringPipeline._cached_samples_7d is None
            ):
                print(
                    "❌ No cached trajectory data. "
                    "Run start_weathering() first."
                )
                return False

            # ----------------------------------------------------
            # Load Texture A
            # ----------------------------------------------------

            D1, S1, R1 = load_and_downsample(texA_paths[0],texA_paths[1],texA_paths[2],128)

            TexA_7d = combine_to_7d_tensor(D1,S1,R1)

            # ----------------------------------------------------
            # Load Texture B
            # ----------------------------------------------------

            D2, S2, R2 = load_and_downsample(texB_paths[0],texB_paths[1],texB_paths[2],128)

            TexB_7d = combine_to_7d_tensor(D2,S2,R2)

            # ----------------------------------------------------
            # Interpolation
            # ----------------------------------------------------

            result = build_weathering_interpolation_path(TexA_7d,TexB_7d,WeatheringPipeline._cached_samples_7d,WeatheringPipeline._cached_path,alpha)

            # ----------------------------------------------------
            # Export
            # ----------------------------------------------------

            output_prefix = os.path.join(output_dir, "interpolation")

            export_texture_set(result, output_prefix)

            print("✅ Weathering interpolation complete.")

            return True

        except Exception as e:

            import traceback

            print(
                f"❌ Interpolation Error:\\n"
                f"{traceback.format_exc()}"
            )

        return False



    @staticmethod
    def _worker(tex_paths, file_name):
        """[비동기 연산] 순수 데이터 연산만 수행"""
        try:
            print("🚀 [Worker] 연산 시작...")
            D, S, R = load_and_downsample(tex_paths[0], tex_paths[1], tex_paths[2], 128)
            X = normalize_features(combine_to_7d(D, S, R))
            
            src, dst, wei, wea = Get_KNN_Graph_Adaptive(X)
            MDS_Dist = Get_MDS_graph(compute_geodesic_distance_matrix(src, dst, wei, X.shape[0]), 3)
            
            points, path = build_weathering_trajectory(MDS_Dist, src, dst, wei, np.argmin(wea), np.argmax(wea))
            trajectory = resample_trajectory(points, 256)

            trajectory_samples = X[path] 
            
            # 캐시 저장
            WeatheringPipeline._cached_trajectory = trajectory
            WeatheringPipeline._cached_path = path
            WeatheringPipeline._cached_samples_7d = X
            
            # 3. 최적화된 바이너리 저장
            save_dir = os.path.dirname(WeatheringPipeline._SAVE_PATH)
            os.makedirs(save_dir, exist_ok=True)
            
            # 런타임용 바이너리 저장 (물리적 정보 포함)
            runtime_bin_path = WeatheringPipeline._SAVE_PATH.replace(".npy", file_name + ".bin")
            export_to_unreal_runtime_data(trajectory_samples, runtime_bin_path)
            
            print(f"✅ [Worker] 런타임 최적화 데이터 준비 완료.")
                
        except Exception as e:
            # 에러 전체 내용을 출력 (traceback 활용)
            import traceback
            print(f"❌ [Worker] 연산 에러 상세:\n{traceback.format_exc()}")

    @staticmethod
    def _check_status(delta_time):
        if os.path.exists(WeatheringPipeline._SAVE_PATH):
            try:
                trajectory = np.load(WeatheringPipeline._SAVE_PATH)
                WeatheringPipeline._visualize(trajectory)
                os.remove(WeatheringPipeline._SAVE_PATH)
                
                # 안전한 해제 로직
                if WeatheringPipeline._callback_handle is not None:
                    unreal.unregister_slate_pre_tick_callback(WeatheringPipeline._callback_handle)
                    WeatheringPipeline._callback_handle = None
            except Exception as e:
                print(f"시각화 에러: {e}")
        return True

    @staticmethod
    def _visualize(trajectory):
        """[메인 스레드] 시각화 대신 데이터를 로그에 출력"""
        print(f"📊 [Data Inspection] Trajectory shape: {trajectory.shape}")
        
        # 전체 데이터 중 첫 5개의 포인트값만 확인
        for i in range(min(5, len(trajectory))):
            point = trajectory[i]
            print(f"  Point {i}: x={point[0]:.4f}, y={point[1]:.4f}, z={point[2]:.4f}")
        
        # 마지막 포인트 확인
        if len(trajectory) > 5:
            last = trajectory[-1]
            print(f"  ... (중략) ...")
            print(f"  Last Point: x={last[0]:.4f}, y={last[1]:.4f}, z={last[2]:.4f}")
            
        print("✅ 데이터 출력 완료.")
