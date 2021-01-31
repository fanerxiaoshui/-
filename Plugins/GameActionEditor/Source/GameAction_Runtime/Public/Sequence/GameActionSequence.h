// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneSequence.h"
#include "GameActionSequence.generated.h"

class AActor;
class ACharacter;

/**
 * 
 */
USTRUCT()
struct GAMEACTION_RUNTIME_API FGameActionSequenceSubobjectBinding
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FName OwnerPropertyName;
	UPROPERTY()
	FString PathToSubobject;
};

UCLASS()
class GAMEACTION_RUNTIME_API UGameActionSequence : public UMovieSceneSequence
{
	GENERATED_BODY()
public:
    UGameActionSequence(const FObjectInitializer& ObjectInitializer);

	//~ UMovieSceneSequence interface
	void BindPossessableObject(const FGuid& ObjectId, UObject& PossessedObject, UObject* Context) override;
	void UnbindPossessableObjects(const FGuid& ObjectId) override;
	bool CanPossessObject(UObject& Object, UObject* InPlaybackContext) const override;
	void LocateBoundObjects(const FGuid& ObjectId, UObject* Context, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const override;
	UMovieScene* GetMovieScene() const override { return MovieScene; }
	UObject* GetParentObject(UObject* Object) const override;
	UObject* CreateDirectorInstance(IMovieScenePlayer& Player) override;
	bool AllowsSpawnableObjects() const override { return true; }
	bool CanRebindPossessable(const FMovieScenePossessable& InPossessable) const override { return !InPossessable.GetParent().IsValid(); }
	void PreSave(const ITargetPlatform* TargetPlatform) override;

#if WITH_EDITORONLY_DATA
	FGuid SetOwnerCharacter(const TSubclassOf<ACharacter>& OwnerType);
	FGuid AddPossessableActor(const FName& Name, const TSubclassOf<AActor>& ActorType);
	FGuid AddSpawnable(const FString& Name, UObject& ObjectTemplate);

	TWeakPtr<class FGameActionEditScene> BelongToEditScene;
#endif

	UPROPERTY(Instanced)
	UMovieScene* MovieScene;

	UPROPERTY()
	FGuid OwnerGuid;
	UPROPERTY()
	TMap<FGuid, FName> PossessableActors;
	UPROPERTY()
	TMap<FGuid, FGameActionSequenceSubobjectBinding> BindingSubobjects;
};
