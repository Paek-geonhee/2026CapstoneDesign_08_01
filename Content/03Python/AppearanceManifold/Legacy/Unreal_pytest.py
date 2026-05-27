import unreal
import sys
import os

# 1. 라이브러리 경로 추가 (UE 플러그인 내 libs 폴더)
plugin_libs_path = os.path.join(os.path.dirname(__file__), "Modules")
if plugin_libs_path not in sys.path:
    sys.path.append(plugin_libs_path)

def test_geodesic_import():
    try:
        # 2. 모듈 Import 테스트
        import geodesic_backend
        print("✅ Success: geodesic_backend imported successfully!")
        
        # 3. 간단한 더미 데이터로 기능 테스트
        import numpy as np
        
        # 더미 데이터 생성 (128x128 샘플과 유사한 환경 가정)
        src = np.array([0, 1], dtype=np.int32)
        dst = np.array([1, 2], dtype=np.int32)
        weight = np.array([1.0, 2.0], dtype=np.float32)
        N = 3
        
        print("🚀 Running compute_geodesic_distance_matrix...")
        dist_matrix = geodesic_backend.compute_geodesic_distance_matrix(src, dst, weight, N)
        
        print("✅ Success: Computation completed!")
        print(f"Result shape: {dist_matrix.shape}")
        print(dist_matrix)
        
    except ImportError as e:
        print(f"❌ Error: Could not import geodesic_backend. {e}")
    except Exception as e:
        print(f"❌ Error during execution: {e}")

if __name__ == "__main__":
    test_geodesic_import()