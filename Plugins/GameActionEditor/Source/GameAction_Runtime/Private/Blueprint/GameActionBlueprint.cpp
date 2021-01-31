// Fill out your copyright notice in the Description page of Project Settings.


#include "Blueprint/GameActionBlueprint.h"
#include <GameFramework/Character.h>
#include <GameFramework/GameStateBase.h>
#include <Engine/GameInstance.h>

#include "Blueprint/GameActionGeneratedClass.h"
#include "GameAction/GameActionInstance.h"

#if WITH_EDITOR
UGameActionScene::UGameActionScene()
{
	GameInstanceType = UGameInstance::StaticClass();
	GameStateType = AGameStateBase::StaticClass();
	GameModeType = AGameModeBase::StaticClass();
}

FTransform FGameActionSceneActorData::GetSpawnTransform() const
{
	if (Template->GetRootComponent())
	{
		return Template->GetActorTransform();
	}
	return SpawnTransform;
}

UClass* UGameActionBlueprint::GetBlueprintClass() const
{
	return UGameActionGeneratedClass::StaticClass();
}

void UGameActionBlueprint::GetReparentingRules(TSet<const UClass*>& AllowedChildrenOfClasses, TSet<const UClass*>& DisallowedChildrenOfClasses) const
{
	AllowedChildrenOfClasses.Add(UGameActionInstanceBase::StaticClass());
}

void UGameActionBlueprint::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UGameActionBlueprint, GameActionScene))
	{
		
	}
}

void UGameActionBlueprint::PreSave(const ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);

	SequenceOverrides.RemoveAll([](const FGameActionSequenceOverride& E){ return E.NodeOverride.IsValid() == false; });
}

TSubclassOf<ACharacter> UGameActionBlueprint::GetOwnerType() const
{
	return GameActionScene->OwnerTemplate->GetClass();
}

UGameActionBlueprint* UGameActionBlueprint::FindRootBlueprint() const
{
	for (UClass* Parent = ParentClass; Parent && (UObject::StaticClass() != Parent); Parent = Parent->GetSuperClass())
	{
		if (UGameActionBlueprint* TestBP = Cast<UGameActionBlueprint>(Parent->ClassGeneratedBy))
		{
			if (TestBP->IsRootBlueprint())
			{
				return TestBP;
			}
		}
	}
	return nullptr;
}

const FName UGameActionBlueprint::MD_GameActionPossessableReference = TEXT("GameActionPossessableReference");
const FName UGameActionBlueprint::MD_GameActionSpawnableReference = TEXT("GameActionSpawnableReference");
const FName UGameActionBlueprint::MD_GameActionTemplateReference = TEXT("GameActionTemplateReference");

#endif
