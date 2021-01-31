// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <Factories/Factory.h>
#include <AssetTypeActions/AssetTypeActions_Blueprint.h>
#include <ThumbnailRendering/DefaultSizedThumbnailRenderer.h>
#include "GameActionFactory.generated.h"

class UGameActionBlueprint;
class UGameActionInstanceBase;
class ACharacter;
class UGameActionScene;

/**
 * 
 */
UCLASS()
class GAMEACTION_EDITOR_API UGameActionBlueprintFactory : public UFactory
{
	GENERATED_BODY()
public:
	UGameActionBlueprintFactory();

	FString GetDefaultNewAssetName() const override;
	UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;

	UPROPERTY(Transient)
	TSubclassOf<UGameActionInstanceBase> ParentClass;
	UPROPERTY(Transient)
	UGameActionScene* GameActionScene;
	UPROPERTY(Transient)
	uint8 bCreateAbstract : 1;

	bool ConfigureProperties() override;
};

class FGameActionBlueprint_AssetTypeActions : public FAssetTypeActions_Blueprint
{
public:
	FGameActionBlueprint_AssetTypeActions();

	// Inherited via FAssetTypeActions_Base
	FText GetName() const override;
	FText GetDisplayNameFromAssetData(const FAssetData& AssetData) const override;
	UClass* GetSupportedClass() const override;
	FColor GetTypeColor() const override;
	uint32 GetCategories() override;
	void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor) override;
	UFactory* GetFactoryForBlueprintType(UBlueprint* InBlueprint) const override;

	static bool RetargetGameActionScene(UGameActionBlueprint* Blueprint);
};

UCLASS()
class UGameActionBlueprintThumbnailRenderer : public UDefaultSizedThumbnailRenderer
{
	GENERATED_BODY()
public:
	void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily) override;
	void BeginDestroy() override;
	bool AllowsRealtimeThumbnails(UObject* Object) const override { return false; }
private:
	class FAdvancedPreviewScene* ThumbnailScene = nullptr;
};
