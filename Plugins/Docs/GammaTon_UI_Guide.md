# GammaTon 플러그인 UI 완전 가이드

논문: **"Visual Simulation of Weathering by γ-ton Tracing"** (Chen et al., SIGGRAPH 2005)  
작성일: 2026-05-24

---

## 목차

1. [γ-ton이란 무엇인가](#1-γ-ton이란-무엇인가)
2. [한 번의 γ-ton 수명 흐름도](#2-한-번의-γ-ton-수명-흐름도)
3. [Simulation — 전역 시뮬레이션 파라미터](#3-simulation--전역-시뮬레이션-파라미터)
4. [Ton Type 카드](#4-ton-type-카드)
   - 4.1 [Motion: ks / kp / kf](#41-motion-ks--kp--kf)
   - 4.2 [Carrier: sd / sp / sr / sh](#42-carrier-sd--sp--sr--sh)
   - 4.3 [Source 설정](#43-source-설정)
   - 4.4 [Transport Rules](#44-transport-rules)
5. [Physics — kp 포물선 파라미터](#5-physics--kp-포물선-파라미터)
6. [Cross-Channel — 채널 간 상호작용](#6-cross-channel--채널-간-상호작용)
7. [Per-Actor γ-Reflectance](#7-per-actor-γ-reflectance--δs--δp--δf)
8. [Per-Actor 초기 재질값 (Stain-Bleeding)](#8-per-actor-초기-재질값-stain-bleeding)
9. [Detail Textures — 디테일 텍스처](#9-detail-textures--디테일-텍스처)
10. [시나리오 프리셋 해설](#10-시나리오-프리셋-해설)
   - 얼룩 번짐 / 금속 녹청 / 도시 강수 소일링 / 생물학적 성장 / 파이프 누수 / 사막 풍사 퇴적 / 산업 매연 / 해안 염분 분사
11. [버튼 기능 요약](#11-버튼-기능-요약)
12. [파라미터 튜닝 가이드](#12-파라미터-튜닝-가이드)
13. [빠른 시작 체크리스트](#13-빠른-시작-체크리스트)

---

## 1. γ-ton이란 무엇인가

γ-ton(감마-톤)은 **풍화 입자 하나를 표현하는 가상 캐리어(carrier)**입니다.  
빗물 한 방울, 먼지 하나, 산화 분자 하나가 공간을 이동하다가 표면에 재질을 쌓는 과정을 몬테카를로 방식으로 시뮬레이션합니다.

각 γ-ton은 두 가지 상태를 가집니다:

| 상태 | 내용 |
|------|------|
| **motion** (운동 확률) | 다음 표면 충돌 시 어떻게 움직일지: 반사(ks), 튀기(kp), 흐름(kf), 정착(settle) |
| **carrier** (운반 재질) | 현재 들고 있는 재질의 양: 먼지(sd), 색소(sp), 거칠기(sr), 습도(sh) |

γ-ton은 소스에서 발사 → 표면에 충돌 → motion 확률에 따라 행동을 결정 → settle(정착) 시 carrier를 표면에 침착.  
이 과정이 **n_tons × n_iterations** 번 반복되어 텍스처가 채워집니다.

---

## 2. 한 번의 γ-ton 수명 흐름도

```
소스에서 발사
      │
      ▼
  표면 충돌
      │
      ├─ [deteriorate] motion 확률 감쇠: ks -= Δs, kp -= Δp, kf += kp잔량 - Δf
      │
      ├─ [PICKUP rules] 표면 재질 일부를 carrier에 흡수 (stain-bleeding 핵심)
      │
      ├─ 난수 ξ 추출
      │        │
      │   ξ < ks ──→ ks (hemisphere uniform 반사)  carrier × 0.92 소폭 감쇠
      │        │
      │   ξ < ks+kp ──→ kp (포물선 bounce)
      │        │
      │   ξ < ks+kp+kf ──→ kf (표면 흐름)  FLOW rules 적용 후 -n 방향 재충돌
      │        │
      │   나머지 ──→ settle (정착)  SETTLE rules 적용 → 텍스처에 침착
      │                              (sp는 sh 임계값 이상인 면에만 침착)
      │
      └─ settle이면 종료, 아니면 다시 표면 충돌로
```

settle 확률 = `max(0, 1 - ks - kp - kf)`  
**ks+kp+kf = 1.0 이면 절대 정착하지 않음 → deteriorate로 감쇠되어야 settle 가능**

---

## 3. Simulation — 전역 시뮬레이션 파라미터

### γ-tons / iter

매 이터레이션마다 발사하는 γ-ton 수.

- **영향**: 픽셀당 히트 수에 직접 비례. 많을수록 균일하고 정밀한 결과.
- **권장**: 512² 텍스처 기준 30,000~50,000 / 1024² 기준 100,000+
- **trade-off**: 2배 늘리면 시간도 2배.

### Iterations

시뮬레이션 반복 횟수.

- **영향**: 한 번 쌓인 재질이 다음 iteration에서 PICKUP으로 재분배됨 → 반복할수록 자연스러운 확산.  
  Cross-Channel (rust, humidity decay)도 iteration마다 1회 적용.
- **권장**: 20~40회. 50회 이상은 수확체감.

### Max bounces

γ-ton 하나가 최대 반사/이동할 수 있는 횟수. 초과 시 이탈(escape) 처리.

- **영향**: 낮으면 소스 바로 아래에만 쌓임. 높으면 멀리 퍼지지만 연산 증가.
- **권장**: ks 위주 시나리오 → 15~20 / kf 위주 시나리오 → 10~15.

### Deposit K

전역 침착 강도 스케일. 모든 SETTLE/FLOW 이벤트의 deposit량에 곱해짐.

**픽셀 포화 계산:**
```
픽셀당 평균 침착 = deposit_k × settle_prob × n_tons × n_iter
                   ─────────────────────────────────────────
                           texture_size²
```

| texture_size | 권장 deposit_k | n_tons × iter 조합 |
|-------------|----------------|-------------------|
| 256²        | 0.5~1.0        | 10k × 10 이상      |
| 512²        | 0.4~0.6        | 30k × 20 이상      |
| 1024²       | 0.5~0.8        | 80k × 30 이상      |

- **너무 낮으면** (< 0.1): 결과가 거의 안 보임.
- **너무 높으면** (> 2.0): 첫 iteration에 텍스처가 즉시 포화됨.

### Flow step (cm)

kf (표면 흐름) 이벤트 시 γ-ton이 이동하는 거리 (Unreal 단위: cm).

- **영향**: 클수록 드립 자국이 길게 뻗고, 작을수록 짧은 얼룩으로 남음.
- **권장**: 20~60 cm. 씬 스케일에 맞춰 조정.

### Texture size (px)

결과 aging 텍스처 해상도. 해상도가 높을수록 세밀하지만 같은 density를 채우려면 더 많은 ton이 필요.

- **테스트**: 256 → **품질**: 512 → **최고화질**: 1024+

---

## 4. Ton Type 카드

한 시뮬레이션에서 여러 종류의 γ-ton을 동시에 발사할 수 있습니다.  
각 Ton Type은 완전히 독립된 물리 설정을 가집니다 (Weight에 비례하여 확률적 선택).

**`+ Add Ton Type` 버튼**으로 추가, **`− Remove`**로 삭제.  
카드 헤더의 **▼/▶ 버튼**으로 접기/펼치기 가능.

---

### 4.1 Motion: ks / kp / kf

**settle 확률 = max(0, 1 − ks − kp − kf)**

이 세 값의 합이 1 미만일 때 남은 확률이 settle입니다.

---

#### ks — 반구 반사 확률 (Specular-hemisphere)

표면에 닿으면 법선 기준 반구 내 **균일 무작위 방향**으로 반사합니다 (논문 §3.2: "regard as diffuse").

- **높은 ks** (0.8~1.0): γ-ton이 여러 번 튀고 넓게 퍼짐 → Metallic Patina, 대기 산화
- **낮은 ks** (0.0~0.2): 바로 흐름/정착 → 빗물, 얼룩

> **carrier 감쇠**: ks 반사 1회마다 sd×0.92, sp×0.95, sr×0.92, sh×0.88 감쇠.  
> 소스 근처일수록 진하고 멀수록 옅은 자연스러운 그라디언트 형성.

---

#### kp — 포물선 bounce 확률 (Parabolic bounce)

중력의 영향을 받는 **포물선 궤적**으로 튑니다 (논문 §3.1).  
Δp 감쇠에 의해 kp가 줄어드는 만큼 kf로 전환됩니다 (논문 eq.3).

- **높은 kp** (0.5~0.9): 표면 위를 튀어다니다가 점차 흐름으로 전환 → 빗방울 튐, 모래 이동
- **낮은 kp** (0.0~0.2): 포물선 운동 거의 없음

**Δp (kp decay)와의 관계:**
```
hit 1: kp_new = max(kp - Δp, 0)
       kf_new = max(kf + kp_new - Δf, 0)   ← kp 잔량이 kf로 전환
```

---

#### kf — 표면 흐름 확률 (Surface flow)

중력 접선 방향으로 표면을 따라 이동합니다 (gravity 70% + random 30% 혼합).  
FLOW 규칙이 적용되어 이동 중에도 sp/sh를 부분 침착합니다.

- **높은 kf** (0.6~0.9): 긴 드립/줄무늬 패턴 → 빗물 흔적, 파이프 누수
- **낮은 kf** (0.0~0.2): 흐름 거의 없음

> **수직 vs 수평 면**: kf는 수직 면(벽)에서 가장 뚜렷한 세로 줄무늬를 만듭니다.

---

### 4.2 Carrier: sd / sp / sr / sh

γ-ton이 들고 다니는 재질의 **초기 구성 비율** (0~1 각 채널).  
settle 시 transport rules에 따라 표면에 침착됩니다.

---

#### sd — Soiling Density (먼지/소일 밀도)

표면을 덮는 중성 먼지·흙의 양.  
텍스처 R 채널 → 머티리얼에서 `Lerp(OrigBaseColor, DustColor × DustTexture, sd)`의 알파.

- **높은 sd** (0.8~1.0): 먼지, 모래, 분진 효과

> **방향 가중치**: settle 시 위를 향한 면일수록 더 많이 쌓입니다.  
> 계수: `w_sd = 0.2 + 0.8 × upward_factor` — 수직면에는 최소 0.2배, 완전한 수평면에는 1.0배가 적용됩니다.

---

#### sp — Soiling Pigment (색소)

유색 오염물 (녹물, 이끼, 산화 피막 등).  
텍스처 G 채널 → 머티리얼에서 `Lerp(위 결과, PigmentColor × PigmentTexture, sp)`의 알파.

- **높은 sp** (0.8~1.0): 짙은 색소 침착 → 녹청, 이끼, 갈색 얼룩

> **이끼 조건**: sp는 표면의 sh(습도)가 **0.20 이상인 곳에서만** 침착됩니다.  
> 또한 위를 향한 노출면보다 그늘진 수직면에 더 집중됩니다.

> **자동 피드백**: 메시 원본 BaseColor의 밝기가 매 히트마다 sp에 추가됩니다.  
> 어두운 틈새는 sp가 더 빨리 쌓입니다.

---

#### sr — Soiling Roughness (거칠기)

표면 미세 텍스처 변화.  
텍스처 B 채널 → 머티리얼에서 `Roughness = OrigRoughness + sr × RoughnessScale`.

- **높은 sr** (0.5~1.0): 거친 표면 → 소금 결정, 부식, 산화 피막

---

#### sh — Soiling Humidity (습도/수분)

표면의 수분량. Cross-Channel 규칙의 입력값으로 작용.  
텍스처 A 채널 → 머티리얼에서 `BaseColor × (1 − sh × WetnessScale)` (젖은 표면은 어두워 보임).

- **높은 sh** (0.8~1.0): 빗물, 이끼 수분, 누수
- **낮은 sh** (0.0): 건조한 입자 (먼지, 모래)

> **sh 피드백**: surfel의 sh가 쌓이면 다음 γ-ton의 ks와 kp가 모두 약화됩니다 → 젖은 표면에서 입자가 더 잘 달라붙고 흐름으로 전환이 빨라집니다.  
> `ks_effective = ks × (1 − 0.5 × sh_surf)`  
> `kp_effective = kp × (1 − 0.3 × sh_surf)`

---

### 4.3 Source 설정

γ-ton을 발사하는 소스의 종류와 위치.

#### Source Type

| 타입 | 설명 | 용도 |
|------|------|------|
| **AREA_TOP** | Z 위치에서 아래로 쏘는 직사각형 면 | 비, 낙진, 상부 먼지 |
| **DIRECTIONAL** | 특정 방향의 평행 빔 | 측면 바람, 해풍, 사막 모래 |
| **POINT** | 한 점에서 퍼져 나오는 원뿔 | 파이프 누수, 스포트라이트 |
| **ENVIRONMENT** | 구면 전방향 발사 (HalfX = 반지름) | 대기 산화, 이끼 포자, 전방향 부식 |

소스 위치는 **viewport에 디버그 렌더링**됩니다. 값을 바꾸면 실시간으로 반영됩니다.

#### CX / CY / CZ — 소스 중심 좌표 (cm)

UE 월드 좌표 (Z-up). AREA_TOP에서 CZ는 천장 높이.

#### DX / DY / DZ — 방향 벡터

발사 방향. 자동으로 normalize 됩니다.

#### Spread (도, °)

원뿔 반각. 0° = 완전 평행 / 30° = 넓은 분산.

#### HalfX / HalfZ (cm)

소스 면적의 절반 크기. ENVIRONMENT에서는 **HalfX = 구 반지름**이고 HalfZ는 무시됨.

---

### 4.4 Transport Rules

γ-ton과 표면 사이의 재질 이동 규칙. 형식:

```
[Event]  [To Entity].[To Channel]  ←  [From Entity].[From Channel]  × Coeff
```

#### Event

| 이벤트 | 발생 시점 |
|--------|----------|
| **PICKUP** | 표면에 닿을 때마다 (ks/kp/kf/settle 모두) |
| **FLOW** | kf 흐름 이벤트 |
| **SETTLE** | 정착 이벤트 |

#### Entity / Channel

| Entity | 의미 |
|--------|------|
| **TON** | 현재 이동 중인 γ-ton의 carrier |
| **SURFACE** | 충돌한 surfel의 material 누적값 |

Channel: `sd` / `sp` / `sr` / `sh`

#### 기본 규칙 해설 (`GTDefaultTransportRules`)

```
PICKUP: TON.sp  ← SURFACE.sp × 1.0   → 표면의 기존 색소를 ton이 흡수해서 퍼뜨림
PICKUP: TON.sh  ← SURFACE.sh × 1.0   → 표면의 습도를 ton이 흡수

FLOW:   SURF.sp ← TON.sp   × 0.25    → 흐르면서 색소의 25%를 그 자리에 남김
FLOW:   SURF.sh ← TON.sh   × 0.25    → 흐르면서 습도의 25%를 그 자리에 남김

SETTLE: SURF.sd ← TON.sd   × 1.0     → 정착 시 먼지 전량 침착
SETTLE: SURF.sp ← TON.sp   × 1.0     → 정착 시 색소 전량 침착
SETTLE: SURF.sr ← TON.sr   × 1.0     → 정착 시 거칠기 전량 침착
SETTLE: SURF.sh ← TON.sh   × 1.0     → 정착 시 습도 전량 침착
```

#### 커스텀 규칙 예시

**표면의 기존 sr을 pickup해서 퍼뜨리기 (부식 전파):**
```
PICKUP  TON.sr ← SURFACE.sr × 1.0
SETTLE  SURF.sr ← TON.sr    × 1.0
```

**Stain-Bleeding (표면의 녹을 ton이 흡수해 이동):**
```
PICKUP  TON.sr ← SURFACE.sr × 1.0    ← 이 규칙이 체인의 녹을 계단으로 옮김
```

**+ Rule** 버튼으로 추가, **Reset** 버튼으로 기본값으로 초기화.

---

## 5. Physics — kp 포물선 파라미터

### Bounce dist. (cm)

kp 포물선 1회 바운스의 최대 이동 거리.

- **크면**: 입자가 멀리 튐 (모래폭풍, 튀는 빗방울)
- **작으면**: 제자리 근처에서 짧게 튐
- **권장**: 30~80 cm

### Parabola gravity

포물선 궤적의 중력 강도.

| 값 | 효과 |
|----|------|
| 0.1~0.3 | 거의 평평한 포물선 (옆으로 멀리 날아감) |
| 0.5 | 완만한 호 (기본값, 자연스러운 빗방울) |
| 1.0~2.0 | 급격한 낙하 (무거운 입자, 모래알) |

---

## 6. Cross-Channel — 채널 간 상호작용

매 **iteration 1회** 모든 픽셀에 적용됩니다 (γ-ton 추적과 별개).

### sh→sr (rust rate) — 습도에 의한 녹 성장

```
sr += rust_rate × sh    (매 iteration, 모든 픽셀)
```

| 값 | 효과 |
|----|------|
| 0.0 | 비활성 |
| 0.005~0.015 | 완만한 녹 형성 |
| 0.05+ | 빠른 녹 확산 |

### sh decay — 습도 증발

```
sh *= (1 - decay)    (매 iteration, 모든 픽셀)
```

| 값 | 효과 |
|----|------|
| 0.0 | 습도 영구 축적 |
| 0.1~0.3 | 느린 건조 (이끼, 장기 습윤) |
| 0.5 | 중간 (빗물 → 반건조) |
| 0.65 | 이끼 시나리오 권장 (노출면 빠르게 건조, 그늘만 유지) |
| 0.8~1.0 | 빠른 건조 (일시적 습도) |

### sp covers sd (moss) — 색소가 먼지를 덮음

```
sd -= cover_rate × sp    (매 iteration, 모든 픽셀)
```

| 값 | 효과 |
|----|------|
| 0.0 | 비활성 |
| 0.05~0.15 | 이끼가 먼지를 서서히 대체 |
| 0.3+ | 이끼가 빠르게 먼지 지움 |

---

## 7. Per-Actor γ-Reflectance — Δs / Δp / Δf

`Refresh Actor List` 버튼으로 현재 선택된 Actor들을 리스트에 추가합니다.  
각 Actor마다 **γ-reflectance** (감쇠 계수)를 독립적으로 설정할 수 있습니다.

---

### Δs — ks decay (반사 감쇠)

```
ks_new = max(ks - Δs, 0)
```

**updateReflectance()**: 표면에 sr(거칠기)이 쌓이면 Δs가 자동으로 증가합니다.
```
Δs_effective = Δs_base + sr_accumulated × 0.3
```

| Δs 값 | 의미 |
|--------|------|
| 0.0 | ks 전혀 감쇠 없음 (영원히 반사) |
| 0.1~0.3 | 느린 감쇠 (금속, 유리) |
| 0.5 | 기본값 — 2번 히트 후 ks=0 |
| 0.8~1.0 | 빠른 포획 (거친 콘크리트, 흙) |

---

### Δp — kp decay (포물선 감쇠 → kf 전환)

```
kp_new = max(kp - Δp, 0)
kf_new = max(kf + kp_new - Δf, 0)
```

**updateReflectance()**: sh(습도)가 쌓이면 Δp 자동 증가 → 젖은 표면에서 흐름 빠르게 전환.

---

### Δf — kf decay (흐름 감쇠 → settle 전환)

**updateReflectance()**: sh(습도)가 쌓이면 Δf가 자동으로 증가합니다.
```
Δf_effective = Δf_base + sh_accumulated × 0.1
```

| Δf 값 | 의미 |
|--------|------|
| 0.0 | 흐름 영구 지속 (멀리까지 퍼짐) |
| 0.05~0.1 | 10~20번 흐름 후 정착 (긴 드립 자국) |
| 0.2~0.5 | 2~5번 흐름 후 정착 (짧은 흔적) |

---

## 8. Per-Actor 초기 재질값 (Stain-Bleeding)

**논문 §4 stain-bleeding 구현을 위한 핵심 기능입니다.**

시뮬레이션 시작 전에 액터 표면에 재질이 이미 존재하는 상태를 설정합니다.  
PICKUP 규칙에 의해 γ-ton이 이 초기 재질을 흡수하여 다른 오브젝트로 운반합니다.

`Refresh Actor List` 클릭 후 각 Actor의 `— Initial Material —` 섹션에서 설정합니다.

| 파라미터 | 의미 | 사용 예 |
|----------|------|---------|
| **init sd** | 초기 먼지 밀도 | 오래된 건물 표면에 먼지가 이미 쌓인 상태 |
| **init sp** | 초기 색소 | 체인에 이미 녹이 있어서 계단으로 번질 때 |
| **init sr** | 초기 거칠기 | 마모된 금속, 풍화된 돌 |
| **init sh** | 초기 습도 | 이미 젖어있는 표면, 지하/음지 오브젝트 |

**논문 Fig.8 재현 예시:**
```
Actor: 철 체인       init sp = 1.0   (이미 완전히 녹슨 상태)
Actor: 콘크리트 계단 init sp = 0.0   (초기에 깨끗함)

Transport Rules (체인):
  PICKUP  TON.sp ← SURFACE.sp × 1.0   → 체인의 녹을 ton이 흡수

시뮬레이션 결과:
  ton이 체인에서 sp를 흡수 → 계단으로 이동 → SETTLE로 침착
  → 계단에 녹 얼룩이 형성됨
```

> **주의**: Actor 수와 Refresh List의 항목 수가 일치해야 per-actor 값이 적용됩니다.  
> 불일치 시 모든 액터에 기본값(0)이 사용됩니다.

---

## 9. Detail Textures — 디테일 텍스처

Run 버튼 위의 **Detail Textures** 섹션에서 텍스처를 직접 할당할 수 있습니다.

**논문 §3.5**: γ-ton map을 알파로 사용하여 원본 텍스처와 풍화된 텍스처를 블렌딩.

```
머티리얼 블렌딩 공식:
  DustEffect    = DustColor    × DustTexture       ← sd 채널 알파로 Lerp
  PigmentEffect = PigmentColor × PigmentTexture     ← sp 채널 알파로 Lerp

  BaseColor = Lerp(OrigBaseColor, DustEffect, AgingTex.R)
  BaseColor = Lerp(위 결과,       PigmentEffect, AgingTex.G)
  BaseColor = BaseColor × (1 - sh × WetnessScale)
```

### Dust Texture

먼지/소일 침착 영역에 적용되는 디테일 텍스처.

- **비워두면**: `DustColor` 단색으로만 표현
- **권장**: 콘크리트 표면, 노이즈 패턴, 흙 디테일 텍스처
- **ambientCG 검색어**: `seamless concrete dirt texture`, `dust noise texture`

### Pigment Texture

이끼/녹/얼룩 침착 영역에 적용되는 디테일 텍스처.

- **비워두면**: `PigmentColor` 단색으로만 표현
- **권장**: 이끼 패턴, 녹 얼룩, 유기적 노이즈 텍스처
- **ambientCG 검색어**: `seamless moss texture tileable`, `seamless rust stain texture`

> **텍스처 형식**: Color(Albedo) 채널만 필요합니다. Normal/Roughness 등 PBR 풀셋 불필요.  
> ambientCG에서 다운로드 시 `{TextureName}_Color.png` 파일 하나만 임포트하면 됩니다.

---

## 10. 시나리오 프리셋 해설

### Custom (manual)

직접 조정의 시작점. 모든 파라미터를 수동 설정.

---

### 얼룩 번짐 (Stain Bleeding) [논문 §3.5]

| 파라미터 | 값 | 의미 |
|----------|-----|------|
| ks/kp/kf | 0/0.8/0.2 | 포물선 위주 → 점차 흐름으로 전환 |
| sp | 0.9 | 진한 색소 (커피, 녹물 얼룩) |
| Δp/Δf | 0.4/0.05 | 2번 히트 → kf 지배 → 천천히 정착 |

→ 수직 벽에서 길게 뻗는 얼룩 줄무늬.

---

### 금속 녹청 (Metallic Patina) [논문 §3.5]

| 파라미터 | 값 | 의미 |
|----------|-----|------|
| ks | 1.0 | 완전 반사 |
| sp | 1.0 | 순수 색소 (녹청 피막) |
| Source | ENVIRONMENT | 전방향 대기에서 |
| Δs | 0.15 | 느린 감쇠 → 많이 반사 후 정착 |

→ 음각/홈에 더 많이 쌓임 (화학적 산화 표현).

> **자동 Transport Rule**: 이 프리셋 선택 시 다음 규칙이 자동으로 추가됩니다.  
> `PICKUP  TON.sr ← SURFACE.sr × 1.0` — 이미 산화된 표면의 거칠기를 γ-ton이 흡수해 인접 면으로 전파합니다.

---

### 도시 강수 소일링 (Urban Rain) [멀티톤]

**Ton Type 1** (빗물): kf=0.65, sh=0.85 — 세로 흐름 + 습도 축적  
**Ton Type 2** (먼지/매연): DIRECTIONAL 소스, sd=1 — 측면 바람 먼지

→ 위에서 오는 빗물 자국 + 측면 매연 얼룩의 복합 패턴.

---

### 생물학적 성장 / 이끼 [멀티톤]

**Ton Type 1** (수분): kf=0.92, sh=0.95 — 표면 따라 물 흐름  
**Ton Type 2** (이끼 포자): ENVIRONMENT, sp=0.9, sh=0.7, weight=0.2

- sh decay=0.65: 노출면은 빠르게 건조, 그늘/오목한 곳만 지속적으로 습함
- sp 임계값 0.20: sh가 쌓인 곳에서만 이끼 침착

→ 틈새·그늘에만 선택적 이끼 형성.

---

### 파이프 누수 (Pipe Drip)

| 파라미터 | 값 | 의미 |
|----------|-----|------|
| kf | 0.8 | 대부분 흐름 |
| sh | 0.95 | 거의 물 |
| Source | POINT (Z=350) | 파이프 위치에 점 소스 |
| sh→sr | 0.03 | 습도 → 녹 성장 |
| sh decay | 0.4 | 중간 속도 건조 → 흐름 자국이 반영구적으로 유지 |

→ 한 점에서 물이 흘러 세로 줄무늬 + 시간이 지나면 녹 자국.

> **자동 Transport Rule**: 이 프리셋 선택 시 다음 규칙이 자동으로 추가됩니다.  
> `PICKUP  TON.sr ← SURFACE.sr × 0.5` — 파이프 주변에 이미 생긴 부식(sr)을 흘러내리는 물이 흡수해 아래로 전파합니다.

---

### 사막 풍사 퇴적 (Desert Sand)

| 파라미터 | 값 | 의미 |
|----------|-----|------|
| ks/kp/kf | 0.5/0.3/0.1 | 반사 + 포물선 바운스 위주 |
| sd | 1.0 | 순수 먼지/모래 |
| sr | 0.1 | 미세한 거칠기 |
| Source | DIRECTIONAL (측면) | 바람 방향 평행 빔 |

→ 측면 바람으로 날아온 모래가 포물선 궤적으로 튀면서 쌓임. 바람이 닿는 면에 집중 퇴적.

---

### 산업 매연 (Industrial Soot)

| 파라미터 | 값 | 의미 |
|----------|-----|------|
| ks/kp | 0.4/0.4 | 반사·바운스 각 40% → 넓게 퍼짐 |
| sd | 0.6 | 먼지(매연 입자) |
| sp | 0.95 | 짙은 색소 (그을음) |
| Source | AREA_TOP | 위에서 아래로 낙하 |
| Cross-channel | 없음 | 화학 반응 없이 순수 퇴적 |

→ 위에서 떨어지는 그을음 입자가 바운스를 통해 넓게 분산. 수평면에 집중되며 sp가 높아 진한 얼룩 형성.

---

### 해안 염분 분사 (Coastal Salt Spray)

| 파라미터 | 값 | 의미 |
|----------|-----|------|
| ks/kp | 0.5/0.3 | 반사 + 바운스 |
| sd | 0.85 | 염분 퇴적 (높음) |
| sr | 0.5 | 강한 거칠기 (소금 결정) |
| sh | 0.25 | 약한 습도 (해무) |
| Source | DIRECTIONAL (측면 해풍) | 옆에서 불어오는 방향성 분사 |
| sh→sr | 0.008 | 해무 습도 → 표면 부식 |
| sh decay | 0.1 | 느린 건조 → 습도가 오래 유지됨 |

→ 측면 해풍으로 소금 입자가 분사되어 거칠기 증가 + 소금 퇴적. 반복 실행 시 sr이 누적되어 표면이 점점 거칠어짐.

---

## 11. 버튼 기능 요약

| 버튼 | 기능 |
|------|------|
| **▶ Run GammaTon Simulation** | 전체 시뮬레이션 실행. AgingTex 생성 및 저장, 머티리얼 적용 |
| **↺ Reapply Material** | 시뮬레이션 없이 저장된 AgingTex로 현재 색상/텍스처 설정만 재적용. DustColor, PigmentColor, DustTexture 등을 바꾼 뒤 빠르게 확인할 때 사용 |
| **Trace Single Ray** | γ-ton 1개의 경로를 뷰포트에 시각화 + 상세 로그 출력. 파라미터 검증용 |
| **Refresh Actor List** | 현재 선택된 Actor들로 Per-Actor 목록 갱신. 기존 값은 보존 |
| **+ Add Ton Type** | 새 Ton Type 카드 추가 |

> **Reapply 사용 흐름:**  
> Run 실행 → DustColor/텍스처 변경 → ↺ Reapply → 결과 확인 (시뮬 불필요)

---

## 12. 파라미터 튜닝 가이드

### 아무것도 안 보일 때

1. **Texture size를 256으로 낮추기** (가장 우선)
2. **Deposit K를 1.0으로 올리기**
3. **ks+kp+kf ≤ 0.9 확인** (settle 확률 10% 이상)
4. **n_tons 50,000 이상**으로 늘리기
5. Status 로그에서 `Settled:` 수 확인 → 0이면 settle이 아예 안 일어남

### 결과가 균일하게 덮여 패턴이 없을 때

- Deposit K를 **낮추기** (0.2~0.3)
- Iterations를 **줄이기**
- Δs를 **낮추기** (입자가 더 멀리 퍼짐)

### 드립 자국이 안 나올 때 (kf 씬)

- **kf 비율 높이기** (0.6+)
- **Flow step 늘리기** (50~80 cm)
- **수직/경사 메시** 사용
- **Source를 POINT**로 특정 위치에 고정

### 이끼가 온 표면에 균일하게 덮일 때

- **sh decay 높이기** (0.5~0.7): 노출면 습도 빠르게 증발
- **moss weight 낮추기** (0.1~0.2): 포자 밀도 감소
- **kf 높이기** (0.85+): 수분이 오목한 곳으로 집중

### 음각(홈, 틈)에 더 많이 쌓이게 하려면

- **ENVIRONMENT 소스** 사용
- **ks 높이고 Δs 낮추기** (많이 반사하면서 음각에 갇히는 효과)
- Max bounces를 **높이기** (15~20)

### Stain-Bleeding이 안 보일 때

- 소스 액터의 **init sp (또는 init sr) 값 설정** (0.5~1.0)
- PICKUP 규칙 확인: `TON.sp ← SURFACE.sp × 1.0` 있어야 함
- Iterations 늘리기 (번짐에는 여러 번 반복이 필요)

### 복합 풍화 권장 조합

| 원하는 효과 | 권장 시나리오 |
|------------|--------------|
| 금속 오래된 느낌 | Metallic Patina → Pipe Drip 순차 실행 |
| 도심 건물 | Urban Rain (멀티톤) |
| 정글 유적 | Biological Growth → Coastal Salt Spray |
| 공장 지대 | Industrial Soot → Pipe Drip |

---

## 13. 빠른 시작 체크리스트

```
□ 1. 오브젝트 선택 (Outliner에서 Static Mesh Actor 하나 이상)

□ 2. Scenario 선택 (처음엔 원하는 프리셋, 조정은 Custom으로)

□ 3. Texture size: 256 (빠른 테스트) 또는 512 (정상 품질)

□ 4. [선택] Detail Textures 섹션에서 DustTexture / PigmentTexture 할당

□ 5. Refresh Actor List 클릭 → Per-Actor 목록 확인
      [Stain-Bleeding 원할 시] init sp / init sd 값 설정

□ 6. Trace Single Ray 클릭 → Status 로그에서 경로 확인
      (Settled가 뜨면 물리 설정 정상)

□ 7. ▶ Run GammaTon Simulation 클릭

□ 8. /Game/GammaTon/ 폴더에 [ActorName]_Dust 텍스처 생성 확인

□ 9. 뷰포트에서 결과 확인
      안 보이면 → Deposit K ↑, Texture size ↓, n_tons ↑

□ 10. DustColor / PigmentColor 조정 후 ↺ Reapply로 빠른 색상 확인
       (시뮬 재실행 불필요)

□ 11. 만족스러우면 Texture size: 512~1024으로 올려서 재실행
```

### Status 로그 해석

```
Done!  20 iters | Settled: 184231 | Escaped: 415769 | Avg bounces: 3.21
reflect=892340  bounce=0  flow=201455  settle=184231
```

| 항목 | 의미 |
|------|------|
| Settled | 정착한 γ-ton 총수. 이게 낮으면 결과가 희미함 |
| Escaped | Max bounces 초과 이탈. 50% 이상이면 Max bounces 늘리기 |
| Avg bounces | 평균 반사 횟수. 1~5가 정상 |
| reflect/flow/settle | 이벤트별 발생 횟수. 원하는 이벤트가 지배적인지 확인 |

---

*논문: Chen et al., "Visual Simulation of Weathering by γ-ton Tracing", SIGGRAPH 2005*
