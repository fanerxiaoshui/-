// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "Templates/SubclassOf.h"
#include "GameActionBlueprint.generated.h"

class ACharacter;
class UGameInstance;
class AGameStateBase;
class AGameModeBase;

/**
 * 
 */
USTRUCT()
struct GAMEACTION_RUNTIME_API FGameActionSceneActorData
{
	GENERATED_BODY()
public:
#if WITH_EDITORONLY_DATA
	FGameActionSceneActorData()
		:bAsPossessable(true),
		bGenerateNetSetter(true)
	{}
	
	UPROPERTY()
	AActor* Template = nullptr;

	UPROPERTY(EditAnywhere, meta = (DisplayName = "为Possessable变量"))
	uint8 bAsPossessable : 1;

	UPROPERTY(EditAnywhere, meta = (DisplayName = "生成网络支持的Set函数", EditCondition = bAsPossessable))
	uint8 bGenerateNetSetter : 1;

	UPROPERTY()
	FTransform SpawnTransform;

	UPROPERTY(Transient)
	mutable AActor* PreviewActor = nullptr;

	FTransform GetSpawnTransform() const;
#endif
};

USTRUCT()
struct GAMEACTION_RUNTIME_API FGameActionSequenceOverride
{
	GENERATED_BODY()
public:
#if WITH_EDITORONLY_DATA
	FGameActionSequenceOverride()
		: bEnableOverride(true)
	{}
	
	UPROPERTY()
	TSoftObjectPtr<UObject> NodeOverride;

	UPROPERTY()
	uint8 bEnableOverride : 1;

	UPROPERTY()
	class UGameActionSequence* SequenceOverride = nullptr;
#endif
};

UCLASS()
class GAMEACTION_RUNTIME_API UGameActionScene : public UObject
{
	GENERATED_BODY()
public:
#if WITH_EDITORONLY_DATA
	UGameActionScene();
	
	UPROPERTY()
	ACharacter* OwnerTemplate;

	UPROPERTY(EditAnywhere, Category = "配置")
	TArray<FGameActionSceneActorData> TemplateDatas;

	UPROPERTY(EditAnywhere, Category = "配置", meta = (DisplayName = "预览用GameInstance"))
	TSubclassOf<UGameInstance> GameInstanceType;

	UPROPERTY(EditAnywhere, Category = "配置", meta = (DisplayName = "预览用GameState"))
	TSubclassOf<AGameStateBase> GameStateType;

	UPROPERTY(EditAnywhere, Category = "配置", meta = (DisplayName = "预览用GameMode"))
	TSubclassOf<AGameModeBase> GameModeType;
#else
	UGameActionScene() {}
#endif
};

UCLASS()
class GAMEACTION_RUNTIME_API UGameActionBlueprint : public UBlueprint
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	UClass* GetBlueprintClass() const override;
	void GetReparentingRules(TSet<const UClass*>& AllowedChildrenOfClasses, TSet<const UClass*>& DisallowedChildrenOfClasses) const override;
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	void PreSave(const ITargetPlatform* TargetPlatform) override;

	// FBlueprintCompilationManagerImpl::FlushCompilationQueueImpl中FBlueprintEditorUtils::IsDataOnlyBlueprint判定为DataOnly的导致无法触发OnPostCDOCompiled
	bool AlwaysCompileOnLoad() const override { return IsRootBlueprint() == false; }

	TSubclassOf<ACharacter> GetOwnerType() const;
	UGameActionBlueprint* FindRootBlueprint() const;
	bool IsRootBlueprint() const { return GameActionGraph != nullptr; }
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UObject* GameActionGraph = nullptr;
	class UEdGraph_GameAction* GetGameActionGraph() const { return (UEdGraph_GameAction*)GameActionGraph; }
	
	UPROPERTY()
	class UGameActionScene* GameActionScene;

	static const FName MD_GameActionPossessableReference;
	static const FName MD_GameActionSpawnableReference;
	static const FName MD_GameActionTemplateReference;

	TWeakPtr<class FGameActionSequencer> PreviewSequencer;

	UPROPERTY()
	TArray<FGameActionSequenceOverride> SequenceOverrides;
#endif
};
