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
    
    // 풍화 경로의 바이너리 파일 저장 경로를 호출합니다.
    UFUNCTION(BlueprintPure, Category = "AppearanceManifold|Runtime|Utility")
    static FString GetTrajectorySaveDirectory(const FString& FileName);
    

    // 풍화 경로의 바이너리 파일과 노드 수를 반환합니다.
    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Utility")
    static TArray<float> LoadWeatheringBinaryData(const FString& FilePath, int32& OutT);


    // 특정 경로의 텐서 바이너리 파일을 호출합니다.
    // 신규 키프레임을 생성하는 용도로 활용할 수 있습니다.
    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Utility")
    static TArray<float> LoadWeatheringTensorsFromFiles(const FString& DirectoryPath);


    // 배열 정보를 액터 이름과 인덱스 번호를 활용해 바이너리 파일로 저장합니다.
    // 주로 텍스처를 1차원 배열로 변환한 뒤 저장하는 용도로 활용할 수 있습니다.
    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Utility")
    static FString SaveFloatArrayToBinary(const TArray<float>& Data, const FString& ActorName, int32 Index);


    // 저장된 배열 정보를 가져옵니다.
    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Utility")
    static bool LoadFloatArrayFromBinary(const FString& FilePath, TArray<float>& OutData);


    // 특정 이름을 가진 텐서(1차원 배열) 파일의 경로가 존재하는지 확인합니다.
    // 해당 경로가 존재할 경우 데이터를 제거하거나 참조하는 체크 함수로 활용합니다.
    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Utility")
    static FString GetTensorCacheFilePath(const FString& ActorName, int32 Index, bool& bFileFound);


    // 특정 이름을 가진 텐서 파일을 제거합니다.
    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Editor|Utility")
    static bool DeleteTensorCacheFile(const FString& ActorName, int32 Index);
#pragma endregion

#pragma region Weathering
    // 키 프레임의 각 픽셀들의 Trajectory와 가장 가까운 지점을 선정해 인덱스 형태로 출력합니다.
    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Weathering")
    static void BuildNearestTrajectoryCache(
        const TArray<float>& Texture7D,
        const TArray<float>& TrajectorySamples,
        int32 T,
        TArray<int32>& OutNearestIndices);

    // 확보된 텐서 정보와 풍화 경로 및 알파 값을 통해 중간 프레임 텍스처를 배열로 출력합니다.
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
#pragma endregion

#pragma region Texture

    // UTexture2D 정보를 하나의 1차원 배열 텐서 형태로 변환합니다.
    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Runtime|Texture")
    static TArray<float> CombineTextureSources(
        UTexture2D* Base,
        UTexture2D* Spec,
        UTexture2D* Rough);


    // 중간 프레임 텍스처 역할을 수행하는 1차원 배열 텐서를 실제 UTexture2D 인스턴스로 변환하여 출력합니다.
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

    // "풍화 경로 생성을 위한 예외적 함수입니다."
    // ReconstructTexturesFromTensor와 내부 로직과 출력의 형태가 다릅니다.
    // 1차원 배열 텐서를 실제 UTexture2D 인스턴스로 변환하여 출력합니다.
    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Editor|Texture")
    static void ReconstructTexturesForTrajectory(
        const TArray<float>& Tensor7D,
        int32 Width,
        int32 Height,

        UTexture2D* ExistingBaseColor,
        UTexture2D* ExistingSpecular,
        UTexture2D* ExistingRoughness,

        UTexture2D*& OutBaseColor,
        UTexture2D*& OutSpecular,
        UTexture2D*& OutRoughness);


    // 생성된 텍스처를 메쉬에 적용합니다.
    // BaseColor, Specular, Roughness에 해당하는 머티리얼 파라미터가 지정되어 존재해야만 동작합니다.
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

    // 에디터 전용 함수입니다.
    // 텍스처 에셋을 PNG 형태로 저장합니다.
    // 주로 텍스처를 파이썬 스크립트에서 다루기 위한 형태로 변환하는 용도로 활용합니다.
    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Editor|Utility")
    static bool ExportTextureToPNG(UTexture2D* Texture, const FString& FilePath);

    // 에디터 전용 함수입니다.
    // 특정 파일 경로를 이용해 텍스처로 구성하는 함수입니다.
    // 필요한가?
    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Editor|Utility")
    static UTexture2D* ImportTextureFromFile(const FString& FilePath);
    
    // 에디터 전용 함수입니다.
    // 텍스처를 파이썬 스크립트에서 다루기 위한 형태로 변환한 뒤 저장하는 경로를 호출합니다.
    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Editor|Utility")
    static FString GetAppearanceManifoldDirectory();

    // 에디터 전용 함수입니다.
    // 특정 파이썬 커맨드를 실행합니다.
    //UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Editor|Python")
    static bool ExecutePythonCommand(const FString& PythonCommand);
    
    // 에디터 전용 함수입니다.
    // 풍화 경로를 생성하는 실질적인 트리거입니다.
    UFUNCTION(BlueprintCallable, Category = "AppearanceManifold|Editor|Pipeline")
    static bool RunWeatheringPipeline(UTexture2D* BaseColor,UTexture2D* Specular,UTexture2D* Roughness,
        const FString& WorkingDirectory,const FString& FileName);

#pragma endregion

#pragma region Helper

    static void UpdateTextureBGRA(UTexture2D* Texture, const TArray<uint8>& PixelData, int32 Width, int32 Height);

    static bool LoadRawPNGData(const FString& FilePath, TArray<uint8>& OutData, int32& OutWidth, int32& OutHeight);
#pragma endregion
};


