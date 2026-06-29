// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "BuildNearestTrajectoryAsync.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBuildNearestTrajectoryCacheCompleted, const TArray<int32>&, NearestIndices);

/**
 * 
 */
UCLASS()
class APPEARANCEMANIFOLD_API UBuildNearestTrajectoryAsync : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:

    UPROPERTY(BlueprintAssignable)
    FBuildNearestTrajectoryCacheCompleted Completed;

    UFUNCTION(BlueprintCallable,
        meta = (BlueprintInternalUseOnly = "true"),
        Category = "AppearanceManifold")
    static UBuildNearestTrajectoryAsync* BuildNearestTrajectoryCacheAsync(
        const TArray<float>& Texture7D,
        const TArray<float>& TrajectorySamples,
        int32 T);

    virtual void Activate() override;

private:

    TArray<float> CachedTexture7D;
    TArray<float> CachedTrajectorySamples;
    int32 CachedT = 0;
};
