// Fill out your copyright notice in the Description page of Project Settings.


#include "BuildNearestTrajectoryAsync.h"

#include "AppearanceManifoldBlueprintFunctionLibrary.h"

#include "Async/Async.h"

UBuildNearestTrajectoryAsync* UBuildNearestTrajectoryAsync::BuildNearestTrajectoryCacheAsync(
    const TArray<float>& Texture7D,
    const TArray<float>& TrajectorySamples,
    int32 T)
{
    UBuildNearestTrajectoryAsync* Node =
        NewObject<UBuildNearestTrajectoryAsync>();

    Node->CachedTexture7D = Texture7D;
    Node->CachedTrajectorySamples = TrajectorySamples;
    Node->CachedT = T;

    return Node;
}

void UBuildNearestTrajectoryAsync::Activate()
{
    TWeakObjectPtr<UBuildNearestTrajectoryAsync> WeakThis(this);

    Async(
        EAsyncExecution::ThreadPool,
        [WeakThis,
        Texture7D = CachedTexture7D,
        TrajectorySamples = CachedTrajectorySamples,
        T = CachedT]()
        {
            TArray<int32> Result;

            UAppearanceManifoldBlueprintFunctionLibrary::BuildNearestTrajectoryCache(
                Texture7D,
                TrajectorySamples,
                T,
                Result);

            AsyncTask(
                ENamedThreads::GameThread,
                [WeakThis, Result = MoveTemp(Result)]() mutable
                {
                    if (!WeakThis.IsValid())
                    {
                        return;
                    }

                    WeakThis->Completed.Broadcast(Result);

                    WeakThis->SetReadyToDestroy();
                });
        });
}