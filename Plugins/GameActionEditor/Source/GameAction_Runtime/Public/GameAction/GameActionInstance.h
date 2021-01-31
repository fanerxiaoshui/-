// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "IMovieScenePlaybackClient.h"
#include "UObject/NoExportTypes.h"
#include "GameAction/GameActionType.h"
#include "Engine/EngineTypes.h"
#include "Tracks/IMovieSceneTransformOrigin.h"
#include "GameActionInstance.generated.h"

class UGameActionSegmentBase;
class UGameActionSequencePlayer;
class ACharacter;
class UGameActionComponent;

/**
 * 
 */
UCLASS(abstract, Blueprintable, BlueprintType, meta = (DisplayName = "游戏行为基类"))
class GAMEACTION_RUNTIME_API UGameActionInstanceBase : public UObject, public IMovieScenePlaybackClient, public IMovieSceneTransformOrigin
{
	GENERATED_BODY()
public:
	const static FName GameActionOwnerName;

    UGameActionInstanceBase(const FObjectInitializer& ObjectInitializer);

	void PostInitProperties() override;
	UWorld* GetWorld() const override;
	bool IsSupportedForNetworking() const override { return true; }
	void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	int32 GetFunctionCallspace(UFunction* Function, FFrame* Stack) override;
	bool CallRemoteFunction(UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack) override;
	virtual void ReplicateSubobject(bool& WroteSomething, class UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags);

	UFUNCTION(BlueprintCallable, Category = "GameAction", meta = (CompactNodeTitle = "Owner"))
    ACharacter* GetOwner() const;
	UFUNCTION(BlueprintCallable, Category = "GameAction")
	bool IsLocalControlled() const;
	UFUNCTION(BlueprintCallable, Category = "GameAction")
	bool HasAuthority() const;

	virtual void Tick(float DeltaSeconds);

	UGameActionComponent* GetComponent() const { return OwningComponent; }
	UPROPERTY(Transient, ReplicatedUsing = OnRep_OwningComponent)
	UGameActionComponent* OwningComponent = nullptr;
	UFUNCTION()
	void OnRep_OwningComponent();

	UPROPERTY(BlueprintReadOnly, meta = (DisplayName = "默认入口"))
	FGameActionEntry DefaultEntry;

	// 启用的GameAction只能在Component激活一个，后续激活的Action会打断当前激活的
	UPROPERTY(EditDefaultsOnly, Category = "配置", meta = (DisplayName = "共享播放器"))
	uint8 bSharePlayer : 1;
	UPROPERTY()
	UGameActionSequencePlayer* SequencePlayer = nullptr;

	UPROPERTY(EditAnywhere, Category = "配置")
	float SubStepDuration = 1.f / 60.f;
	uint8 EnableSubStepMode = 0;
	UFUNCTION(BlueprintCallable, Category = "GameAction")
	void PushEnableSubStepMode();
	UFUNCTION(BlueprintCallable, Category = "GameAction")
	void PopEnableSubStepMode();

	UPROPERTY(ReplicatedUsing = OnRep_ActivedSegment)
	UGameActionSegmentBase* ActivedSegment = nullptr;
	UFUNCTION()
	void OnRep_ActivedSegment(UGameActionSegmentBase* PreActivedSegment);

	UFUNCTION(BlueprintCallable, Category = "GameAction")
	bool TryStartEntry(const FGameActionEntry& Entry);
	UFUNCTION(BlueprintCallable, Category = "GameAction")
	bool TryStartDefaultEntry();
	UFUNCTION(BlueprintCallable, Category = "GameAction")
	void AbortGameAction();
	UFUNCTION(BlueprintCallable, Category = "GameAction")
	bool IsActived() const { return ActivedSegment != nullptr; }
	UFUNCTION(BlueprintCallable, Category = "GameAction")
	bool TryEventTransition(FName EventName);

	void ConstructInstance();
	void ActiveInstance();
	void DeactiveInstance();
	void AbortInstance();
	void DestructInstance();

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnInstanceDeactived, UGameActionInstanceBase*, bool/*IsAbort*/);
	FOnInstanceDeactived OnInstanceDeactivedNative;
	
	UPROPERTY(Transient)
	TArray<AActor*> InstanceManagedSpawnables;

#if WITH_EDITORONLY_DATA
	uint8 bIsSimulation : 1;

	UPROPERTY(EditDefaultsOnly, Category = "配置")
	TArray<FGameActionEventEntry> DefaultEvents;
#endif

	void SyncSequenceOrigin();
protected:
#if WITH_EDITOR
	friend class UGameActionBlueprintFactory;
#endif

	virtual void WhenConstruct() { ReceiveWhenConstruct(); }
	virtual void WhenInstanceActived() { ReceiveWhenActived(); }
	virtual void WhenInstanceDeactived() { ReceiveWhenDeactived(); }
	virtual void WhenInstanceAborted() { ReceiveWhenAborted(); }
	virtual void WhenDestruct() { ReceiveWhenDestruct(); }

	UFUNCTION(BlueprintImplementableEvent, Category = "GameAction", meta = (DisplayName = "When Construct"))
	void ReceiveWhenConstruct();

	UFUNCTION(BlueprintImplementableEvent, Category = "GameAction", meta = (DisplayName = "When Destruct"))
	void ReceiveWhenDestruct();
	
	UFUNCTION(BlueprintImplementableEvent, Category = "GameAction", meta = (DisplayName = "When Actived"))
	void ReceiveWhenActived();

	UFUNCTION(BlueprintImplementableEvent, Category = "GameAction", meta = (DisplayName = "When Deactived"))
	void ReceiveWhenDeactived();

	UFUNCTION(BlueprintNativeEvent, Category = "GameAction", meta = (DisplayName = "When Aborted"))
	void ReceiveWhenAborted();
	
	UFUNCTION(BlueprintImplementableEvent, Category = "GameAction", meta = (DisplayName = "Tick"))
	void ReceiveTick(float DeltaSeconds);
protected:
	UFUNCTION(BlueprintCallable, Category = "GameAction", meta = (CompactNodeTitle = "Origin Transform", HidePin = "Target"))
	FTransform GetOriginTransform() const;
	
	UFUNCTION(BlueprintCallable, Category = "GameAction", meta = (CompactNodeTitle = "To World", HidePin = "Target"))
	void TransformActorData(const FGameActionPossessableActorData& ActorData, FVector& WorldLocation, FRotator& WorldRotation) const;

public:
	void ActionTransition(UGameActionSegmentBase* FromSegment, UGameActionSegmentBase* ToSegement);
	void RollbackTransition(UGameActionSegmentBase* FromSegment, UGameActionSegmentBase* ToSegement);
	// 服务器通知失败后客户端延迟一下再进行跳转判断
	float PreRollbackTime = TNumericLimits<float>::Lowest();
	static constexpr float RollbackInterval = 0.1f;
	bool CanTransition() const;
	// 主控端执行回退时记录下当前回退的时间戳，防止频繁调用失败的跳转
	void UpdateRollbackRecorder();

	UFUNCTION(Server, Reliable)
	void InvokeTransitionToServer(UGameActionSegmentBase* FromSegment, UGameActionSegmentBase* ToSegment, const FName& ServerCheckFunctionName);
	UFUNCTION(Client, Reliable)
	void CancelActionToClient(UGameActionSegmentBase* Segment);

	void FinishInstanceCommonPass();
	UFUNCTION(Server, Reliable)
	void FinishInstanceToServer();
	
	UFUNCTION(Client, Reliable)
	void EnterActionToClient(UGameActionSegmentBase* ToSegment);

	// 这种实现存在问题，RPC速度快于属性同步，激活时所需的数据可能还没下发
	UFUNCTION(Client, Reliable)
	void EntryCreatedActionToClient(UGameActionComponent* Owner, UGameActionSegmentBase* ToSegment);
	
	UFUNCTION(Server, Reliable)
	void AbortInstanceToServer();
	UFUNCTION(NetMulticast, Reliable)
	void AbortInstanceNetMulticast();
protected:
	// IMovieScenePlaybackClient
	bool RetrieveBindingOverrides(const FGuid& InBindingId, FMovieSceneSequenceID InSequenceID, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const override { return false; }
	UObject* GetInstanceData() const override { return const_cast<UGameActionInstanceBase*>(this); }
	// IMovieScenePlaybackClient

	// IMovieSceneTransformOrigin
	FTransform NativeGetTransformOrigin() const override { return ActionTransformOrigin; }
	// IMovieSceneTransformOrigin

public:
	UPROPERTY()
	FTransform ActionTransformOrigin;
};
