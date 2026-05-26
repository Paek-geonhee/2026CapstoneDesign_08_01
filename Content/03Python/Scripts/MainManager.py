import os
import numpy as np
import time
import threading
import unreal

# 연산용 모듈 (언리얼과 무관한 로직들)
from AppearanceManifold.Modules.geodesic_backend import compute_geodesic_distance_matrix
from AppearanceManifold.Modules.Utility import load_and_downsample, combine_to_7d, normalize_features
from AppearanceManifold.Modules.Trajectory import build_weathering_trajectory, resample_trajectory
from AppearanceManifold.Modules.KNN import Get_KNN_Graph_Adaptive
from AppearanceManifold.Modules.MDS import Get_MDS_graph

class WeatheringPipeline:
    # 저장 경로를 OS 표준으로 설정 (언리얼 API 호출 없음)
    _SAVE_PATH = os.path.join(unreal.Paths.project_saved_dir(), "WeatheringResults", "trajectory.npy")
    _callback_registered = False
    _callback_handle = None

    @staticmethod
    def start_weathering(tex_paths):
        """파이프라인 시작 엔트리 포인트"""
        if os.path.exists(WeatheringPipeline._SAVE_PATH):
            os.remove(WeatheringPipeline._SAVE_PATH)
            
        # 1. 연산 스레드 시작
        thread = threading.Thread(target=WeatheringPipeline._worker, args=(tex_paths,))
        thread.start()
        
        # 2. 메인 스레드 감시자 등록 시 핸들 저장
        if WeatheringPipeline._callback_handle is None:
            WeatheringPipeline._callback_handle = unreal.register_slate_pre_tick_callback(WeatheringPipeline._check_status)
            WeatheringPipeline._callback_registered = True
        return "Pipeline Started"

    @staticmethod
    def _worker(tex_paths):
        """[비동기 연산] 순수 데이터 연산만 수행"""
        try:
            print("🚀 [Worker] 연산 시작...")
            D, S, R = load_and_downsample(tex_paths[0], tex_paths[1], tex_paths[2], 128)
            X = normalize_features(combine_to_7d(D, S, R))
            
            src, dst, wei, wea = Get_KNN_Graph_Adaptive(X)
            MDS_Dist = Get_MDS_graph(compute_geodesic_distance_matrix(src, dst, wei, X.shape[0]), 3)
            
            points, _ = build_weathering_trajectory(MDS_Dist, src, dst, wei, np.argmin(wea), np.argmax(wea))
            trajectory = resample_trajectory(points, 256)

            # 저장 전에 디렉토리 경로 확인 로그
            save_dir = os.path.dirname(WeatheringPipeline._SAVE_PATH)
            print(f"📂 [Worker] 저장 시도 경로: {save_dir}")
            
            os.makedirs(save_dir, exist_ok=True)
            np.save(WeatheringPipeline._SAVE_PATH, trajectory)
            
            if os.path.exists(WeatheringPipeline._SAVE_PATH):
                print("✅ [Worker] 연산 완료 및 파일 저장 완료.")
            else:
                print("❌ [Worker] 저장 시도했으나 파일이 존재하지 않음.")
                
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
