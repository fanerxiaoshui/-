// Fill out your copyright notice in the Description page of Project Settings.


#include "GameAction/GameActionSegment.h"
#include <Engine/BlueprintGeneratedClass.h>
#include <GameFramework/Character.h>
#include <GameFramework/PlayerState.h>
#include <Engine/Engine.h>
#include <Engine/NetDriver.h>
#include <GameFramework/Character.h>

#include "GameAction/GameActionComponent.h"
#include "GameAction/GameActionInstance.h"
#include "Sequence/GameActionSequence.h"
#include "Sequence/GameActionSequencePlayer.h"
#include "Utils/GameAction_Log.h"

#define LOCTEXT_NAMESPACE "GameActionSegment"

const FName UGameActionSegmentBase::OnFinishedEventName = TEXT("OnFinished");

UGameActionSegmentBase::UGameActionSegmentBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	DefaultEvents.Add(FGameActionEventEntry(OnFinishedEventName, LOCTEXT("当播放结束", "当播放结束")));
#endif
}

UWorld* UGameActionSegmentBase::GetWorld() const
{
	return GetOwner()->GetWorld();
}

void UGameActionSegmentBase::ActiveAction()
{
	GameAction_Log(Display, "激活游戏动作 [%s]", *GetName());
	
	UGameActionInstanceBase* Instance = GetOwner();
	check(Instance->ActivedSegment == nullptr);
	Instance->ActivedSegment = this;
	WhenActionActived();
	check(IsActived());
	OnActionActivedEvent.ExecuteIfBound();
}

void UGameActionSegmentBase::AbortAction()
{
	GameAction_Log(Display, "中断游戏动作 [%s]", *GetName());

	UGameActionInstanceBase* Instance = GetOwner();
	check(Instance->ActivedSegment == this);
	Instance->ActivedSegment = nullptr;
	WhenActionAborted();
	check(IsActived() == false);
	OnActionAbortedEvent.ExecuteIfBound();
}

void UGameActionSegmentBase::DeactiveAction()
{
	GameAction_Log(Display, "反激活游戏动作 [%s]", *GetName());
	
	check(GetOwner()->ActivedSegment == this);
	GetOwner()->ActivedSegment = nullptr;
	WhenActionDeactived();
	check(IsActived() == false);
	OnActionDeactivedEvent.ExecuteIfBound();
}

namespace SegmentUtils
{
	using FTickTransitionVisited = TArray<const FGameActionTickTransition*, TInlineAllocator<2>>;
	struct FTickTransitionTracer
	{
		FTickTransitionVisited& Visited;
		FTickTransitionTracer(FTickTransitionVisited& Visited)
			: Visited(Visited)
		{}

		const FGameActionTickTransition& Transition(const FGameActionTickTransition& TickTransition)
		{
			Visited.Add(&TickTransition);

			UGameActionSegmentBase* Segment = TickTransition.TransitionToSegment;
			for (const FGameActionTickTransition& NextTickTransition : Segment->TickTransitions)
			{
				if (NextTickTransition.CanTransition(Segment, false))
				{
					if (ensure(Visited.Contains(&NextTickTransition) == false))
					{
						return Transition(NextTickTransition);
					}
				}
			}
			return TickTransition;
		}
	};
}

void UGameActionSegmentBase::TickAction(float DeltaSeconds)
{
	WhenActionTick(DeltaSeconds);
	check(IsActived());
	OnActionTickEvent.ExecuteIfBound(DeltaSeconds);

	if (IsLocalControlled())
	{
		UGameActionInstanceBase* Instance = GetOwner();
		if (IsActived() == false || Instance->CanTransition() == false)
		{
			return;
		}
		for (const FGameActionTickTransition& TickTransition : TickTransitions)
		{
			if (TickTransition.CanTransition(this, false))
			{
				TGuardValue<bool> SpawnRegisterIsInActionTransitionGuard(FGameActionPlayerContext::bIsInActionTransition, true);
				DeactiveAction();
				
				SegmentUtils::FTickTransitionVisited Visited;
				SegmentUtils::FTickTransitionTracer TickTransitionTracer(Visited);
				const FGameActionTickTransition& LastTransition = TickTransitionTracer.Transition(TickTransition);

#if !UE_BUILD_SHIPPING
				if (Visited.Num() > 1)
				{
					FString IgnoreSegments = TEXT("|");
					for (int32 Idx = 0; Idx < Visited.Num() - 1; ++Idx)
					{
						IgnoreSegments += Visited[Idx]->TransitionToSegment->GetName() + TEXT("|");
					}
					GameAction_Log(Display, "因为跳转条件允许，跳过了 %s 中间片段", *IgnoreSegments);
				}
#endif

				LastTransition.TransitionToSegment->ActiveAction();
				if (HasAuthority() == false)
				{
					Instance->InvokeTransitionToServer(this, LastTransition.TransitionToSegment, LastTransition.Condition.GetFunctionName());
				}
				
				break;
			}
		}
	}
}

void UGameActionSegmentBase::TransitionActionFailed(UGameActionSegmentBase* TransitionFailedSegment)
{
	WhenTransitionFailed(TransitionFailedSegment);
	OnActionTransitionFailed.ExecuteIfBound();
}

void UGameActionSegmentBase::TryFinishActionOrTransition()
{
	GameAction_Log(Display, "结束游戏动作 [%s]", *GetName());
	
	if (ensure(IsLocalControlled()))
	{
		const bool TransitionSucceed = InvokeEventTransition(OnFinishedEventName);
		if (TransitionSucceed == false)
		{
			UGameActionInstanceBase* Instance = GetOwner();
			check(Instance->ActivedSegment == this);

			Instance->FinishInstanceCommonPass();
			if (HasAuthority() == false)
			{
				Instance->FinishInstanceToServer();
			}
		}
	}
}

bool UGameActionSegmentBase::InvokeEventTransition(const FName& EventName)
{
	UGameActionInstanceBase* Instance = GetOwner();
	if (Instance->CanTransition() == false)
	{
		return false;
	}
	for (const FGameActionEventTransition& EventTransition : EventTransitions)
	{
		if (EventTransition.EventName == EventName)
		{
			if (EventTransition.CanTransition(this, false))
			{
				TGuardValue<bool> SpawnRegisterIsInActionTransitionGuard(FGameActionPlayerContext::bIsInActionTransition, true);
				DeactiveAction();

				for (const FGameActionTickTransition& TickTransition : TickTransitions)
				{
					if (TickTransition.CanTransition(this, false))
					{
						SegmentUtils::FTickTransitionVisited Visited;
						SegmentUtils::FTickTransitionTracer TickTransitionTracer(Visited);
						const FGameActionTickTransition& LastTransition = TickTransitionTracer.Transition(TickTransition);

#if !UE_BUILD_SHIPPING
						FString IgnoreSegments = TEXT("|") + EventTransition.TransitionToSegment->GetName();
						if (Visited.Num() > 1)
						{
							for (int32 Idx = 0; Idx < Visited.Num() - 1; ++Idx)
							{
								IgnoreSegments += Visited[Idx]->TransitionToSegment->GetName() + TEXT("|");
							}
						}
						else
						{
							IgnoreSegments += TEXT("|");
						}
						GameAction_Log(Display, "因为跳转条件允许，跳过了 %s 中间片段", *IgnoreSegments);
#endif

						LastTransition.TransitionToSegment->ActiveAction();
						if (HasAuthority() == false)
						{
							Instance->InvokeTransitionToServer(this, LastTransition.TransitionToSegment, LastTransition.Condition.GetFunctionName());
						}
						return true;
					}
				}
				
				EventTransition.TransitionToSegment->ActiveAction();
				if (HasAuthority() == false)
				{
					Instance->InvokeTransitionToServer(this, EventTransition.TransitionToSegment, EventTransition.Condition.GetFunctionName());
				}
				return true;
			}
			else
			{
				return false;
			}
		}
	}
	return false;
}

void UGameActionSegmentBase::ReceiveWhenActionAborted_Implementation()
{
	WhenActionDeactived();
}

void UGameActionSegmentBase::ReceiveWhenTransitionFailed_Implementation(UGameActionSegmentBase* TransitionFailedSegment)
{
	DefaultTransitionFailedToClient();
}

void UGameActionSegmentBase::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	if (UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(GetClass()))
	{
		BPClass->GetLifetimeBlueprintReplicationList(OutLifetimeProps);
	}
}

int32 UGameActionSegmentBase::GetFunctionCallspace(UFunction* Function, FFrame* Stack)
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return FunctionCallspace::Local;
	}
	
	ACharacter* OwningCharacter = GetOwner()->GetOwner();
	return OwningCharacter->GetFunctionCallspace(Function, Stack);
}

bool UGameActionSegmentBase::CallRemoteFunction(UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack)
{
	bool bProcessed = false;

	ACharacter* OwningCharacter = GetOwner()->GetOwner();
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

UGameActionInstanceBase* UGameActionSegmentBase::GetOwner() const
{
	return CastChecked<UGameActionInstanceBase>(GetOuter());
}

ACharacter* UGameActionSegmentBase::GetOwningCharacter() const
{
	check(GetOwner());
	return GetOwner()->GetOwner();
}

bool UGameActionSegmentBase::HasAuthority() const
{
	return GetOwner()->HasAuthority();
}

bool UGameActionSegmentBase::IsLocalControlled() const
{
	return GetOwner()->IsLocalControlled();
}

bool UGameActionSegmentBase::IsActived() const
{
	return GetOwner()->ActivedSegment == this;
}

void UGameActionSegmentBase::DefaultTransitionFailedToClient_Implementation()
{
	UGameActionInstanceBase* Instance = GetOwner();
	Instance->UpdateRollbackRecorder();
	if (UGameActionSegmentBase* FailedActionSegment = Instance->ActivedSegment)
	{
		Instance->RollbackTransition(FailedActionSegment, this);
	}
	else
	{
		ActiveAction();
	}
}

UGameActionSegment::UGameActionSegment(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GameActionSequence = CreateDefaultSubobject<UGameActionSequence>(GET_MEMBER_NAME_CHECKED(UGameActionSegment, GameActionSequence));
}

void UGameActionSegment::WhenActionActived()
{
	UGameActionInstanceBase* Owner = GetOwner();
	UGameActionSequencePlayer* SequencePlayer = Owner->SequencePlayer;
	
	SequencePlayer->Initialize(GetOwner(), GameActionSequence, PlayRate, PlayEndAction);
	Owner->SyncSequenceOrigin();
	SequencePlayer->Play();

	if (IsLocalControlled())
	{
		SequencePlayer->OnFinished.AddUObject(this, &UGameActionSegment::OnSequenceFinished);
	}
}

void UGameActionSegment::WhenActionAborted()
{
	TGuardValue<bool> SpawnRegisterIsPlayFinishedGuard(FGameActionPlayerContext::bIsPlayAborted, true);
	WhenActionDeactived();
}

void UGameActionSegment::WhenActionDeactived()
{
	UGameActionSequencePlayer* SequencePlayer = GetOwner()->SequencePlayer;
	if (SequencePlayer->IsPlaying())
	{
		SequencePlayer->Stop();
	}

	if (IsLocalControlled())
	{
		SequencePlayer->OnFinished.RemoveAll(this);
	}
}

void UGameActionSegment::WhenTransitionFailed(UGameActionSegmentBase* TransitionFailedSegment)
{
	UGameActionSequencePlayer* SequencePlayer = GetOwner()->SequencePlayer;
	if (SequencePlayer->IsPlaying())
	{
		const float Seconds = SequencePlayer->GetCurrentTime().AsSeconds();
		TransitionFailedToClientSyncTime(Seconds);
	}
	else
	{
		TransitionFailedToClientStopAction();
	}
}

void UGameActionSegment::TransitionFailedToClientSyncTime_Implementation(float RollbackSeconds)
{
	UGameActionInstanceBase* Instance = GetOwner();
	Instance->UpdateRollbackRecorder();
	if (UGameActionSegmentBase* FailedActionSegment = Instance->ActivedSegment)
	{
		Instance->RollbackTransition(FailedActionSegment, this);
	}
	else
	{
		ActiveAction();
	}
	UGameActionSequencePlayer* SequencePlayer = Instance->SequencePlayer;

	if (UWorld* World = GetWorld())
	{
		if (APlayerState* PlayerState = GetOwner()->GetOwner()->GetPlayerState())
		{
			const float PingMs = PlayerState->ExactPing;
			RollbackSeconds += (PingMs / 1000.f);
		}
	}

	SequencePlayer->JumpToSeconds(RollbackSeconds);
}

void UGameActionSegment::TransitionFailedToClientStopAction_Implementation()
{
	UGameActionInstanceBase* Instance = GetOwner();
	Instance->UpdateRollbackRecorder();
	if (UGameActionSegmentBase* FailedActionSegment = Instance->ActivedSegment)
	{
		FailedActionSegment->DeactiveAction();
	}
}

void UGameActionSegment::OnSequenceFinished()
{
	UGameActionSequencePlayer* SequencePlayer = GetOwner()->SequencePlayer;
	SequencePlayer->OnFinished.RemoveAll(this);
	TryFinishActionOrTransition();
}

float UGameActionSegment::GetCurrentPlayTime() const
{
	UGameActionSequencePlayer* SequencePlayer = GetOwner()->SequencePlayer;
	const FQualifiedFrameTime CurrentTime = SequencePlayer->GetCurrentTime();
	return CurrentTime.AsSeconds();
}

float UGameActionSegment::GetSequenceTotalTime() const
{
	UMovieScene* MovieScene = GameActionSequence->GetMovieScene();
	return MovieScene->GetTickResolution().AsSeconds(MovieScene->GetPlaybackRange().Size<FFrameNumber>());
}

bool UGameActionSegment::IsInSequenceTime(const FFrameNumber Lower, const FFrameNumber Upper) const
{
	UGameActionSequencePlayer* Player = GetOwner()->SequencePlayer;
	const FQualifiedFrameTime CurrentTime = Player->GetCurrentTime();
	const FFrameNumber CurrentFrame = CurrentTime.ConvertTo(FFrameRate(60000, 1)).GetFrame();
	const bool IsInRange = TRange<FFrameNumber>(Lower, Upper).Contains(CurrentFrame);
	return IsInRange;
}

#undef LOCTEXT_NAMESPACE
