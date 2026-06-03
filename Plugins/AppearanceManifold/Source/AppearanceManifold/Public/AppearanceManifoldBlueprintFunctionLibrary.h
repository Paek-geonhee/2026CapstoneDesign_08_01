// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AppearanceManifoldBlueprintFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class APPEARANCEMANIFOLD_API UAppearanceManifoldBlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

#pragma region Utility 
    UFUNCTION(BlueprintPure, Category = "AppearanceManifold|Runtime|Utility")
    static FString GetTrajectorySaveDirectory(const FString& FileName);

    /** .bin ������ �о� trajectory_samples �����͸� �α׷� ��� */
    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Utility")
    static void LogWeatheringBinaryData(const FString& FilePath);

   // static void 
    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Utility")
    static TArray<float> LoadWeatheringBinaryData(const FString& FilePath, int32& OutT);

    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Utility")
    static TArray<float> LoadWeatheringTensorsFromFiles(const FString& DirectoryPath);

    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Utility")
    static bool LoadRawPNGData(const FString& FilePath, TArray<uint8>& OutData, int32& OutWidth, int32& OutHeight);

    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Utility")
    static FString SaveFloatArrayToBinary(const TArray<float>& Data, const FString& ActorName, int32 Index);

    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Utility")
    static bool LoadFloatArrayFromBinary(const FString& FilePath, TArray<float>& OutData);
#pragma endregion

#pragma region Weathering

    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Weathering")
    static void InterpolateWeatheringCached(
        const TArray<float>& TexA_7d,
        const TArray<float>& TexB_7d,
        const TArray<int32>& CachedNearestA,
        const TArray<int32>& CachedNearestB,
        const TArray<float>& TrajectorySamples,
        int32 T,
        float Alpha,
        TArray<float>& OutResult);


    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Weathering")
    static void BuildNearestTrajectoryCache(
        const TArray<float>& Texture7D,
        const TArray<float>& TrajectorySamples,
        int32 T,
        TArray<int32>& OutNearestIndices);

#pragma endregion

#pragma region Texture

    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Texture")
    static TArray<float> CombineTextureSources(
        UTexture2D* Base,
        UTexture2D* Spec,
        UTexture2D* Rough);



    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Texture")
    static void ReconstructTexturesFromTensor(
        const TArray<float>& Tensor7D,
        int32 Width,
        int32 Height,

        UTexture2D* ExistingBaseColor,
        UTexture2D* ExistingSpecular,
        UTexture2D* ExistingRoughness,

        UTexture2D*& OutBaseColor,
        UTexture2D*& OutSpecular,
        UTexture2D*& OutRoughness);


    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Texture")
    static void ApplyWeatheringTexturesToMesh(
        UMeshComponent* MeshComponent,
        int32 MaterialIndex,
        UTexture2D* BaseTexture,
        UTexture2D* SpecTexture,
        UTexture2D* RoughTexture,
        FName BaseParamName,
        FName SpecParamName,
        FName RoughParamName,
        UMaterialInstanceDynamic* ExistingMID,
        UMaterialInstanceDynamic*& OutMID);

#pragma endregion

#pragma region EDITOR_ONLY

    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Editor|Utility")
    static bool ExportTextureToPNG(
        UTexture2D* Texture,
        const FString& FilePath
    );


    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Editor|Utility")
    static UTexture2D* ImportTextureFromFile(const FString& FilePath);

    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Editor|Utility")
    static FString GetAppearanceManifoldDirectory();

    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Editor|Python")
    static bool ExecutePythonCommand(
        const FString& PythonCommand
    );

    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Editor|Pipeline")
    static bool RunWeatheringPipeline(
        UTexture2D* BaseColor,
        UTexture2D* Specular,
        UTexture2D* Roughness,
        const FString& WorkingDirectory,
        const FString& FileName
    );

    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Editor|Pipeline")
    static TArray<UTexture2D*> RunWeatheringInterpolation(
        UTexture2D* BaseColorA,
        UTexture2D* SpecularA,
        UTexture2D* RoughnessA,
        UTexture2D* BaseColorB,
        UTexture2D* SpecularB,
        UTexture2D* RoughnessB,
        float alpha,
        const FString& WorkingDirectory);

#pragma endregion



};


