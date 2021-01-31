// Fill out your copyright notice in the Description page of Project Settings.


#include "Animation/GameActionAnimNotify.h"
#include <GameFramework/Actor.h>
#include <Components/SkeletalMeshComponent.h>
#include <Animation/AnimSequenceBase.h>
#include <Animation/AnimMontage.h>

#include "GameAction/GameActionComponent.h"
#include "GameAction/GameActionInstance.h"

UGameActionAnimNotify::UGameActionAnimNotify()
{
	bIsNativeBranchingPoint = true;
}

#if WITH_EDITOR
bool UGameActionAnimNotify::CanBePlaced(UAnimSequenceBase* Animation) const
{
	return Animation && Animation->IsA<UAnimMontage>();
}
#endif

void UGameActionAnimNotify::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
	if (ensure(MeshComp))
	{
		AActor* Owner = MeshComp->GetOwner();
		if (Owner->HasAuthority())
		{
			if (UGameActionComponent* GameActionComponent = Owner->FindComponentByClass<UGameActionComponent>())
			{
				if (GameActionComponent->ContainGameAction(ActionToPlay) == false)
				{
					GameActionComponent->PlayGameAction(ActionToPlay, EntryName);
				}
			}
		}
	}
}

FString UGameActionAnimNotify::GetNotifyName_Implementation() const
{
#if WITH_EDITOR
	return FString::Printf(TEXT("播放游戏行为[%s]"), ActionToPlay ? *ActionToPlay->GetDisplayNameText().ToString() : TEXT("None"));
#endif
	return FString::Printf(TEXT("播放游戏行为[%s]"), ActionToPlay ? *ActionToPlay->GetName() : TEXT("None"));
}
