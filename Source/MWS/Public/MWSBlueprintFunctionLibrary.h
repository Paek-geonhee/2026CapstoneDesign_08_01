// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MWSBlueprintFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class MWS_API UMWSBlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category = "Weathering")
    static FString GetTrajectorySaveDirectory(const FString& FileName);

    /** .bin 파일을 읽어 trajectory_samples 데이터를 로그로 출력 */
    UFUNCTION(BlueprintCallable, Category = "Weathering")
    static void LogWeatheringBinaryData(const FString& FilePath);

   // static void 
    UFUNCTION(BlueprintCallable, Category = "Weathering")
    static TArray<float> LoadWeatheringBinaryData(const FString& FilePath, int32& OutT);

    UFUNCTION(BlueprintCallable, Category = "Weathering")
    static void InterpolateWeathering(
        const TArray<float>& TexA_7d, // (H * W * 7) 크기의 평탄화된 배열
        const TArray<float>& TexB_7d, // (H * W * 7) 크기의 평탄화된 배열
        const TArray<float>& TrajectorySamples, // (T * 7) 바이너리에서 로드한 궤적 데이터
        int32 T,                      // 궤적 길이
        float Alpha,
        TArray<float>& OutResult);     // 결과 저장용 (H * W * 7)

    UFUNCTION(BlueprintCallable, Category = "Weathering")
    static void InterpolateWeatheringCached(
        const TArray<float>& TexA_7d,
        const TArray<float>& TexB_7d,
        const TArray<int32>& CachedNearestA,
        const TArray<int32>& CachedNearestB,
        const TArray<float>& TrajectorySamples,
        int32 T,
        float Alpha,
        TArray<float>& OutResult);

    UFUNCTION(BlueprintCallable, Category = "MWS|Texture")
    static TArray<float> CombineTextureSources(
        UTexture2D* Base,
        UTexture2D* Spec,
        UTexture2D* Rough);


    UFUNCTION(BlueprintCallable, Category = "MWS|Texture")
    static void BuildNearestTrajectoryCache(
        const TArray<float>& Texture7D,
        const TArray<float>& TrajectorySamples,
        int32 T,
        TArray<int32>& OutNearestIndices);

    UFUNCTION(BlueprintCallable)
    static void ReconstructTexturesFromTensor(
        const TArray<float>& Tensor7D,
        int32 Width,
        int32 Height,
        UTexture2D*& OutBaseColor,
        UTexture2D*& OutSpecular,
        UTexture2D*& OutRoughness);


    UFUNCTION(BlueprintCallable)
    static void ApplyWeatheringTexturesToMesh(
        UMeshComponent* MeshComponent,
        int32 MaterialIndex,
        UTexture2D* BaseTexture,
        UTexture2D* SpecTexture,
        UTexture2D* RoughTexture,
        FName BaseParamName,
        FName SpecParamName,
        FName RoughParamName,
        UMaterialInstanceDynamic*& OutMID);
};


