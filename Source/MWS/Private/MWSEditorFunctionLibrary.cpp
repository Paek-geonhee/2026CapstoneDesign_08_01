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

bool UMWSEditorFunctionLibrary::ExportTextureToPNG(
    UTexture2D* Texture,
    const FString& FilePath)
{
    if (!Texture)
    {
        return false;
    }

#if WITH_EDITOR

    return UExporter::ExportToFile(
        Texture,
        nullptr,
        *FilePath,
        false,
        false
    ) != 0;
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
    const FString& WorkingDirectory)
{
    if (!BaseColor || !Specular || !Roughness)
    {
        return false;
    }

    const FString BaseColorPath =
        WorkingDirectory / TEXT("BaseColor.png");

    const FString SpecularPath =
        WorkingDirectory / TEXT("Specular.png");

    const FString RoughnessPath =
        WorkingDirectory / TEXT("Roughness.png");

    const bool bBaseExport =
        ExportTextureToPNG(BaseColor, BaseColorPath);

    const bool bSpecExport =
        ExportTextureToPNG(Specular, SpecularPath);

    const bool bRoughExport =
        ExportTextureToPNG(Roughness, RoughnessPath);

    if (!bBaseExport || !bSpecExport || !bRoughExport)
    {
        return false;
    }

    FString PythonCommand = FString::Printf(
        TEXT("import MainManager; ")
        TEXT("MainManager.WeatheringPipeline.start_weathering([")
        TEXT("r'%s', ")
        TEXT("r'%s', ")
        TEXT("r'%s'])"),
        *BaseColorPath,
        *SpecularPath,
        *RoughnessPath
    );

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
    const FString OutputDir =
        FPaths::ProjectContentDir() /
        TEXT("03Python/AppearanceManifold/");

    IPlatformFile& PlatformFile =
        FPlatformFileManager::Get().GetPlatformFile();

    PlatformFile.CreateDirectoryTree(*OutputDir);

    return OutputDir;
}