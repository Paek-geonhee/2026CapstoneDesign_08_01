// Fill out your copyright notice in the Description page of Project Settings.


#include "AppearanceManifoldBlueprintFunctionLibrary.h"

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
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

struct FWeatheringSample {
    float Data[7];
};

FString UAppearanceManifoldBlueprintFunctionLibrary::GetTrajectorySaveDirectory(const FString& FileName)
{
    
    FString Directory = FPaths::Combine(FPaths::ProjectContentDir(),TEXT("_WeatheringResults"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    return FPaths::Combine(Directory, FileName + TEXT(".bin"));
}

void UAppearanceManifoldBlueprintFunctionLibrary::LogWeatheringBinaryData(const FString& FilePath)
{
    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Failed to load file: %s"), *FilePath);
        return;
    }


    if (FileData.Num() < 4)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ File is too small to contain header."));
        return;
    }

    int32 T = *reinterpret_cast<int32*>(FileData.GetData());


    float* SamplesData = reinterpret_cast<float*>(FileData.GetData() + 4);

    UE_LOG(LogTemp, Log, TEXT("✅ Binary Load Success. Trajectory Length (T): %d"), T);

    int32 NumToPrint = FMath::Min(5, T);
    for (int32 i = 0; i < NumToPrint; ++i)
    {
        float* row = &SamplesData[i * 7];
        UE_LOG(LogTemp, Log, TEXT("   Point %d: [%.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f]"),
            i, row[0], row[1], row[2], row[3], row[4], row[5], row[6]);
    }

    if (T > 5)
    {
        float* last = &SamplesData[(T - 1) * 7];
        UE_LOG(LogTemp, Log, TEXT("   ... Last Point: [%.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f]"),
            last[0], last[1], last[2], last[3], last[4], last[5], last[6]);
    }
}


FString UAppearanceManifoldBlueprintFunctionLibrary::SaveFloatArrayToBinary(
    const TArray<float>& Data,
    const FString& ActorName,
    int32 Index)
{
    const FString SaveDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TensorCache"));

    IFileManager::Get().MakeDirectory(*SaveDirectory, true);

    const FString FileName = FString::Printf(TEXT("%s_%d.bin"), *ActorName, Index);

    const FString FullPath = FPaths::Combine(SaveDirectory, FileName);

    FBufferArchive Archive;
    Archive << const_cast<TArray<float>&>(Data);

    const bool bSaved = FFileHelper::SaveArrayToFile(Archive, *FullPath);

    Archive.FlushCache();
    Archive.Empty();

    return bSaved ? FullPath : FString();
}

bool UAppearanceManifoldBlueprintFunctionLibrary::LoadFloatArrayFromBinary(
    const FString& FilePath,
    TArray<float>& OutData)
{
    TArray<uint8> BinaryData;

    if (!FFileHelper::LoadFileToArray(BinaryData, *FilePath))
    {
        return false;
    }

    FMemoryReader Reader(BinaryData, true);

    Reader.Seek(0);
    Reader << OutData;
    Reader.Close();

    return true;
}

TArray<float> UAppearanceManifoldBlueprintFunctionLibrary::LoadWeatheringBinaryData(const FString& FilePath, int32& OutT)
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


void UAppearanceManifoldBlueprintFunctionLibrary::InterpolateWeatheringCached(
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

        //////////////// Unrolling ///////////////////
        const float ResultA0 = PixelA[0] + (SampleInterp[0] - SampleA[0]);
        const float ResultB0 = PixelB[0] - (SampleB[0] - SampleInterp[0]);
        OutPixel[0] = FMath::Clamp(InvAlpha * ResultA0 + Alpha * ResultB0, 0.0f, 1.0f);

        const float ResultA1 = PixelA[1] + (SampleInterp[1] - SampleA[1]);
        const float ResultB1 = PixelB[1] - (SampleB[1] - SampleInterp[1]);
        OutPixel[1] = FMath::Clamp(InvAlpha * ResultA1 + Alpha * ResultB1, 0.0f, 1.0f);

        const float ResultA2 = PixelA[2] + (SampleInterp[2] - SampleA[2]);
        const float ResultB2 = PixelB[2] - (SampleB[2] - SampleInterp[2]);
        OutPixel[2] = FMath::Clamp(InvAlpha * ResultA2 + Alpha * ResultB2, 0.0f, 1.0f);

        const float ResultA3 = PixelA[3] + (SampleInterp[3] - SampleA[3]);
        const float ResultB3 = PixelB[3] - (SampleB[3] - SampleInterp[3]);
        OutPixel[3] = FMath::Clamp(InvAlpha * ResultA3 + Alpha * ResultB3, 0.0f, 1.0f);

        const float ResultA4 = PixelA[4] + (SampleInterp[4] - SampleA[4]);
        const float ResultB4 = PixelB[4] - (SampleB[4] - SampleInterp[4]);
        OutPixel[4] = FMath::Clamp(InvAlpha * ResultA4 + Alpha * ResultB4, 0.0f, 1.0f);

        const float ResultA5 = PixelA[5] + (SampleInterp[5] - SampleA[5]);
        const float ResultB5 = PixelB[5] - (SampleB[5] - SampleInterp[5]);
        OutPixel[5] = FMath::Clamp(InvAlpha * ResultA5 + Alpha * ResultB5, 0.0f, 1.0f);

        const float ResultA6 = PixelA[6] + (SampleInterp[6] - SampleA[6]);
        const float ResultB6 = PixelB[6] - (SampleB[6] - SampleInterp[6]);
        OutPixel[6] = FMath::Clamp(InvAlpha * ResultA6 + Alpha * ResultB6, 0.0f, 1.0f);
        //////////////// ////////////// ///////////////////
    }
}

TArray<float> UAppearanceManifoldBlueprintFunctionLibrary::LoadWeatheringTensorsFromFiles(const FString& DirectoryPath)
{
    TArray<FString> FoundFiles;
    IFileManager::Get().FindFiles(FoundFiles, *DirectoryPath, TEXT("*.png"));

    FString BasePath, SpecPath, RoughPath;

    // 파일명 접미사 확인 로직
    for (const FString& FileName : FoundFiles)
    {
        if (FileName.Contains(TEXT("_basecolor.png"))) BasePath = DirectoryPath / FileName;
        else if (FileName.Contains(TEXT("_specular.png"))) SpecPath = DirectoryPath / FileName;
        else if (FileName.Contains(TEXT("_roughness.png"))) RoughPath = DirectoryPath / FileName;
    }

    // 필수 파일 검증
    if (BasePath.IsEmpty() || SpecPath.IsEmpty() || RoughPath.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("❌ 필수 텍스처 파일(_basecolor, _specular, _roughness)을 찾을 수 없습니다."));
        return TArray<float>();
    }
    UE_LOG(LogTemp, Warning, TEXT("❌ 필수 텍스처 파일(%s, %s, %s)를 찾았습니다."), *BasePath, *SpecPath, *RoughPath);

    TArray<uint8> BaseRaw, SpecRaw, RoughRaw;
    int32 W1, H1, W2, H2, W3, H3;

    if (!LoadRawPNGData(BasePath, BaseRaw, W1, H1) ||
        !LoadRawPNGData(SpecPath, SpecRaw, W2, H2) ||
        !LoadRawPNGData(RoughPath, RoughRaw, W3, H3))
    {
        return TArray<float>();
    }

    // [신규] 해상도 일치 검증
    if (W1 != W2 || W1 != W3 || H1 != H2 || H1 != H3)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ 텍스처 해상도가 서로 다릅니다."));
        return TArray<float>();
    }

    int32 PixelCount = W1 * H1;
    TArray<float> Result;
    Result.SetNumUninitialized(PixelCount * 7);

    ParallelFor(PixelCount, [&](int32 i)
        {
            const int32 PtrIdx = i * 4;
            const int32 TensorIdx = i * 7;

            // Base (BGRA -> RGB)
            Result[TensorIdx + 0] = BaseRaw[PtrIdx + 2] / 255.0f; // R
            Result[TensorIdx + 1] = BaseRaw[PtrIdx + 1] / 255.0f; // G
            Result[TensorIdx + 2] = BaseRaw[PtrIdx + 0] / 255.0f; // B

            // Spec (BGRA -> RGB)
            Result[TensorIdx + 3] = SpecRaw[PtrIdx + 2] / 255.0f;
            Result[TensorIdx + 4] = SpecRaw[PtrIdx + 1] / 255.0f;
            Result[TensorIdx + 5] = SpecRaw[PtrIdx + 0] / 255.0f;

            // Rough
            Result[TensorIdx + 6] = RoughRaw[PtrIdx + 1] / 255.0f;
        });

    return Result;
}

bool UAppearanceManifoldBlueprintFunctionLibrary::LoadRawPNGData(const FString& FilePath, TArray<uint8>& OutData, int32& OutWidth, int32& OutHeight)
{
    TArray<uint8> FileContent;
    if (!FFileHelper::LoadFileToArray(FileContent, *FilePath)) return false;

    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

    if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(FileContent.GetData(), FileContent.Num()))
    {
        OutWidth = ImageWrapper->GetWidth();
        OutHeight = ImageWrapper->GetHeight();
        return ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, OutData);
    }
    return false;
}

TArray<float> UAppearanceManifoldBlueprintFunctionLibrary::CombineTextureSources(
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


void UAppearanceManifoldBlueprintFunctionLibrary::BuildNearestTrajectoryCache(
    const TArray<float>& Texture7D,
    const TArray<float>& TrajectorySamples,
    int32 T,
    TArray<int32>& OutNearestIndices)
{
    double Start = FPlatformTime::Seconds();
    const int32 TotalPixels = Texture7D.Num() / 7;

    OutNearestIndices.SetNumUninitialized(TotalPixels);

    const FWeatheringSample* Traj =
        reinterpret_cast<const FWeatheringSample*>(
            TrajectorySamples.GetData());

    ParallelFor(TotalPixels, [&](int32 i)
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
        });
    double End = FPlatformTime::Seconds();
    UE_LOG(LogTemp, Warning,
        TEXT("Pixels=%d T=%d"),
        TotalPixels,
        T);
    UE_LOG(LogTemp, Warning,
        TEXT("BuildNearestTrajectoryCache %.3f ms"),
        (End - Start) * 1000.0);
}

void UAppearanceManifoldBlueprintFunctionLibrary::ReconstructTexturesFromTensor(
    const TArray<float>& Tensor7D,
    int32 Width,
    int32 Height,

    UTexture2D* ExistingBaseColor,
    UTexture2D* ExistingSpecular,
    UTexture2D* ExistingRoughness,

    UTexture2D*& OutBaseColor,
    UTexture2D*& OutSpecular,
    UTexture2D*& OutRoughness)
{
    const int32 PixelCount = Width * Height;
    if (Tensor7D.Num() != PixelCount * 7) return;

    auto CreateTex = [&](bool bSRGB) {
        UTexture2D* Tex = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
        Tex->AddToRoot();

        // 1. SourceInit을 사용하여 엔진이 버퍼를 안전하게 할당하도록 함
        Tex->Source.Init(Width, Height, 1, 1, TSF_BGRA8);
        Tex->SRGB = bSRGB;
        Tex->CompressionSettings = bSRGB ? TC_Default : TC_Masks;
        return Tex;
        };

    if (!ExistingBaseColor) OutBaseColor = CreateTex(true);
    else                    OutBaseColor = ExistingBaseColor;

    if (!ExistingSpecular) OutSpecular = CreateTex(false);
    else                   OutSpecular = ExistingSpecular;

    if (!ExistingRoughness) OutRoughness = CreateTex(false);
    else                    OutRoughness = ExistingRoughness;
        

    // 2. Source 데이터를 직접 수정 (BulkData.Lock 대신 Mip.BulkData 사용)
    auto WriteToTexture = [&](UTexture2D* Tex, int32 ChannelOffset, bool bIsRoughness) {
        uint8* RawData = Tex->Source.LockMip(0); // 엔진이 보장하는 안전한 Lock

        ParallelFor(PixelCount, [&](int32 i) {
            int32 PIdx = i * 4;
            if (bIsRoughness) {
                uint8 R = uint8(FMath::Clamp(Tensor7D[i * 7 + 6] * 255.0f, 0.0f, 255.0f));
                RawData[PIdx + 0] = RawData[PIdx + 1] = RawData[PIdx + 2] = R;
            }
            else {
                int32 TIdx = i * 7 + ChannelOffset;
                RawData[PIdx + 0] = uint8(FMath::Clamp(Tensor7D[TIdx + 2] * 255.0f, 0.0f, 255.0f));
                RawData[PIdx + 1] = uint8(FMath::Clamp(Tensor7D[TIdx + 1] * 255.0f, 0.0f, 255.0f));
                RawData[PIdx + 2] = uint8(FMath::Clamp(Tensor7D[TIdx + 0] * 255.0f, 0.0f, 255.0f));
            }
            RawData[PIdx + 3] = 255;
            });

        Tex->Source.UnlockMip(0);
        // [중요] 컴파일을 트리거하지 않고 에셋 상태만 갱신
        //Tex->UpdateResource();
        };

    WriteToTexture(OutBaseColor, 0, false);
    WriteToTexture(OutSpecular, 3, false);
    WriteToTexture(OutRoughness, 0, true);

    OutBaseColor->UpdateResource();
    OutSpecular->UpdateResource();
    OutRoughness->UpdateResource();


    auto LogTextureSource = [](const TCHAR* Name, UTexture2D* Tex)
        {
            TArray64<uint8> RawData;
            Tex->Source.GetMipData(RawData, 0);

            UE_LOG(LogTemp, Warning,
                TEXT("%s RawData Num=%lld"),
                Name,
                RawData.Num());

            if (RawData.Num() >= 4)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("%s Pixel0=%d %d %d %d"),
                    Name,
                    RawData[0],
                    RawData[1],
                    RawData[2],
                    RawData[3]);
            }
        };

    LogTextureSource(TEXT("BC"), OutBaseColor);
    LogTextureSource(TEXT("SP"), OutSpecular);
    LogTextureSource(TEXT("RG"), OutRoughness);
}


void UAppearanceManifoldBlueprintFunctionLibrary::ApplyWeatheringTexturesToMesh(
    UMeshComponent* MeshComponent,
    int32 MaterialIndex,
    UTexture2D* BaseTexture,
    UTexture2D* SpecTexture,
    UTexture2D* RoughTexture,
    FName BaseParamName,
    FName SpecParamName,
    FName RoughParamName,
    UMaterialInstanceDynamic* ExistingMID,
    UMaterialInstanceDynamic*& OutMID)
{
    OutMID = nullptr;

    if (!MeshComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("MeshComponent is null."));
        return;
    }

    if (!BaseTexture || !SpecTexture || !RoughTexture)
    {
        UE_LOG(LogTemp, Error, TEXT("Texture input is invalid."));
        return;
    }

    // ------------------------------------------------------------
    // Create Dynamic Material
    // ------------------------------------------------------------

    UMaterialInterface* BaseMaterial = MeshComponent->GetMaterial(MaterialIndex);

    if (!BaseMaterial)
    {
        UE_LOG(LogTemp, Error,TEXT("Material is null."));
        return;
    }

    UMaterialInstanceDynamic* MID = ExistingMID;

    if (!MID)
    {
        MID = MeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(MaterialIndex, BaseMaterial);
        UE_LOG(LogTemp, Warning, TEXT("MID Created"));
    }

    if (!MID)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create MID."));
        return;
    }

    // ------------------------------------------------------------
    // Apply Textures
    // ------------------------------------------------------------

    MID->SetTextureParameterValue(BaseParamName, BaseTexture);

    MID->SetTextureParameterValue(SpecParamName, SpecTexture);

    MID->SetTextureParameterValue(RoughParamName, RoughTexture);

    OutMID = MID;

    UE_LOG(LogTemp, Log, TEXT("Weathering textures applied."));
}




#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"

#include "Engine/Texture2D.h"

#if WITH_EDITOR
#include "Exporters/Exporter.h"
#include "IPythonScriptPlugin.h"
#endif

#include "ImageUtils.h"


bool UAppearanceManifoldBlueprintFunctionLibrary::ExportTextureToPNG(UTexture2D* Texture, const FString& FilePath)
{
    if (!Texture) return false;

    UE_LOG(LogTemp, Warning,
        TEXT("Texture=%p PlatformData=%p"),
        Texture,
        Texture->GetPlatformData());

    if (Texture->GetPlatformData())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("MipCount=%d"),
            Texture->GetPlatformData()->Mips.Num());
    }
    // check for PlatformData=0000000000000000
#if WITH_EDITOR
    // �ؽ�ó�� �����͸� ���� �� �ֵ��� ���� ����ȭ ���� Ȯ��


    checkf(Texture->GetPlatformData(), TEXT("No Platform data"));

    UE_LOG(LogTemp, Warning,
        TEXT("Mips=%d"),
        Texture->GetPlatformData()->Mips.Num());
    // check for Mips=0

    // Transient �ؽ�ó���� Mip[0] �����͸� FImage�� ���� ����
    FImage Image;

    const int32 Width = Texture->Source.GetSizeX();
    const int32 Height = Texture->Source.GetSizeY();

    Image.Init(
        Width,
        Height,
        ERawImageFormat::BGRA8,
        EGammaSpace::sRGB);

    TArray64<uint8> RawData;
    Texture->Source.GetMipData(RawData, 0);

    if (RawData.Num() != Width * Height * 4)
    {
        UE_LOG(LogTemp, Error,
            TEXT("Invalid source data. Expected=%d Actual=%lld"),
            Width * Height * 4,
            RawData.Num());

        return false;
    }

    FMemory::Memcpy(
        Image.AsBGRA8().GetData(),
        RawData.GetData(),
        RawData.Num());

    return FImageUtils::SaveImageByExtension(
        *FilePath,
        FImageView(Image));
    // return true;
#else
    return false;
#endif
}

bool UAppearanceManifoldBlueprintFunctionLibrary::ExecutePythonCommand(
    const FString& PythonCommand)
{
#if WITH_EDITOR

    IPythonScriptPlugin::Get()->ExecPythonCommand(
        *PythonCommand
    );

    return true;

#else

    return false;

#endif
}

bool UAppearanceManifoldBlueprintFunctionLibrary::RunWeatheringPipeline(
    UTexture2D* BaseColor,
    UTexture2D* Specular,
    UTexture2D* Roughness,
    const FString& WorkingDirectory,
    const FString& FileName)
{
    if (!BaseColor || !Specular || !Roughness)
    {
        UE_LOG(LogTemp, Warning, TEXT("Texture Invalid"));
        return false;
    }


    const FString BaseColorPath =
        WorkingDirectory / TEXT("BaseColor.png");

    const FString SpecularPath =
        WorkingDirectory / TEXT("Specular.png");

    const FString RoughnessPath =
        WorkingDirectory / TEXT("Roughness.png");

    IFileManager::Get().Delete(*BaseColorPath);
    IFileManager::Get().Delete(*SpecularPath);
    IFileManager::Get().Delete(*RoughnessPath);

    const bool bBaseExport =
        ExportTextureToPNG(BaseColor, BaseColorPath);

    const bool bSpecExport =
        ExportTextureToPNG(Specular, SpecularPath);

    const bool bRoughExport =
        ExportTextureToPNG(Roughness, RoughnessPath);

    if (!bBaseExport || !bSpecExport || !bRoughExport)
    {
        UE_LOG(LogTemp, Warning, TEXT("Exported Invalid"));
        return false;
    }

    FString PythonCommand = FString::Printf(
        TEXT("import MainManager; ")
        TEXT("MainManager.WeatheringPipeline.start_weathering([")
        TEXT("r'%s', ")
        TEXT("r'%s', ")
        TEXT("r'%s'], ")
        TEXT("r'%s')"),
        *BaseColorPath,
        *SpecularPath,
        *RoughnessPath,
        *FileName
    );


    UE_LOG(LogTemp, Warning,
        TEXT("BC Exists=%d Size=%lld"),
        IFileManager::Get().FileExists(*BaseColorPath),
        IFileManager::Get().FileSize(*BaseColorPath));

    UE_LOG(LogTemp, Warning,
        TEXT("SP Exists=%d Size=%lld"),
        IFileManager::Get().FileExists(*SpecularPath),
        IFileManager::Get().FileSize(*SpecularPath));

    UE_LOG(LogTemp, Warning,
        TEXT("RG Exists=%d Size=%lld"),
        IFileManager::Get().FileExists(*RoughnessPath),
        IFileManager::Get().FileSize(*RoughnessPath));

    return ExecutePythonCommand(PythonCommand);
}

TArray<UTexture2D*> UAppearanceManifoldBlueprintFunctionLibrary::RunWeatheringInterpolation(UTexture2D* BaseColorA, UTexture2D* SpecularA, UTexture2D* RoughnessA, UTexture2D* BaseColorB, UTexture2D* SpecularB, UTexture2D* RoughnessB, float alpha, const FString& WorkingDirectory)
{
    TArray<UTexture2D*> Result;
    if (!BaseColorA || !SpecularA || !RoughnessA || !BaseColorB || !SpecularB || !RoughnessB)
    {
        return Result;
    }

    const FString BaseColorAPath =
        WorkingDirectory / TEXT("BaseColorA.png");

    const FString SpecularAPath =
        WorkingDirectory / TEXT("SpecularA.png");

    const FString RoughnessAPath =
        WorkingDirectory / TEXT("RoughnessA.png");

    const bool bBaseExportA =
        ExportTextureToPNG(BaseColorA, BaseColorAPath);

    const bool bSpecExportA =
        ExportTextureToPNG(SpecularA, SpecularAPath);

    const bool bRoughExportA =
        ExportTextureToPNG(RoughnessA, RoughnessAPath);

    if (!bBaseExportA || !bSpecExportA || !bRoughExportA)
    {
        return Result;
    }

    const FString BaseColorBPath =
        WorkingDirectory / TEXT("BaseColorB.png");

    const FString SpecularBPath =
        WorkingDirectory / TEXT("SpecularB.png");

    const FString RoughnessBPath =
        WorkingDirectory / TEXT("RoughnessB.png");

    const bool bBaseExportB =
        ExportTextureToPNG(BaseColorB, BaseColorBPath);

    const bool bSpecExportB =
        ExportTextureToPNG(SpecularB, SpecularBPath);

    const bool bRoughExportB =
        ExportTextureToPNG(RoughnessB, RoughnessBPath);

    if (!bBaseExportB || !bSpecExportB || !bRoughExportB)
    {
        return Result;
    }

    FString PythonCommand = FString::Printf(
        TEXT("import MainManager; ")
        TEXT("MainManager.WeatheringPipeline.run_interpolation(")
        TEXT("[r'%s', r'%s', r'%s'], ")
        TEXT("[r'%s', r'%s', r'%s'], ")
        TEXT("%f, ")
        TEXT("r'%s')"),

        *BaseColorAPath,
        *SpecularAPath,
        *RoughnessAPath,

        *BaseColorBPath,
        *SpecularBPath,
        *RoughnessBPath,

        alpha,

        *WorkingDirectory
    );

    return Result;
}


UTexture2D* UAppearanceManifoldBlueprintFunctionLibrary::ImportTextureFromFile(const FString& FilePath)
{
    return FImageUtils::ImportFileAsTexture2D(FilePath);
}

FString UAppearanceManifoldBlueprintFunctionLibrary::GetAppearanceManifoldDirectory()
{
    FString Directory = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("_WeatheringResults"));

    IFileManager::Get().MakeDirectory(*Directory, true);

    return Directory;
}
