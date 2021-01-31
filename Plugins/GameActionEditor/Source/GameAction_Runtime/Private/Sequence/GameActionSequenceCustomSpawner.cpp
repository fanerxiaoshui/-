// Fill out your copyright notice in the Description page of Project Settings.


#include "Sequence/GameActionSequenceCustomSpawner.h"
#include <MovieSceneSpawnable.h>
#include <Misc/ScopeExit.h>
#include <GameFramework/Character.h>
#include <Engine/Engine.h>

#include "GameAction/GameActionInstance.h"

#if WITH_EDITOR
AActor* UGameActionSequenceCustomSpawner::GetPreviewInstance(UObject* Outer) const
{
	if (ensure(PreviewType))
	{
		return NewObject<AActor>(Outer, PreviewType, NAME_None, RF_Transactional);
	}
	return nullptr;
}
#endif

AActor* UGameActionSequenceCustomSpawner::SpawnCustomActor(AActor* ObjectTemplate, const FMovieSceneSpawnable& Spawnable, UGameActionInstanceBase* GameActionInstance, const FTransform& Origin) const
{
	// TODO：可配置是否为Transient，处理召唤物的情况
	const EObjectFlags ActorFlags = RF_NoFlags;//RF_Transient;

	AActor* ActorTemplate = ObjectTemplate;
	
	TSubclassOf<AActor> SpawnType = GetSpawnType(GameActionInstance);

#if WITH_EDITOR
	if (GameActionInstance->GetWorld()->WorldType == EWorldType::EditorPreview)
	{
		SpawnType = ObjectTemplate->GetClass();
	}
#endif

	if (SpawnType == nullptr)
	{
		return nullptr;
	}
	
	// 现在由于Spawn的Template必须类型一致否则没法Spawn，所以需要New一个
	const bool IsNewTemplateForSpawn = ObjectTemplate->GetClass() != SpawnType;
	if (IsNewTemplateForSpawn)
	{
		ActorTemplate = NewObject<AActor>(GetTransientPackage(), SpawnType, NAME_None, RF_Transient);
		UEngine::FCopyPropertiesForUnrelatedObjectsParams CopyParams;
		CopyParams.bNotifyObjectReplacement = false;
		CopyParams.bPreserveRootComponent = false;
		UEngine::CopyPropertiesForUnrelatedObjects(ObjectTemplate, ActorTemplate, CopyParams);
	}
	ON_SCOPE_EXIT
	{
		if (IsNewTemplateForSpawn)
		{
			ActorTemplate->MarkPendingKill();
		}
	};

	UWorld* World = GameActionInstance->GetWorld();
	FActorSpawnParameters SpawnParameters;
	{
		SpawnParameters.ObjectFlags = ActorFlags;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.bDeferConstruction = true;
		SpawnParameters.Template = ActorTemplate;
		SpawnParameters.OverrideLevel = GameActionInstance->GetOwner()->GetLevel();
	}

	FTransform SpawnTransform;
	if (USceneComponent* RootComponent = ActorTemplate->GetRootComponent())
	{
		SpawnTransform.SetTranslation(RootComponent->GetRelativeLocation());
		SpawnTransform.SetRotation(RootComponent->GetRelativeRotation().Quaternion());
	}
	else
	{
		SpawnTransform = Spawnable.SpawnTransform;
	}

	SpawnTransform *= Origin;

	AActor* SpawnableInstance = World->SpawnActorAbsolute(ActorTemplate->GetClass(), SpawnTransform, SpawnParameters);
	if (!SpawnableInstance)
	{
		return nullptr;
	}

	// Ensure this spawnable is not a preview actor. Preview actors will not have BeginPlay() called on them.
#if WITH_EDITOR
	SpawnableInstance->bIsEditorPreviewActor = false;

	if (GIsEditor)
	{
		// Explicitly set RF_Transactional on spawned actors so we can undo/redo properties on them. We don't add this as a spawn flag since we don't want to transact spawn/destroy events.
		SpawnableInstance->SetFlags(RF_Transactional);

		for (UActorComponent* Component : SpawnableInstance->GetComponents())
		{
			if (Component)
			{
				Component->SetFlags(RF_Transactional);
			}
		}
	}
#endif
	const bool bIsDefaultTransform = true;
	PreSpawning(SpawnableInstance);
	SpawnableInstance->FinishSpawning(SpawnTransform, bIsDefaultTransform);
	PostSpawned(SpawnableInstance);
	return SpawnableInstance;
}
