// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MWSEditorFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class MWS_API UMWSEditorFunctionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    UFUNCTION(BlueprintCallable, Category = "MWS|Texture")
    static bool ExportTextureToPNG(
        UTexture2D* Texture,
        const FString& FilePath
    );

    UFUNCTION(BlueprintCallable, Category = "MWS|Python")
    static bool ExecutePythonCommand(
        const FString& PythonCommand
    );

    UFUNCTION(BlueprintCallable, Category = "MWS|Pipeline")
    static bool RunWeatheringPipeline(
        UTexture2D* BaseColor,
        UTexture2D* Specular,
        UTexture2D* Roughness,
        const FString& WorkingDirectory
    );

    UFUNCTION(BlueprintCallable, Category = "MWS|Texture")
    static UTexture2D* ImportTextureFromFile(
        const FString& FilePath
    );

    UFUNCTION(BlueprintCallable, Category = "MWS|Texture")
    static FString GetAppearanceManifoldDirectory();

	
};
