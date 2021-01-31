// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Templates/SubclassOf.h"
#include "GameActionSequenceCustomSpawner.generated.h"

struct FMovieSceneSpawnable;
class UGameActionInstanceBase;
/**
 * 
 */
UENUM()
enum class EGameActionSpawnOwnership : uint8
{
	// 所有权为当前序列，当前序列结束后就会执行销毁
	Sequence,
	// 所有权为整个行为实例，当行为实例结束后会销毁
	Instance,
	// 所有权不在系统内部处理，可定制特殊的销毁逻辑
	External
};

UCLASS(abstract, const, collapseCategories)
class GAMEACTION_RUNTIME_API UGameActionSequenceSpawnerSettingsBase : public UObject
{
    GENERATED_BODY()
public:
	UGameActionSequenceSpawnerSettingsBase()
		: bAsReference(false), bDestroyWhenAborted(true)
	{}
	
	UPROPERTY(EditAnywhere, Category = "生成器", meta = (DisplayName = "为引用"))
	uint8 bAsReference : 1;
	UPROPERTY(EditAnywhere, Category = "生成器", meta = (DisplayName = "生成物所有权", EditCondition = bAsReference))
	EGameActionSpawnOwnership Ownership = EGameActionSpawnOwnership::Instance;
	UPROPERTY(EditAnywhere, Category = "生成器", meta = (DisplayName = "当中断时销毁", EditCondition = bAsReference))
	uint8 bDestroyWhenAborted : 1;
	UPROPERTY(EditAnywhere, Category = "生成器", meta = (DisplayName = "销毁延迟时间", EditCondition = "bAsReference == false || Ownership == EGameActionSpawnOwnership::Sequence"))
	float DestroyDelayTime = 0.f;
};

UCLASS()
class GAMEACTION_RUNTIME_API UGameActionSequenceSpawnerSettings : public UGameActionSequenceSpawnerSettingsBase
{
	GENERATED_BODY()
public:
	
};

UCLASS(abstract, const, collapseCategories)
class GAMEACTION_RUNTIME_API UGameActionSequenceCustomSpawnerBase : public UGameActionSequenceSpawnerSettingsBase
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
    virtual AActor* GetPreviewInstance(UObject* Outer) const { return nullptr; }
#endif

	virtual AActor* SpawnCustomActor(AActor* ObjectTemplate, const FMovieSceneSpawnable& Spawnable, UGameActionInstanceBase* GameActionInstance, const FTransform& Origin) const { checkNoEntry(); return nullptr; }
};

UCLASS(abstract, Blueprintable)
class GAMEACTION_RUNTIME_API UGameActionSequenceCustomSpawner : public UGameActionSequenceCustomSpawnerBase
{
    GENERATED_BODY()
protected:
#if WITH_EDITORONLY_DATA
    UPROPERTY(EditDefaultsOnly, Category = "生成器", meta = (DisplayName = "预览用类型"))
    TSubclassOf<AActor> PreviewType;

    AActor* GetPreviewInstance(UObject* Outer) const override;
#endif
	
    AActor* SpawnCustomActor(AActor* ObjectTemplate, const FMovieSceneSpawnable& Spawnable, UGameActionInstanceBase* GameActionInstance, const FTransform& Origin) const override;
    virtual TSubclassOf<AActor> GetSpawnType(UGameActionInstanceBase* GameActionInstance) const { return ReceiveGetSpawnType(GameActionInstance); }
    virtual void PreSpawning(AActor* SpawningActor) const { ReceivePreSpawning(SpawningActor); }
    virtual void PostSpawned(AActor* SpawningActor) const { ReceivePostSpawned(SpawningActor); }
	
    UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "GetSpawnType"))
    TSubclassOf<AActor> ReceiveGetSpawnType(UGameActionInstanceBase* GameActionInstance) const;
    UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "PreSpawning"))
    void ReceivePreSpawning(AActor* SpawningActor) const;
    UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "PostSpawned"))
    void ReceivePostSpawned(AActor* SpawnedActor) const;
};
