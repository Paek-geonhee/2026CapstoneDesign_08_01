// Fill out your copyright notice in the Description page of Project Settings.


#include "MWSBlueprintFunctionLibrary.h"

#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "ImageUtils.h"

#include "Kismet/KismetSystemLibrary.h"
#include "IImageWrapper.h"
#include "Misc/FileHelper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

#include "Async/ParallelFor.h"

struct FWeatheringSample {
    float Data[7];
};

FString UMWSBlueprintFunctionLibrary::GetTrajectorySaveDirectory(const FString& FileName)
{
    
    FString Directory = FPaths::Combine(FPaths::ProjectContentDir(),TEXT("_WeatheringResults"));

    IFileManager::Get().MakeDirectory(*Directory, true);

    return FPaths::Combine(Directory, FileName + TEXT(".bin"));
}

void UMWSBlueprintFunctionLibrary::LogWeatheringBinaryData(const FString& FilePath)
{
    // 1. 파일 전체를 TArray<uint8>로 로드
    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Failed to load file: %s"), *FilePath);
        return;
    }

    // 2. 데이터 유효성 검사 (최소 4바이트는 있어야 T를 읽음)
    if (FileData.Num() < 4)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ File is too small to contain header."));
        return;
    }

    // 3. 포인터를 통한 데이터 파싱
    // 첫 4바이트는 int32 (T)
    int32 T = *reinterpret_cast<int32*>(FileData.GetData());

    // 데이터 시작점 (헤더 4바이트 이후)
    float* SamplesData = reinterpret_cast<float*>(FileData.GetData() + 4);

    UE_LOG(LogTemp, Log, TEXT("✅ Binary Load Success. Trajectory Length (T): %d"), T);

    // 4. 데이터 검증 로그 (첫 5개의 포인트만 출력)
    int32 NumToPrint = FMath::Min(5, T);
    for (int32 i = 0; i < NumToPrint; ++i)
    {
        float* row = &SamplesData[i * 7];
        UE_LOG(LogTemp, Log, TEXT("   Point %d: [%.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f]"),
            i, row[0], row[1], row[2], row[3], row[4], row[5], row[6]);
    }

    // 마지막 포인트 확인
    if (T > 5)
    {
        float* last = &SamplesData[(T - 1) * 7];
        UE_LOG(LogTemp, Log, TEXT("   ... Last Point: [%.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f]"),
            last[0], last[1], last[2], last[3], last[4], last[5], last[6]);
    }
}

TArray<float> UMWSBlueprintFunctionLibrary::LoadWeatheringBinaryData(const FString& FilePath, int32& OutT)
{
    TArray<uint8> FileData;
    TArray<float> OutTrajectoryData;
    OutT = 0;

    if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Failed to load file: %s"), *FilePath);
        return OutTrajectoryData;
    }

    if (FileData.Num() < 4)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ File is too small to contain header."));
        return OutTrajectoryData;
    }

    // 1. 헤더에서 T(궤적 길이) 읽기
    OutT = *reinterpret_cast<int32*>(FileData.GetData());

    // 2. 데이터 크기 계산 (T * 7개 * 4바이트)
    int32 TotalElements = OutT * 7;
    int32 DataSizeInBytes = TotalElements * sizeof(float);

    if (FileData.Num() < 4 + DataSizeInBytes)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ File data is incomplete."));
        return OutTrajectoryData;
    }

    // 3. 데이터를 TArray<float>로 복사
    OutTrajectoryData.SetNumUninitialized(TotalElements);
    FMemory::Memcpy(OutTrajectoryData.GetData(), FileData.GetData() + 4, DataSizeInBytes);

    UE_LOG(LogTemp, Log, TEXT("✅ Binary Load Success. Trajectory Length (T): %d"), OutT);

    return OutTrajectoryData;
}


void UMWSBlueprintFunctionLibrary::InterpolateWeathering(
    const TArray<float>& TexA_7d, // (H * W * 7) 크기의 평탄화된 배열
    const TArray<float>& TexB_7d, // (H * W * 7) 크기의 평탄화된 배열
    const TArray<float>& TrajectorySamples, // (T * 7) 바이너리에서 로드한 궤적 데이터
    int32 T,                      // 궤적 길이
    float Alpha,
    TArray<float>& OutResult)     // 결과 저장용 (H * W * 7)
{
    const int32 TotalPixels = TexA_7d.Num() / 7;
    const FWeatheringSample* Traj = reinterpret_cast<const FWeatheringSample*>(TrajectorySamples.GetData());

    Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
    OutResult.SetNumUninitialized(TexA_7d.Num());

    for (int32 i = 0; i < TotalPixels; ++i)
    {
        const float* PixelA = &TexA_7d[i * 7];
        const float* PixelB = &TexB_7d[i * 7];

        // 1. Find Nearest Trajectory Indices
        int32 IdxA = 0;
        float MinDistA = MAX_flt;
        for (int32 t = 0; t < T; ++t)
        {
            float DistSq = 0;
            for (int c = 0; c < 7; ++c) DistSq += FMath::Square(Traj[t].Data[c] - PixelA[c]);
            if (DistSq < MinDistA) { MinDistA = DistSq; IdxA = t; }
        }

        int32 IdxB = 0;
        float MinDistB = MAX_flt;
        for (int32 t = 0; t < T; ++t)
        {
            float DistSq = 0;
            for (int c = 0; c < 7; ++c) DistSq += FMath::Square(Traj[t].Data[c] - PixelB[c]);
            if (DistSq < MinDistB) { MinDistB = DistSq; IdxB = t; }
        }

        // 2. Interpolate Index
        float InterpIdx = (float(IdxB) - float(IdxA)) * Alpha + float(IdxA);
        int32 ClampedIdx = FMath::Clamp(FMath::RoundToInt(InterpIdx), 0, T - 1);

        // 3. Bidirectional Semantic Transfer & Final Blend
        for (int32 c = 0; c < 7; ++c)
        {
            float SampleA = Traj[IdxA].Data[c];
            float SampleB = Traj[IdxB].Data[c];
            float SampleInterp = Traj[ClampedIdx].Data[c];

            float ResultA = PixelA[c] + (SampleInterp - SampleA);
            float ResultB = PixelB[c] - (SampleB - SampleInterp);

            float Reconstructed = (1.0f - Alpha) * ResultA + Alpha * ResultB;

            // Stabilization (Clamp)
            if (c < 6) OutResult[i * 7 + c] = FMath::Clamp(Reconstructed, 0.0f, 1.0f);
            else       OutResult[i * 7 + c] = FMath::Clamp(Reconstructed, 0.0f, 1.0f);
        }
    }
}


TArray<float> UMWSBlueprintFunctionLibrary::CombineTextureSources(
    UTexture2D* Base,
    UTexture2D* Spec,
    UTexture2D* Rough)
{
    TArray<float> EmptyResult;

    if (!Base || !Spec || !Rough)
    {
        return EmptyResult;
    }

    // ------------------------------------------------------------
    // Texture Source Validation
    // ------------------------------------------------------------

    if (!Base->Source.IsValid() ||
        !Spec->Source.IsValid() ||
        !Rough->Source.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid texture source."));
        return EmptyResult;
    }

    const int32 Width = Base->Source.GetSizeX();
    const int32 Height = Base->Source.GetSizeY();

    if (Spec->Source.GetSizeX() != Width ||
        Spec->Source.GetSizeY() != Height ||
        Rough->Source.GetSizeX() != Width ||
        Rough->Source.GetSizeY() != Height)
    {
        UE_LOG(LogTemp, Error, TEXT("Texture size mismatch."));
        return EmptyResult;
    }

    // ------------------------------------------------------------
    // Raw Data Load
    // ------------------------------------------------------------

    TArray64<uint8> BaseRaw;
    TArray64<uint8> SpecRaw;
    TArray64<uint8> RoughRaw;

    if (!Base->Source.GetMipData(BaseRaw, 0) ||
        !Spec->Source.GetMipData(SpecRaw, 0) ||
        !Rough->Source.GetMipData(RoughRaw, 0))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load mip data."));
        return EmptyResult;
    }

    const ETextureSourceFormat BaseFormat =
        Base->Source.GetFormat();

    const ETextureSourceFormat SpecFormat =
        Spec->Source.GetFormat();

    const ETextureSourceFormat RoughFormat =
        Rough->Source.GetFormat();

    const int32 PixelCount = Width * Height;

    // ------------------------------------------------------------
    // Final Tensor
    // ------------------------------------------------------------

    TArray<float> Result;

    Result.SetNumUninitialized(PixelCount * 7);

    // ------------------------------------------------------------
    // Parallel Tensor Assembly
    // ------------------------------------------------------------

    ParallelFor(PixelCount, [&](int32 i)
        {
            float BR = 0.0f;
            float BG = 0.0f;
            float BB = 0.0f;

            float SR = 0.0f;
            float SG = 0.0f;
            float SB = 0.0f;

            float Roughness = 0.0f;

            // --------------------------------------------------------
            // Base Color
            // --------------------------------------------------------

            switch (BaseFormat)
            {
            case TSF_BGRA8:
            {
                const uint8* Ptr =
                    BaseRaw.GetData() + i * 4;

                BB = Ptr[0] / 255.0f;
                BG = Ptr[1] / 255.0f;
                BR = Ptr[2] / 255.0f;
                break;
            }

            case TSF_G8:
            {
                const float V =
                    BaseRaw[i] / 255.0f;

                BR = V;
                BG = V;
                BB = V;
                break;
            }

            default:
                break;
            }

            // --------------------------------------------------------
            // Specular
            // --------------------------------------------------------

            switch (SpecFormat)
            {
            case TSF_BGRA8:
            {
                const uint8* Ptr =
                    SpecRaw.GetData() + i * 4;

                SB = Ptr[0] / 255.0f;
                SG = Ptr[1] / 255.0f;
                SR = Ptr[2] / 255.0f;
                break;
            }

            case TSF_G8:
            {
                const float V =
                    SpecRaw[i] / 255.0f;

                SR = V;
                SG = V;
                SB = V;
                break;
            }

            default:
                break;
            }

            // --------------------------------------------------------
            // Roughness
            // --------------------------------------------------------

            switch (RoughFormat)
            {
            case TSF_G8:
            {
                Roughness =
                    RoughRaw[i] / 255.0f;
                break;
            }

            case TSF_BGRA8:
            {
                Roughness =
                    RoughRaw[i * 4 + 2] / 255.0f;
                break;
            }

            default:
                break;
            }

            // --------------------------------------------------------
            // Final Tensor Write
            // --------------------------------------------------------

            const int32 Idx = i * 7;

            Result[Idx + 0] = BR;
            Result[Idx + 1] = BG;
            Result[Idx + 2] = BB;

            Result[Idx + 3] = SR;
            Result[Idx + 4] = SG;
            Result[Idx + 5] = SB;

            Result[Idx + 6] = Roughness;
        });

    UE_LOG(LogTemp, Log,
        TEXT("CombineTextureSources Success. PixelCount=%d TensorSize=%d"),
        PixelCount,
        Result.Num());

    return Result;
}


void UMWSBlueprintFunctionLibrary::BuildNearestTrajectoryCache(
    const TArray<float>& Texture7D,
    const TArray<float>& TrajectorySamples,
    int32 T,
    TArray<int32>& OutNearestIndices)
{
    const int32 TotalPixels = Texture7D.Num() / 7;

    OutNearestIndices.SetNumUninitialized(TotalPixels);

    const FWeatheringSample* Traj =
        reinterpret_cast<const FWeatheringSample*>(
            TrajectorySamples.GetData());

    for (int32 i = 0; i < TotalPixels; ++i)
    {
        const float* Pixel = &Texture7D[i * 7];

        int32 BestIdx = 0;
        float BestDist = MAX_flt;

        for (int32 t = 0; t < T; ++t)
        {
            const float* Sample = Traj[t].Data;

            float DistSq = 0.0f;

            for (int32 c = 0; c < 7; ++c)
            {
                const float D = Sample[c] - Pixel[c];
                DistSq += D * D;
            }

            if (DistSq < BestDist)
            {
                BestDist = DistSq;
                BestIdx = t;
            }
        }

        OutNearestIndices[i] = BestIdx;
    }

    UE_LOG(LogTemp, Log,
        TEXT("BuildNearestTrajectoryCache Complete. Pixels=%d"),
        TotalPixels);
}

void UMWSBlueprintFunctionLibrary::InterpolateWeatheringCached(
    const TArray<float>& TexA_7d,
    const TArray<float>& TexB_7d,
    const TArray<int32>& CachedNearestA,
    const TArray<int32>& CachedNearestB,
    const TArray<float>& TrajectorySamples,
    int32 T,
    float Alpha,
    TArray<float>& OutResult)
{
    const int32 TotalPixels = TexA_7d.Num() / 7;

    if (CachedNearestA.Num() != TotalPixels ||
        CachedNearestB.Num() != TotalPixels)
    {
        UE_LOG(LogTemp, Error,
            TEXT("Cached nearest index size mismatch."));
        return;
    }

    const FWeatheringSample* Traj =
        reinterpret_cast<const FWeatheringSample*>(
            TrajectorySamples.GetData());

    Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

    const float InvAlpha = 1.0f - Alpha;

    OutResult.SetNumUninitialized(TexA_7d.Num());

    for (int32 i = 0; i < TotalPixels; ++i)
    {
        const float* PixelA = &TexA_7d[i * 7];
        const float* PixelB = &TexB_7d[i * 7];

        const int32 IdxA = CachedNearestA[i];
        const int32 IdxB = CachedNearestB[i];

        const float InterpIdxFloat =
            float(IdxA) +
            (float(IdxB) - float(IdxA)) * Alpha;

        const int32 InterpIdx =
            FMath::Clamp(
                FMath::RoundToInt(InterpIdxFloat),
                0,
                T - 1);

        const float* SampleA = Traj[IdxA].Data;
        const float* SampleB = Traj[IdxB].Data;
        const float* SampleInterp = Traj[InterpIdx].Data;

        float* OutPixel = &OutResult[i * 7];

        for (int32 c = 0; c < 7; ++c)
        {
            const float ResultA =
                PixelA[c] +
                (SampleInterp[c] - SampleA[c]);

            const float ResultB =
                PixelB[c] -
                (SampleB[c] - SampleInterp[c]);

            const float Reconstructed =
                InvAlpha * ResultA +
                Alpha * ResultB;

            OutPixel[c] =
                FMath::Clamp(Reconstructed, 0.0f, 1.0f);
        }
    }
}

void UMWSBlueprintFunctionLibrary::ReconstructTexturesFromTensor(
    const TArray<float>& Tensor7D,
    int32 Width,
    int32 Height,
    UTexture2D*& OutBaseColor,
    UTexture2D*& OutSpecular,
    UTexture2D*& OutRoughness)
{
    OutBaseColor = nullptr;
    OutSpecular = nullptr;
    OutRoughness = nullptr;

    const int32 PixelCount = Width * Height;

    if (Tensor7D.Num() != PixelCount * 7)
    {
        UE_LOG(LogTemp, Error,
            TEXT("Tensor size mismatch."));
        return;
    }

    // ------------------------------------------------------------
    // Create Transient Textures
    // ------------------------------------------------------------

    OutBaseColor =
        UTexture2D::CreateTransient(
            Width,
            Height,
            PF_B8G8R8A8);

    OutSpecular =
        UTexture2D::CreateTransient(
            Width,
            Height,
            PF_B8G8R8A8);

    OutRoughness =
        UTexture2D::CreateTransient(
            Width,
            Height,
            PF_B8G8R8A8);

    if (!OutBaseColor ||
        !OutSpecular ||
        !OutRoughness)
    {
        UE_LOG(LogTemp, Error,
            TEXT("Failed to create transient textures."));
        return;
    }

    // ------------------------------------------------------------
    // Lock Bulk Data
    // ------------------------------------------------------------

    FTexture2DMipMap& BaseMip =
        OutBaseColor->GetPlatformData()->Mips[0];

    FTexture2DMipMap& SpecMip =
        OutSpecular->GetPlatformData()->Mips[0];

    FTexture2DMipMap& RoughMip =
        OutRoughness->GetPlatformData()->Mips[0];

    uint8* BaseData =
        static_cast<uint8*>(
            BaseMip.BulkData.Lock(LOCK_READ_WRITE));

    uint8* SpecData =
        static_cast<uint8*>(
            SpecMip.BulkData.Lock(LOCK_READ_WRITE));

    uint8* RoughData =
        static_cast<uint8*>(
            RoughMip.BulkData.Lock(LOCK_READ_WRITE));

    // ------------------------------------------------------------
    // Tensor → Texture Reconstruction
    // ------------------------------------------------------------

    ParallelFor(PixelCount, [&](int32 i)
        {
            const int32 TensorIdx = i * 7;
            const int32 PixelIdx = i * 4;

            // --------------------------------------------------------
            // BaseColor
            // --------------------------------------------------------

            const uint8 BR =
                uint8(FMath::Clamp(
                    Tensor7D[TensorIdx + 0] * 255.0f,
                    0.0f,
                    255.0f));

            const uint8 BG =
                uint8(FMath::Clamp(
                    Tensor7D[TensorIdx + 1] * 255.0f,
                    0.0f,
                    255.0f));

            const uint8 BB =
                uint8(FMath::Clamp(
                    Tensor7D[TensorIdx + 2] * 255.0f,
                    0.0f,
                    255.0f));

            BaseData[PixelIdx + 0] = BB;
            BaseData[PixelIdx + 1] = BG;
            BaseData[PixelIdx + 2] = BR;
            BaseData[PixelIdx + 3] = 255;

            // --------------------------------------------------------
            // Specular
            // --------------------------------------------------------

            const uint8 SR =
                uint8(FMath::Clamp(
                    Tensor7D[TensorIdx + 3] * 255.0f,
                    0.0f,
                    255.0f));

            const uint8 SG =
                uint8(FMath::Clamp(
                    Tensor7D[TensorIdx + 4] * 255.0f,
                    0.0f,
                    255.0f));

            const uint8 SB =
                uint8(FMath::Clamp(
                    Tensor7D[TensorIdx + 5] * 255.0f,
                    0.0f,
                    255.0f));

            SpecData[PixelIdx + 0] = SB;
            SpecData[PixelIdx + 1] = SG;
            SpecData[PixelIdx + 2] = SR;
            SpecData[PixelIdx + 3] = 255;

            // --------------------------------------------------------
            // Roughness
            // --------------------------------------------------------

            const uint8 R =
                uint8(FMath::Clamp(
                    Tensor7D[TensorIdx + 6] * 255.0f,
                    0.0f,
                    255.0f));

            RoughData[PixelIdx + 0] = R;
            RoughData[PixelIdx + 1] = R;
            RoughData[PixelIdx + 2] = R;
            RoughData[PixelIdx + 3] = 255;
        });

    // ------------------------------------------------------------
    // Unlock
    // ------------------------------------------------------------

    BaseMip.BulkData.Unlock();
    SpecMip.BulkData.Unlock();
    RoughMip.BulkData.Unlock();

    // ------------------------------------------------------------
    // Upload To GPU
    // ------------------------------------------------------------

    OutBaseColor->UpdateResource();
    OutSpecular->UpdateResource();
    OutRoughness->UpdateResource();

    // ------------------------------------------------------------
    // Texture Settings
    // ------------------------------------------------------------

    OutBaseColor->SRGB = true;
    OutSpecular->SRGB = false;
    OutRoughness->SRGB = false;

    UE_LOG(LogTemp, Log,
        TEXT("ReconstructTexturesFromTensor Success."));
}

void UMWSBlueprintFunctionLibrary::ApplyWeatheringTexturesToMesh(
    UMeshComponent* MeshComponent,
    int32 MaterialIndex,
    UTexture2D* BaseTexture,
    UTexture2D* SpecTexture,
    UTexture2D* RoughTexture,
    FName BaseParamName,
    FName SpecParamName,
    FName RoughParamName,
    UMaterialInstanceDynamic*& OutMID)
{
    OutMID = nullptr;

    if (!MeshComponent)
    {
        UE_LOG(LogTemp, Error,
            TEXT("MeshComponent is null."));
        return;
    }

    if (!BaseTexture ||
        !SpecTexture ||
        !RoughTexture)
    {
        UE_LOG(LogTemp, Error,
            TEXT("Texture input is invalid."));
        return;
    }

    // ------------------------------------------------------------
    // Create Dynamic Material
    // ------------------------------------------------------------

    UMaterialInterface* BaseMaterial =
        MeshComponent->GetMaterial(MaterialIndex);

    if (!BaseMaterial)
    {
        UE_LOG(LogTemp, Error,
            TEXT("Material is null."));
        return;
    }

    UMaterialInstanceDynamic* MID =
        MeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(
            MaterialIndex,
            BaseMaterial);

    if (!MID)
    {
        UE_LOG(LogTemp, Error,
            TEXT("Failed to create MID."));
        return;
    }

    // ------------------------------------------------------------
    // Apply Textures
    // ------------------------------------------------------------

    MID->SetTextureParameterValue(
        BaseParamName,
        BaseTexture);

    MID->SetTextureParameterValue(
        SpecParamName,
        SpecTexture);

    MID->SetTextureParameterValue(
        RoughParamName,
        RoughTexture);

    OutMID = MID;

    UE_LOG(LogTemp, Log,
        TEXT("Weathering textures applied."));
}