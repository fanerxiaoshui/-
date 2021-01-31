// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <AdvancedPreviewScene.h>

class UGameActionBlueprint;
class ACharacter;
class UGameActionInstanceBase;
class UGameActionScene;
class FEditorViewportClient;
class UGameActionComponent;

/**
 * 
 */
class FGameActionPreviewScene : public FAdvancedPreviewScene
{
	using Super = FAdvancedPreviewScene;
public:
	FGameActionPreviewScene(ConstructionValues CVS, UGameActionBlueprint* InGameActionBlueprint);
	~FGameActionPreviewScene() override;

	void Tick(float DeltaTime) override;

	UGameActionInstanceBase* GetGameActionInstance() const { return PreviewInstance; }
	UGameActionComponent* GetGameActionComponent() const { return GameActionComponent; }
	UGameActionBlueprint* GameActionBlueprint = nullptr;
	ACharacter* PreviewOwner = nullptr;

	void SetPreviewGameActionScene(UGameActionScene* GameActionScene);
	void CreatePreviewInstance();
	TArray<AActor*> PreviewActors;

	TWeakPtr<FEditorViewportClient> EditorViewportClient;
	void SelectActor(AActor* Actor) const;
	void SelectComponent(UActorComponent* Component) const;
	void SelectObject(UObject* Object) const;

	void AddReferencedObjects(FReferenceCollector& Collector) override;
	FString GetReferencerName() const override { return TEXT("FGameActionPreviewScene"); }
protected:
	UGameActionInstanceBase* PreviewInstance = nullptr;
	UGameActionComponent* GameActionComponent = nullptr;
};

class FGameActionEditScene : public FGameActionPreviewScene
{
	using Super = FGameActionPreviewScene;
public:
	FGameActionEditScene(UGameActionBlueprint* InGameActionBlueprint);

	void SetDrawableObject(UObject* Object, UObject* Origin);
	UObject* GetDrawableObject() const { return DrawableObject.Get(); }
	void ClearDrawableObject();

	UObject* GetItemToTryDisplayingWidgetsFor(FTransform& OutWidgetToWorld) const;

	TWeakObjectPtr<AActor> ToViewActor;
	DECLARE_DELEGATE_TwoParams(FOnMoveToViewActor, const FVector&, const FRotator&);
	FOnMoveToViewActor OnMoveViewActor;

	TWeakObjectPtr<AActor> CameraCutsViewActor;
private:
	TWeakObjectPtr<UObject> DrawableObject;
	TWeakObjectPtr<UObject> DrawableObjectOrigin;
};

class FGameActionSimulateScene : public FGameActionPreviewScene
{
	using Super = FGameActionPreviewScene;
public:
	FGameActionSimulateScene(UGameActionBlueprint* InGameActionBlueprint);

	void AddReferencedObjects(FReferenceCollector& Collector) override;

	APlayerController* PlayerController = nullptr;
};
