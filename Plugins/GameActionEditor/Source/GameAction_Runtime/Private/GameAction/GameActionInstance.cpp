// Fill out your copyright notice in the Description page of Project Settings.


#include "GameAction/GameActionInstance.h"
#include <GameFramework/Character.h>
#include <Net/UnrealNetwork.h>
#include <Engine/BlueprintGeneratedClass.h>
#include <Evaluation/MovieScene3DTransformTemplate.h>
#include <Engine/ActorChannel.h>
#include <Engine/Engine.h>
#include <Engine/NetDriver.h>

#include "GameAction/GameActionComponent.h"
#include "GameAction/GameActionSegment.h"
#include "Sequence/GameActionSequencePlayer.h"
#include "Utils/GameAction_Log.h"

const FName UGameActionInstanceBase::GameActionOwnerName = TEXT("GameActionOwner");

UGameActionInstanceBase::UGameActionInstanceBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bSharePlayer(true)
{
#if WITH_EDITORONLY_DATA
	bIsSimulation = false;
#endif
}

void UGameActionInstanceBase::PostInitProperties()
{
	Super::PostInitProperties();

	if (IsTemplate() == false && bSharePlayer == false)
	{
		SequencePlayer = NewObject<UGameActionSequencePlayer>(this, GET_MEMBER_NAME_CHECKED(UGameActionInstanceBase, SequencePlayer));
	}
}

UWorld* UGameActionInstanceBase::GetWorld() const
{
#if WITH_EDITOR
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return nullptr;
	}
#endif
	return GetOwner()->GetWorld();
}

void UGameActionInstanceBase::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	if (UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(GetClass()))
	{
		BPClass->GetLifetimeBlueprintReplicationList(OutLifetimeProps);
	}

	DOREPLIFETIME(UGameActionInstanceBase, OwningComponent);
	DOREPLIFETIME_CONDITION(UGameActionInstanceBase, ActivedSegment, COND_SkipOwner);
}

int32 UGameActionInstanceBase::GetFunctionCallspace(UFunction* Function, FFrame* Stack)
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return FunctionCallspace::Local;
	}

	ACharacter* OwningCharacter = GetOwner();
	return OwningCharacter->GetFunctionCallspace(Function, Stack);
}

bool UGameActionInstanceBase::CallRemoteFunction(UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack)
{
	bool bProcessed = false;

	ACharacter* OwningCharacter = GetOwner();
	FWorldContext* const Context = GEngine->GetWorldContextFromWorld(GetWorld());
	if (Context != nullptr)
	{
		for (FNamedNetDriver& Driver : Context->ActiveNetDrivers)
		{
			if (Driver.NetDriver != nullptr && Driver.NetDriver->ShouldReplicateFunction(OwningCharacter, Function))
			{
				Driver.NetDriver->ProcessRemoteFunction(OwningCharacter, Function, Parameters, OutParms, Stack, this);
				bProcessed = true;
			}
		}
	}
	return bProcessed;
}

void UGameActionInstanceBase::ReplicateSubobject(bool& WroteSomething, class UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	if (bSharePlayer == false)
	{
		WroteSomething |= Channel->ReplicateSubobject(SequencePlayer, *Bunch, *RepFlags);
	}
}

ACharacter* UGameActionInstanceBase::GetOwner() const
{
	return GetTypedOuter<ACharacter>();
}

bool UGameActionInstanceBase::IsLocalControlled() const
{
#if WITH_EDITOR
	if (bIsSimulation)
	{
		return true;
	}
#endif
	return GetOwner()->IsLocallyControlled();
}

bool UGameActionInstanceBase::HasAuthority() const
{
	return GetOwner()->HasAuthority();
}

void UGameActionInstanceBase::Tick(float DeltaSeconds)
{
	if (bSharePlayer == false)
	{
		SequencePlayer->Update(DeltaSeconds);
	}

	if (ActivedSegment)
	{
		ActivedSegment->TickAction(DeltaSeconds);
	}
	
	ReceiveTick(DeltaSeconds);
}

void UGameActionInstanceBase::OnRep_OwningComponent()
{
	if (bSharePlayer && ensure(OwningComponent))
	{
		SequencePlayer = OwningComponent->SharedPlayer;
	}
}

bool UGameActionInstanceBase::TryStartEntry(const FGameActionEntry& Entry)
{
	if (ensure(IsActived() == false) == false)
	{
		return false;
	}
	else
	{
		if (bSharePlayer)
		{
			if (ensure(SequencePlayer->IsPlaying() == false) == false)
			{
				return false;
			}
		}
	}

	if (ensure(Entry.Transitions.Num() > 0))
	{
		for (const FGameActionEntryTransition& EntryTransition : Entry.Transitions)
		{
			if (EntryTransition.CanTransition(nullptr, false))
			{
				ActiveInstance();
				EntryTransition.TransitionToSegment->ActiveAction();
				if (HasAuthority())
				{
					if (IsLocalControlled() == false)
					{
						EnterActionToClient(EntryTransition.TransitionToSegment);
					}
				}
				else if (IsLocalControlled())
				{
					InvokeTransitionToServer(nullptr, ActivedSegment, EntryTransition.Condition.GetFunctionName());
				}
				return true;
			}
		}
	}
	return false;
}

bool UGameActionInstanceBase::TryStartDefaultEntry()
{
	return TryStartEntry(DefaultEntry);
}

void UGameActionInstanceBase::AbortGameAction()
{
	if (ensure(ActivedSegment))
	{
		if (HasAuthority())
		{
			AbortInstanceNetMulticast();
		}
		else if (ensure(IsLocalControlled()))
		{
			ActivedSegment->AbortAction();
			AbortInstance();
			AbortInstanceToServer();
		}
	}
}

bool UGameActionInstanceBase::TryEventTransition(FName EventName)
{
	if (ActivedSegment)
	{
		return ActivedSegment->InvokeEventTransition(EventName);
	}
	return false;
}

void UGameActionInstanceBase::ConstructInstance()
{
	GameAction_Log(Display, "创建[%s]行为", *GetName());
	WhenConstruct();
}

void UGameActionInstanceBase::ActiveInstance()
{
	GameAction_Log(Display, "[%s]行为实例激活", *GetName());
	WhenInstanceActived();
}

void UGameActionInstanceBase::DeactiveInstance()
{
	GameAction_Log(Display, "[%s]行为实例反激活", *GetName());

	for (AActor* Spawnable : InstanceManagedSpawnables)
	{
		if (ensure(Spawnable))
		{
			Spawnable->Destroy();
		}
	}
	InstanceManagedSpawnables.Empty();
	WhenInstanceDeactived();
	OnInstanceDeactivedNative.Broadcast(this, false);
}

void UGameActionInstanceBase::AbortInstance()
{
	GameAction_Log(Display, "[%s]行为实例被中断", *GetName());
	
	for (AActor* Spawnable : InstanceManagedSpawnables)
	{
		if (ensure(Spawnable))
		{
			Spawnable->Destroy();
		}
	}
	InstanceManagedSpawnables.Empty();
	WhenInstanceAborted();
	OnInstanceDeactivedNative.Broadcast(this, true);
}

void UGameActionInstanceBase::DestructInstance()
{
	GameAction_Log(Display, "销毁[%s]行为", *GetName());
	WhenDestruct();
}

void UGameActionInstanceBase::PushEnableSubStepMode()
{
	if (EnableSubStepMode == 0)
	{
		SequencePlayer->SubStepDuration = SubStepDuration;
	}
	EnableSubStepMode += 1;
	ensure(EnableSubStepMode != 0);
}

void UGameActionInstanceBase::PopEnableSubStepMode()
{
	ensure(EnableSubStepMode > 0);
	EnableSubStepMode -= 1;
	if (EnableSubStepMode == 0)
	{
		SequencePlayer->SubStepDuration = FLT_MAX;
	}
}

void UGameActionInstanceBase::OnRep_ActivedSegment(UGameActionSegmentBase* PreActivedSegment)
{
	UGameActionSegmentBase* CurrentActivedSegment = ActivedSegment;
	TGuardValue<UGameActionSegmentBase*> ActivedSegmentGuardValue(ActivedSegment, PreActivedSegment);
	if (PreActivedSegment && CurrentActivedSegment)
	{
		ActionTransition(PreActivedSegment, CurrentActivedSegment);
	}
	else if (CurrentActivedSegment)
	{
		ActiveInstance();
		CurrentActivedSegment->ActiveAction();
	}
	else if (PreActivedSegment)
	{
		PreActivedSegment->DeactiveAction();
		DeactiveInstance();
	}
}

void UGameActionInstanceBase::SyncSequenceOrigin()
{
	ActionTransformOrigin = GetOriginTransform();
}

void UGameActionInstanceBase::ReceiveWhenAborted_Implementation()
{
	WhenInstanceDeactived();
}

FTransform UGameActionInstanceBase::GetOriginTransform() const
{
	// 使用Owner的胶囊体底部坐标作为原点
	ACharacter* Owner = GetOwner();
	FTransform TransformOrigin = Owner->GetActorTransform();
	TransformOrigin.AddToTranslation(FVector(0.f, 0.f, -Owner->GetDefaultHalfHeight()));
	return TransformOrigin;
}

void UGameActionInstanceBase::TransformActorData(const FGameActionPossessableActorData& ActorData, FVector& WorldLocation, FRotator& WorldRotation) const
{
	const FTransform TransformOrigin = GetOriginTransform();
	WorldLocation = TransformOrigin.TransformPosition(ActorData.RelativeLocation);
	WorldRotation = TransformOrigin.TransformRotation(ActorData.RelativeRotation.Quaternion()).Rotator();
}

void UGameActionInstanceBase::ActionTransition(UGameActionSegmentBase* FromSegment, UGameActionSegmentBase* ToSegement)
{
	TGuardValue<bool> SpawnRegisterIsInActionTransitionGuard(FGameActionPlayerContext::bIsInActionTransition, true);
	FromSegment->DeactiveAction();
	ToSegement->ActiveAction();
}

void UGameActionInstanceBase::RollbackTransition(UGameActionSegmentBase* FromSegment, UGameActionSegmentBase* ToSegement)
{
	TGuardValue<bool> SpawnRegisterIsInActionTransitionGuard(FGameActionPlayerContext::bIsInActionTransition, true);
	FromSegment->DeactiveAction();
	ToSegement->ActiveAction();
}

bool UGameActionInstanceBase::CanTransition() const
{
	return GetWorld()->GetRealTimeSeconds() > PreRollbackTime + RollbackInterval;
}

void UGameActionInstanceBase::UpdateRollbackRecorder()
{
	PreRollbackTime = GetWorld()->GetRealTimeSeconds();
}

void UGameActionInstanceBase::FinishInstanceCommonPass()
{
	ActivedSegment->DeactiveAction();
	DeactiveInstance();
}

void UGameActionInstanceBase::FinishInstanceToServer_Implementation()
{
	FinishInstanceCommonPass();
}

void UGameActionInstanceBase::InvokeTransitionToServer_Implementation(UGameActionSegmentBase* FromSegment, UGameActionSegmentBase* ToSegment, const FName& ServerCheckFunctionName)
{
	if (ensure(ToSegment) == false)
	{
		return;
	}
	check(FromSegment == nullptr || FromSegment->GetOwner() == ToSegment->GetOwner());

	if (ServerCheckFunctionName != NAME_None)
	{
		FGameActionTransitionCondition TransitionCondition;
		TransitionCondition.BindUFunction(this, ServerCheckFunctionName);
		if (TransitionCondition.IsBound())
		{
			if (TransitionCondition.Execute(FromSegment, true) == false)
			{
				if (FromSegment)
				{
					GameAction_Log(Warning, "主控端预测失败，回退至[%s]行为", *FromSegment->GetName());
					FromSegment->TransitionActionFailed(ToSegment);
				}
				else
				{
					GameAction_Log(Warning, "主控端预测失败，取消行为执行");
					CancelActionToClient(ToSegment);
				}
				return;
			}
		}
	}

	GameAction_Log(Display, "主控端预测成功，广播[%s]行为", *ToSegment->GetName());
	if (ActivedSegment == FromSegment)
	{
		if (FromSegment == nullptr)
		{
			ActiveInstance();
			ToSegment->ActiveAction();
		}
		else
		{
			ActionTransition(ActivedSegment, ToSegment);
		}
	}
	else
	{
		if (ActivedSegment && FromSegment)
		{
			RollbackTransition(ActivedSegment, FromSegment);
			ActionTransition(FromSegment, ToSegment);
		}
		else if (ActivedSegment && ToSegment)
		{
			ActionTransition(ActivedSegment, ToSegment);
		}
		else
		{
			ToSegment->ActiveAction();
		}
	}
}

void UGameActionInstanceBase::CancelActionToClient_Implementation(UGameActionSegmentBase* Segment)
{
	if (ensure(Segment))
	{
		Segment->DeactiveAction();
	}
}

void UGameActionInstanceBase::EnterActionToClient_Implementation(UGameActionSegmentBase* ToSegment)
{
	if (ActivedSegment)
	{
		ActivedSegment->DeactiveAction();
	}
	if (ensure(ToSegment))
	{
		ActiveInstance();
		ToSegment->ActiveAction();
	}
}

void UGameActionInstanceBase::EntryCreatedActionToClient_Implementation(UGameActionComponent* Owner, UGameActionSegmentBase* ToSegment)
{
	if (ensure(ActivedSegment == nullptr) == false)
	{
		ActivedSegment->DeactiveAction();
	}
	if (ensure(ToSegment))
	{
		OwningComponent = Owner;
		if (bSharePlayer)
		{
			SequencePlayer = OwningComponent->SharedPlayer;
		}
		ActiveInstance();
		ToSegment->ActiveAction();
	}
}

void UGameActionInstanceBase::AbortInstanceToServer_Implementation()
{
	AbortInstanceNetMulticast();
}

void UGameActionInstanceBase::AbortInstanceNetMulticast_Implementation()
{
	if (ActivedSegment)
	{
		ActivedSegment->AbortAction();
		AbortInstance();
	}
}
