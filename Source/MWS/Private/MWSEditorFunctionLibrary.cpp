// Fill out your copyright notice in the Description page of Project Settings.


#include "MWSEditorFunctionLibrary.h"

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


bool UMWSEditorFunctionLibrary::ExportTextureToPNG(UTexture2D* Texture, const FString& FilePath)
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
    // 텍스처가 데이터를 가질 수 있도록 강제 동기화 상태 확인


    checkf(Texture->GetPlatformData(), TEXT("No Platform data"));

    UE_LOG(LogTemp, Warning,
        TEXT("Mips=%d"),
        Texture->GetPlatformData()->Mips.Num());
    // check for Mips=0

    // Transient 텍스처에서 Mip[0] 데이터를 FImage로 직접 복사
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

bool UMWSEditorFunctionLibrary::ExecutePythonCommand(
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

bool UMWSEditorFunctionLibrary::RunWeatheringPipeline(
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

TArray<UTexture2D*> UMWSEditorFunctionLibrary::RunWeatheringInterpolation(UTexture2D* BaseColorA, UTexture2D* SpecularA, UTexture2D* RoughnessA, UTexture2D* BaseColorB, UTexture2D* SpecularB, UTexture2D* RoughnessB, float alpha, const FString& WorkingDirectory)
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


UTexture2D* UMWSEditorFunctionLibrary::ImportTextureFromFile(
    const FString& FilePath)
{
    return FImageUtils::ImportFileAsTexture2D(
        FilePath
    );
}

FString UMWSEditorFunctionLibrary::GetAppearanceManifoldDirectory()
{
    FString Directory = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("_WeatheringResults"));

    IFileManager::Get().MakeDirectory( *Directory, true);

    return Directory;
}