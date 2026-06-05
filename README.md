## 2026CapstoneDesign_08_01


경희대학교 2026 캡스톤디자인 08분반 01팀 협업 환경입니다.

Unreal Engine 5.7 프로젝트를 활용합니다.

Texture의 Waethering Sequence 생성 및 Weathering에 따른 Mesh Transformation (Cutting or Mowing) 를 구현 및 테스트하고 Unreal Plugin 으로 배포할 수 있는 형태로 통합하는 것을 목표로 합니다.

(이미지 및 GIF)
(각 케이스 별 비교)

## Terminology

- Weathering : 텍스처가 오염, 풍화, 변질되는 현상을 통칭합니다.
- Gamma-ton : 아래 논문에서 제시한 가상의 입자로, 풍화 효과를 만들어내는 주체입니다.
- Appearance Manifold : 특정 텍스처들의 픽셀 데이터를 통합하여 7차원 데이터로 재구성한 텐서입니다.
- Geodesic Distance (Matrix) : 측지선 거리 (행렬). Appearance Manifold 위 각 점들 간의 유클리드 거리가 아닌 저차원 곡면 위에서의 거리를 측정합니다. Dikjstra 알고리즘을 활용해 각 노드들 간의 측지선 최단 거리를 저장합니다.
- Weathering trajectory : 가장 풍화 상태가 많이 차이나는 두 AM상 포인트를 기준으로 측지선 경로를 의미합니다. 이 경로는 실시간 풍화 시뮬레이션을 위한 핵심 데이터입니다.


## Project Overview

2가지의 논문 내용을 결합합니다.
- 논문 A / 2005 SIGGRAPH : Visual Simulation of Weathering By γ-ton Tracing 
- 논문 B / 2006 SIGGRAPH : Appearance manifolds for modeling time-variant appearance of materials
사용자가 원하는 형태의 풍화된 텍스처를 얻고(A), 여러 개의 풍화 텍스처를 바탕으로 런타임에서 시간이 지남에 따라 풍화가 진행되는 효과(B)를 만들어 낼 수 있습니다.

플러그인은 에디터 전용 기능과 런타임 기능을 모두 지원합니다.
- Generating Weathered Texture : Gammaton Simulation / Editor Only
- Generation Weathering Trajectory : Appearance Manifold / Editor Only
- Interpolating between Key Texture on trajectory : Runtime Exectution


## Project Structure
언리얼 플러그인 전용 프로젝트이며, 실질적으로 운용되는 프로젝트 구조는 아래와 같습니다.
  

```Plugins
Plugins
├─Docs
│   ├─.pdf
├─GammaTon
│   ├─ Content
│       ├─ WeatheringManagerComponent.uasset
│   ├─ Scripts 
│       ├─ MainManager.py
│       ├─ __init__.py
│       ├─ Modules
|           ├─ geodesic_backend.cp311-win_amd64.pyd
│           ├─ KNN.py
│           ├─ MDS.py
│           ├─ Reconstructor.py
│           ├─ Trajectory.py
│           ├─ Utility.py
│   ├─ Source
│       ├─ AppearanceManifold
│           ├─ AppearanceManifoldBlueprintFunctionLibrary.h
│           ├─ BuildNearestTrajectoryAsync.h
│       ├─ GammaTon
│           ├─ GammaTon.h
│   ├─ ThirdParty
│       ├─ embree4
│       ├─ PythonWheels
```


    
## Sequence
Step 1. Generating Weathered Texture
(이미지)
감마톤 시뮬레이션 툴을 이용해 지정된 스태틱 메쉬에 대한 풍화된 텍스처를 생성합니다. 원하는 시뮬레이션 프리셋을 그대로 활용하거나, 원하는 파라미터를 조정하여 사용할 수 있습니다.
생성된 결과물 중, BaseColor, Specular, Roughness에 해당하는 텍스처가 png 형태로 저장되어 Weathering Trajectory 생성에 활용됩니다.
-> Step 1 의 결과물만 활용하고 싶은 경우 다음 스텝을 진행할 필요가 없습니다.

Step 2. Generating Weathering Trajectory
(이미지)
풍화 상태가 공존하는 텍스처로부터 풍화 경로를 추론합니다. 
Weathering Manager Component를 풍화 시물레이션을 수행하려는 액터에 부착하고, 기본 풍화 상태의 텍스처를 할당한 뒤 감마톤 결과물을 새 키프레임으로 등록합니다. ( 풍화 경로 생성을 위해선 키프레임이 둘 이상 필요합니다.)
이후 풍화 경로 생성을 수행합니다. 생성된 결과물은 바이너리 형태로 저장됩니다. (풍화 경로, 키프레임 등)

Step 3. Interpolating between Key Texture on trajectory
(GIF)
Component에 지정된 키프레임과 Weathering Trajectory를 기반으로 적절한 Step에 해당하는 중간 프레임을 생성, 메쉬에 적용합니다.
적절한 키프레임과 WT가 존재하고, 풍화 프로퍼티를 적절히 할당했다면, 인게임에서 즉시 풍화 시뮬레이션이 수행됩니다.


