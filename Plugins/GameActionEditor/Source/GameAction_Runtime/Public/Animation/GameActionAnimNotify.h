// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Templates/SubclassOf.h"
#include "GameActionAnimNotify.generated.h"

class UGameActionInstanceBase;

/**
 * 
 */
UCLASS(meta = (DisplayName = "播放游戏行为"))
class GAMEACTION_RUNTIME_API UGameActionAnimNotify : public UAnimNotify
{
	GENERATED_BODY()
public:
	UGameActionAnimNotify();
	
	UPROPERTY(EditAnywhere)
	TSubclassOf<UGameActionInstanceBase> ActionToPlay;

	UPROPERTY(EditAnywhere)
	FName EntryName;

#if WITH_EDITOR
	bool CanBePlaced(UAnimSequenceBase* Animation) const override;
#endif
	void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) override;
	FString GetNotifyName_Implementation() const override;
};
