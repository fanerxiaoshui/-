// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "GameAction/GameActionType.h"
#include "GameActionSegment.generated.h"

class UGameActionSequence;
class UGameActionInstanceBase;
class UGameActionSegmentBase;
class ACharacter;

/**
 * 
 */
USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct GAMEACTION_RUNTIME_API FGameActionTickTransition : public FGameActionTransitionBase
{
	GENERATED_BODY()
};

USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct GAMEACTION_RUNTIME_API FGameActionEventTransition : public FGameActionTransitionBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FName EventName;
};

DECLARE_DYNAMIC_DELEGATE(FOnActionActivedEvent);
DECLARE_DYNAMIC_DELEGATE(FOnActionAbortedEvent);
DECLARE_DYNAMIC_DELEGATE(FOnActionDeactivedEvent);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnActionTickEvent, float, DeltaSeconds);
DECLARE_DYNAMIC_DELEGATE(FOnActionTransitionFailed);

UCLASS(abstract, DefaultToInstanced)
class GAMEACTION_RUNTIME_API UGameActionSegmentBase : public UObject
{
	GENERATED_BODY()
public:
    UGameActionSegmentBase(const FObjectInitializer& ObjectInitializer);

	UWorld* GetWorld() const override;
	bool IsSupportedForNetworking() const override { return true; }
	void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	int32 GetFunctionCallspace(UFunction* Function, FFrame* Stack) override;
	bool CallRemoteFunction(UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack) override;

	UGameActionInstanceBase* GetOwner() const;
	ACharacter* GetOwningCharacter() const;
	bool HasAuthority() const;
	bool IsLocalControlled() const;
	bool IsActived() const;
public:
	void ActiveAction();
	void AbortAction();
	void DeactiveAction();
	void TickAction(float DeltaSeconds);
	void TransitionActionFailed(UGameActionSegmentBase* TransitionFailedSegment);

	void TryFinishActionOrTransition();
	bool InvokeEventTransition(const FName& EventName);
protected:
	virtual void WhenActionActived() { ReceiveWhenActionActived(); }
	virtual void WhenActionAborted() { ReceiveWhenActionAborted(); }
	virtual void WhenActionDeactived() { ReceiveWhenActionDeactived(); }
	virtual void WhenActionTick(float DeltaSeconds) { ReceiveWhenActionTick(DeltaSeconds); }
	virtual void WhenTransitionFailed(UGameActionSegmentBase* TransitionFailedSegment) { ReceiveWhenTransitionFailed(TransitionFailedSegment); }

	UFUNCTION(BlueprintImplementableEvent, Category = "游戏行为", meta = (DisplayName = "When Action Actived"))
	void ReceiveWhenActionActived();
	UFUNCTION(BlueprintNativeEvent, Category = "游戏行为", meta = (DisplayName = "When Action Aborted"))
	void ReceiveWhenActionAborted();
	UFUNCTION(BlueprintImplementableEvent, Category = "游戏行为", meta = (DisplayName = "When Action Deactived"))
	void ReceiveWhenActionDeactived();
	UFUNCTION(BlueprintImplementableEvent, Category = "游戏行为", meta = (DisplayName = "When Action Tick"))
	void ReceiveWhenActionTick(float DeltaSeconds);
	UFUNCTION(BlueprintNativeEvent, Category = "游戏行为", meta = (DisplayName = "When Transition Failed"))
	void ReceiveWhenTransitionFailed(UGameActionSegmentBase* TransitionFailedSegment);

	UPROPERTY(meta = (GameActionSegmentEvent = true))
	FOnActionActivedEvent OnActionActivedEvent;
	UPROPERTY(meta = (GameActionSegmentEvent = true))
	FOnActionAbortedEvent OnActionAbortedEvent;
	UPROPERTY(meta = (GameActionSegmentEvent = true))
	FOnActionDeactivedEvent OnActionDeactivedEvent;
	UPROPERTY(meta = (GameActionSegmentEvent = true))
	FOnActionTickEvent OnActionTickEvent;
	UPROPERTY(meta = (GameActionSegmentEvent = true))
	FOnActionTransitionFailed OnActionTransitionFailed;
	
#if WITH_EDITOR
	friend class FGameActionCompilerContext;
#endif
	
	DECLARE_DYNAMIC_DELEGATE(FEvaluateExposedInputsEvent);
	UPROPERTY()
	FEvaluateExposedInputsEvent EvaluateExposedInputsEvent;
	// 调用该函数更新输入数据
	void EvaluateExposedInputs() { EvaluateExposedInputsEvent.ExecuteIfBound(); }

public:
	UPROPERTY()
	TArray<FGameActionTickTransition> TickTransitions;
	UPROPERTY()
	TArray<FGameActionEventTransition> EventTransitions;

protected:
	UFUNCTION(Client, Reliable)
	void DefaultTransitionFailedToClient();

	const static FName OnFinishedEventName;
public:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UObject* BPNodeTemplate;
	FORCEINLINE class UBPNode_GameActionSegmentBase* GetBPNodeTemplate() const { return (UBPNode_GameActionSegmentBase*)BPNodeTemplate; }

	UPROPERTY(EditDefaultsOnly, Category = "配置")
	TArray<FGameActionEventEntry> DefaultEvents;
#endif
};

// 不需要定制K2Node的行为继承该类型
UCLASS(abstract)
class GAMEACTION_RUNTIME_API UGameActionSegmentGeneric : public UGameActionSegmentBase
{
	GENERATED_BODY()

};

UCLASS(meta = (DisplayName = "行为片段"))
class GAMEACTION_RUNTIME_API UGameActionSegment : public UGameActionSegmentBase
{
	GENERATED_BODY()
public:
	UGameActionSegment(const FObjectInitializer& ObjectInitializer);

    UPROPERTY()
    UGameActionSequence* GameActionSequence = nullptr;

	UPROPERTY(EditAnywhere, Category = "配置", meta = (DisplayName = "播放至结尾时行为"))
	EGameActionPlayerEndAction PlayEndAction = EGameActionPlayerEndAction::Stop;

	UPROPERTY(EditAnywhere, Category = "配置", meta = (DisplayName = "播放速率"))
	float PlayRate = 1.f;
	
	void WhenActionActived() override;
	void WhenActionAborted() override;
	void WhenActionDeactived() override;
	void WhenTransitionFailed(UGameActionSegmentBase* TransitionFailedSegment) override;
	
	UFUNCTION(Client, Reliable)
	void TransitionFailedToClientSyncTime(float RollbackSeconds);
	UFUNCTION(Client, Reliable)
	void TransitionFailedToClientStopAction();
protected:
	UFUNCTION()
	void OnSequenceFinished();
	// 功能函数
public:
	UFUNCTION(BlueprintCallable, Category = "游戏行为")
	float GetCurrentPlayTime() const;
	UFUNCTION(BlueprintCallable, Category = "游戏行为")
	float GetSequenceTotalTime() const;
	UFUNCTION(BlueprintPure, meta = (BlueprintInternalUseOnly = true))
	bool IsInSequenceTime(const FFrameNumber Lower, const FFrameNumber Upper) const;
};
