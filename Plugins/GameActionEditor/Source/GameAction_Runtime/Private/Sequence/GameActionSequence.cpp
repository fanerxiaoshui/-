// Fill out your copyright notice in the Description page of Project Settings.


#include "Sequence/GameActionSequence.h"
#include <GameFramework/Character.h>
#include <MovieScene.h>
#include <Animation/AnimInstance.h>
#if WITH_EDITOR
#include <Interfaces/ITargetPlatform.h>
#endif

#include "GameAction/GameActionInstance.h"
#include "Blueprint/GameActionBlueprint.h"
#include "Sequence/GameActionTimeTestingTrack.h"

UGameActionSequence::UGameActionSequence(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bParentContextsAreSignificant = true;
	DefaultCompletionMode = EMovieSceneCompletionMode::RestoreState;
	
	MovieScene = ObjectInitializer.CreateDefaultSubobject<UMovieScene>(this, GET_MEMBER_NAME_CHECKED(UGameActionSequence, MovieScene));
	{
#if WITH_EDITOR
		MovieScene->SetFlags(RF_Transactional);
#endif

		FFrameRate DisplayRate(30, 1);
		MovieScene->SetDisplayRate(DisplayRate);
		MovieScene->SetPlaybackRange(0, 2.f * 60000, false);
	}
}

void UGameActionSequence::BindPossessableObject(const FGuid& ObjectId, UObject& PossessedObject, UObject* Context)
{
	if (ObjectId.IsValid())
	{
		if (AActor* Actor = Cast<AActor>(&PossessedObject))
		{
			const FName ActorName = 
#if WITH_EDITOR
				*Actor->GetActorLabel();
#else
				Actor->GetFName();
#endif
			PossessableActors.Add(ObjectId, ActorName);
		}
		else
		{
			const AActor* Owner = nullptr;
			if (UActorComponent* Component = Cast<UActorComponent>(&PossessedObject))
			{
				Owner = Component->GetOwner();
			}
			else if (UAnimInstance* AnimInstance = Cast<UAnimInstance>(&PossessedObject))
			{
				Owner = AnimInstance->GetOwningActor();
			}
			
			const FName OwnerName =
#if WITH_EDITOR
				*Owner->GetActorLabel();
#else
				Owner->GetFName();
#endif

			FGameActionSequenceSubobjectBinding GameActionSequenceSubobjectBinding;
			GameActionSequenceSubobjectBinding.OwnerPropertyName = OwnerName;
			GameActionSequenceSubobjectBinding.PathToSubobject = PossessedObject.GetPathName(Owner);
			BindingSubobjects.Add(ObjectId, GameActionSequenceSubobjectBinding);
		}
	}
}

void UGameActionSequence::UnbindPossessableObjects(const FGuid& ObjectId)
{
	PossessableActors.Remove(ObjectId);
	BindingSubobjects.Remove(ObjectId);
}

bool UGameActionSequence::CanPossessObject(UObject& Object, UObject* InPlaybackContext) const
{
	if (InPlaybackContext == nullptr)
	{
		return false;
	}

	UGameActionInstanceBase* GameActionInstance = CastChecked<UGameActionInstanceBase>(InPlaybackContext);

	if (AActor* Actor = Cast<AActor>(&Object))
	{
		return Actor->GetWorld() == GameActionInstance->GetWorld();
	}
	else if (UActorComponent* Component = Cast<UActorComponent>(&Object))
	{
		return Component->GetWorld() == GameActionInstance->GetWorld();
	}
	else if (UAnimInstance* AnimInstance = Cast<UAnimInstance>(&Object))
	{
		return AnimInstance->GetWorld() == GameActionInstance->GetWorld();
	}
	return false;
}

void UGameActionSequence::LocateBoundObjects(const FGuid& ObjectId, UObject* Context, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const
{
	if (ensure(Context) == false)
	{
		return;
	}

	if (UGameActionInstanceBase* GameActionInstance = Cast<UGameActionInstanceBase>(Context))
	{
		if (OwnerGuid == ObjectId)
		{
			OutObjects.Add(GameActionInstance->GetOwner());
		}
		else if (const FName* PropertyName = PossessableActors.Find(ObjectId))
		{
			if (FObjectProperty* ObjectProperty = FindFProperty<FObjectProperty>(GameActionInstance->GetClass(), *PropertyName))
			{
				if (UObject** P_Object = ObjectProperty->ContainerPtrToValuePtr<UObject*>(GameActionInstance))
				{
					OutObjects.Add(*P_Object);
				}
			}
		}
		else if (const FGameActionSequenceSubobjectBinding* SubobjectBinding = BindingSubobjects.Find(ObjectId))
		{
			AActor* OwnerActor = nullptr;
			if (SubobjectBinding->OwnerPropertyName == UGameActionInstanceBase::GameActionOwnerName)
			{
				OwnerActor = GameActionInstance->GetOwner();
			}
			else
			{
				if (FObjectProperty* ObjectProperty = FindFProperty<FObjectProperty>(GameActionInstance->GetClass(), SubobjectBinding->OwnerPropertyName))
				{
					if (AActor** P_Owner = ObjectProperty->ContainerPtrToValuePtr<AActor*>(GameActionInstance))
					{
						OwnerActor = *P_Owner;
					}
				}
			}

			if (OwnerActor)
			{
				OutObjects.Add(FindObject<UObject>(OwnerActor, *SubobjectBinding->PathToSubobject));
			}
		}
	}
	else if (AActor* Actor = Cast<AActor>(Context))
	{
		if (const FGameActionSequenceSubobjectBinding* SubObjectBinding = BindingSubobjects.Find(ObjectId))
		{
			OutObjects.Add(FindObject<UActorComponent>(Actor, *SubObjectBinding->PathToSubobject));
		}
	}
}

UObject* UGameActionSequence::GetParentObject(UObject* Object) const
{
	if (UActorComponent* Component = Cast<UActorComponent>(Object))
	{
		return Component->GetOwner();
	}
	else if (UAnimInstance* AnimInstance = Cast<UAnimInstance>(Object))
	{
		return AnimInstance->GetOwningActor();
	}
	return nullptr;
}

UObject* UGameActionSequence::CreateDirectorInstance(IMovieScenePlayer& Player)
{
	UGameActionInstanceBase* GameActionInstance = CastChecked<UGameActionInstanceBase>(Player.GetPlaybackContext());
	return GameActionInstance;
}

void UGameActionSequence::PreSave(const ITargetPlatform* TargetPlatform)
{
#if WITH_EDITOR
	if (TargetPlatform && TargetPlatform->RequiresCookedData())
	{
		if (UGameActionTimeTestingTrack* TimeTestingTrack = MovieScene->FindMasterTrack<UGameActionTimeTestingTrack>())
		{
			MovieScene->RemoveMasterTrack(*TimeTestingTrack);
		}
	}
#endif
	Super::PreSave(TargetPlatform);
}

#if WITH_EDITOR
FGuid UGameActionSequence::SetOwnerCharacter(const TSubclassOf<ACharacter>& OwnerType)
{
	const FGuid BindingID = MovieScene->AddPossessable(TEXT("Owner"), OwnerType);
	OwnerGuid = BindingID;
	return OwnerGuid;
}

FGuid UGameActionSequence::AddPossessableActor(const FName& Name, const TSubclassOf<AActor>& ActorType)
{
	check(ActorType);
	const FGuid BindingID = MovieScene->AddPossessable(Name.ToString(), ActorType);
	PossessableActors.Add(BindingID, Name);
	return BindingID;
}

FGuid UGameActionSequence::AddSpawnable(const FString& Name, UObject& ObjectTemplate)
{
	const FGuid BindingID = MovieScene->AddSpawnable(Name, ObjectTemplate);
	FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(BindingID);
	Spawnable->SetSpawnOwnership(ESpawnOwnership::InnerSequence);
	return BindingID;
}
#endif
