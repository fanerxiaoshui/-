// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "GameActionType.generated.h"

/**
 * 
 */
class UGameActionSegmentBase;

USTRUCT()
struct GAMEACTION_RUNTIME_API FGameActionEventEntry
{
	GENERATED_BODY()
public:
	FGameActionEventEntry() = default;
#if WITH_EDITORONLY_DATA
	FGameActionEventEntry(const FName& EventName, const FText& Tooltip)
		:EventName(EventName), Tooltip(Tooltip)
	{}
	
	UPROPERTY(EditAnywhere)
	FName EventName;
	UPROPERTY(EditAnywhere)
	FText Tooltip;
#endif
};

DECLARE_DYNAMIC_DELEGATE_RetVal_TwoParams(bool, FGameActionTransitionCondition, const UGameActionSegmentBase*, Segment, bool, IsServerJudge);
USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct GAMEACTION_RUNTIME_API FGameActionTransitionBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FGameActionTransitionCondition Condition;
	
	UPROPERTY()
	UGameActionSegmentBase* TransitionToSegment = nullptr;

	FORCEINLINE bool CanTransition(const UGameActionSegmentBase* Segment, bool IsServerJudge) const { return Condition.IsBound() ? Condition.Execute(Segment, IsServerJudge) : true; }
};

USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct GAMEACTION_RUNTIME_API FGameActionEntryTransition : public FGameActionTransitionBase
{
	GENERATED_BODY()
};

// 可在GameActionInstance的子类下添加该结构体实现多个入口
USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct FGameActionEntry
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TArray<FGameActionEntryTransition> Transitions;
};

USTRUCT(BlueprintType)
struct FGameActionPossessableActorData
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector RelativeLocation;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FRotator RelativeRotation;
};

UENUM()
enum class EGameActionPlayerEndAction : uint8
{
	Stop,
	Loop,
	Pause
};
