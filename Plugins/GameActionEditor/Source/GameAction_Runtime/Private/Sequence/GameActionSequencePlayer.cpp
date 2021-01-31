// Fill out your copyright notice in the Description page of Project Settings.


#include "Sequence/GameActionSequencePlayer.h"
#include <Particles/ParticleSystemComponent.h>
#include <Misc/ScopeExit.h>
#include <Evaluation/MovieScene3DTransformTemplate.h>
#include <GameFramework/Character.h>
#include <Camera/CameraComponent.h>
#include <MovieSceneTimeHelpers.h>
#include <Net/UnrealNetwork.h>
#include <GameFramework/PlayerState.h>
#include <Engine/Engine.h>
#include <Engine/NetDriver.h>
#include <Engine/NetConnection.h>
#include <Camera/PlayerCameraManager.h>
#include <GameFramework/PlayerController.h>

#include "GameAction/GameActionInstance.h"
#include "GameAction/GameActionSegment.h"
#include "Sequence/GameActionDynamicSpawnTrack.h"
#include "Sequence/GameActionSequence.h"
#include "Sequence/GameActionSequenceCustomSpawner.h"
#include "Utils/GameAction_Log.h"

UGameActionSequenceCustomSpawnerBase* FGameActionPlayerContext::CurrentSpawner = nullptr;
bool FGameActionPlayerContext::bIsInActionTransition = false;
bool FGameActionPlayerContext::bIsPlayFinished = false;
bool FGameActionPlayerContext::bIsPlayAborted = false;
const UGameActionSequenceSpawnerSettingsBase* FGameActionPlayerContext::CurrentSpawnerSettings = nullptr;

FGameActionSpawnRegister::FGameActionSpawnRegister()
{

}

UObject* FGameActionSpawnRegister::SpawnObject(FMovieSceneSpawnable& Spawnable, FMovieSceneSequenceIDRef TemplateID, IMovieScenePlayer& Player)
{
	UObject* ObjectTemplate = Spawnable.GetObjectTemplate();
	AActor* ActorTemplate = Cast<AActor>(Spawnable.GetObjectTemplate());
	if (ensure(ActorTemplate) == false)
	{
		return nullptr;
	}

	UGameActionInstanceBase* GameActionInstance = CastChecked<UGameActionInstanceBase>(Player.GetPlaybackContext());
	// 会网络同步的Actor客户端不用Spawn
	if (GameActionInstance->GetOwner()->HasAuthority() == false)
	{
		if (ActorTemplate->GetIsReplicated())
		{
			return nullptr;
		}
	}

	AActor* LocalSpawnableRef = nullptr;
	AActor** P_Spawnable = &LocalSpawnableRef;
	if (FGameActionPlayerContext::CurrentSpawnerSettings->bAsReference)
	{
		FObjectProperty* ObjectProperty = FindFProperty<FObjectProperty>(GameActionInstance->GetClass(), ActorTemplate->GetFName());
		if (ensure(ObjectProperty) == false)
		{
			return nullptr;
		}
		P_Spawnable = ObjectProperty->ContainerPtrToValuePtr<AActor*>(GameActionInstance);
		if (ensure(P_Spawnable) == false)
		{
			return nullptr;
		}
		if (::IsValid(*P_Spawnable))
		{
			return *P_Spawnable;
		}
	}
	AActor*& SpawnableRef = *P_Spawnable;

	// 转换为世界坐标
	const FTransform Origin = GameActionInstance->ActionTransformOrigin;
	if (FGameActionPlayerContext::CurrentSpawner == nullptr)
	{
		// TODO：可配置是否为Transient，处理召唤物的情况
		const EObjectFlags ObjectFlags = RF_Transient;

		UWorld* World = GameActionInstance->GetWorld();

		FActorSpawnParameters SpawnParameters;
		{
			SpawnParameters.ObjectFlags = ObjectFlags;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParameters.bDeferConstruction = true;
			SpawnParameters.Template = ActorTemplate;
			SpawnParameters.OverrideLevel = GameActionInstance->GetOwner()->GetLevel();
		}

		FTransform SpawnTransform;
		if (USceneComponent* RootComponent = ActorTemplate->GetRootComponent())
		{
			SpawnTransform.SetTranslation(RootComponent->GetRelativeLocation());
			SpawnTransform.SetRotation(RootComponent->GetRelativeRotation().Quaternion());
		}
		else
		{
			SpawnTransform = Spawnable.SpawnTransform;
		}
		SpawnTransform *= Origin;

		// Disable all particle components so that they don't auto fire as soon as the actor is spawned. The particles should be triggered through the particle track.
		for (UActorComponent* Component : ActorTemplate->GetComponents())
		{
			if (UParticleSystemComponent* ParticleComponent = Cast<UParticleSystemComponent>(Component))
			{
				ParticleComponent->SetActiveFlag(false);
				ParticleComponent->bAutoActivate = false;
			}
		}

		SpawnableRef = World->SpawnActorAbsolute(ActorTemplate->GetClass(), SpawnTransform, SpawnParameters);
		if (!SpawnableRef)
		{
			return nullptr;
		}

		// Ensure this spawnable is not a preview actor. Preview actors will not have BeginPlay() called on them.
#if WITH_EDITOR
		SpawnableRef->bIsEditorPreviewActor = false;

		if (GIsEditor)
		{
			// Explicitly set RF_Transactional on spawned actors so we can undo/redo properties on them. We don't add this as a spawn flag since we don't want to transact spawn/destroy events.
			SpawnableRef->SetFlags(RF_Transactional);

			for (UActorComponent* Component : SpawnableRef->GetComponents())
			{
				if (Component)
				{
					Component->SetFlags(RF_Transactional);
				}
			}
		}
#endif
		const bool bIsDefaultTransform = true;
		SpawnableRef->FinishSpawning(SpawnTransform, bIsDefaultTransform);
	}
	else
	{
		SpawnableRef = FGameActionPlayerContext::CurrentSpawner->SpawnCustomActor(ActorTemplate, Spawnable, GameActionInstance, Origin);
	}

	if (SpawnableRef)
	{
#if WITH_EDITOR
		if (GIsEditor)
		{
			SpawnableRef->SetActorLabel(ObjectTemplate->GetName());
		}
#endif
		SpawnOwnershipMap.Add(SpawnableRef, FGameActionPlayerContext::CurrentSpawnerSettings);
		if (FGameActionPlayerContext::CurrentSpawnerSettings->Ownership == EGameActionSpawnOwnership::Instance)
		{
			GameActionInstance->InstanceManagedSpawnables.Add(SpawnableRef);
		}
	}
	return SpawnableRef;
}

void FGameActionSpawnRegister::DestroySpawnedObject(UObject& Object)
{
	AActor* Actor = Cast<AActor>(&Object);
	if (!ensure(Actor))
	{
		return;
	}

	// 会网络同步的Actor客户端不用销毁，服务器会处理
	if (Actor->HasAuthority() == false && Actor->GetIsReplicated())
	{
		return;
	}

	const UGameActionSequenceSpawnerSettingsBase* SpawnSection = SpawnOwnershipMap.FindRef(Actor);
	if (SpawnSection->bAsReference)
	{
		if (FGameActionPlayerContext::bIsPlayAborted)
		{
			if (SpawnSection->bDestroyWhenAborted == false)
			{
				return;
			}
		}
		else if (FGameActionPlayerContext::bIsInActionTransition || FGameActionPlayerContext::bIsPlayFinished)
		{
			// 假如所有权不为Sequence，不销毁
			if (SpawnSection->Ownership != EGameActionSpawnOwnership::Sequence)
			{
				return;
			}
		}
	}

#if WITH_EDITOR
	if (GIsEditor)
	{
		// Explicitly remove RF_Transactional on spawned actors since we don't want to trasact spawn/destroy events
		Actor->ClearFlags(RF_Transactional);
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component)
			{
				Component->ClearFlags(RF_Transactional);
			}
		}
	}
#endif

	SpawnOwnershipMap.Remove(Actor);
	if (SpawnSection->DestroyDelayTime <= 0.f)
	{
		Actor->Destroy();
	}
	else
	{
		Actor->SetLifeSpan(SpawnSection->DestroyDelayTime);
	}
}

UGameActionSequencePlayer::UGameActionSequencePlayer()
	: PlayEndAction(EGameActionPlayerEndAction::Stop)
{
	
}

void UGameActionSequencePlayer::Initialize(UGameActionInstanceBase* InGameAction, UGameActionSequence* InSequence, float InPlayRate, EGameActionPlayerEndAction EndAction)
{
	check(InSequence);
	check(!bIsEvaluating);

	if (Sequence)
	{
		StopAtCurrentTime();
	}

	GameAction = InGameAction;
	
	PlayEndAction = EndAction;
	PlayRate = InPlayRate;
	
	Sequence = InSequence;

	FFrameTime StartTimeWithOffset = StartTime;

	EUpdateClockSource ClockToUse = EUpdateClockSource::Tick;

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (MovieScene)
	{
		EMovieSceneEvaluationType EvaluationType = MovieScene->GetEvaluationType();
		FFrameRate                TickResolution = MovieScene->GetTickResolution();
		FFrameRate                DisplayRate = MovieScene->GetDisplayRate();

		UE_LOG(GameAction_Log, Verbose, TEXT("Initialize - GameActionSequence: %s, TickResolution: %f, DisplayRate: %d, CurrentTime: %d"), *InSequence->GetTypedOuter<UGameActionSegmentBase>()->GetName(), TickResolution.Numerator, DisplayRate.Numerator);

		// We set the play position in terms of the display rate,
		// but want evaluation ranges in the moviescene's tick resolution
		PlayPosition.SetTimeBase(DisplayRate, TickResolution, EvaluationType);

		{
			// Set up the default frame range from the sequence's play range
			TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();

			const FFrameNumber SrcStartFrame = UE::MovieScene::DiscreteInclusiveLower(PlaybackRange);
			const FFrameNumber SrcEndFrame = UE::MovieScene::DiscreteExclusiveUpper(PlaybackRange);

			const FFrameNumber StartingFrame = ConvertFrameTime(SrcStartFrame, TickResolution, DisplayRate).FloorToFrame();
			const FFrameNumber EndingFrame = ConvertFrameTime(SrcEndFrame, TickResolution, DisplayRate).FloorToFrame();

			SetFrameRange(StartingFrame.Value, (EndingFrame - StartingFrame).Value);
		}

		StartTimeWithOffset = StartTime;

		ClockToUse = MovieScene->GetClockSource();

		if (ClockToUse == EUpdateClockSource::Custom)
		{
			TimeController = MovieScene->MakeCustomTimeController(GetPlaybackContext());
		}
	}

	if (!TimeController.IsValid())
	{
		switch (ClockToUse)
		{
		case EUpdateClockSource::Audio:    TimeController = MakeShared<FMovieSceneTimeController_AudioClock>();    break;
		case EUpdateClockSource::Platform: TimeController = MakeShared<FMovieSceneTimeController_PlatformClock>(); break;
		case EUpdateClockSource::RelativeTimecode: TimeController = MakeShared<FMovieSceneTimeController_RelativeTimecodeClock>(); break;
		case EUpdateClockSource::Timecode: TimeController = MakeShared<FMovieSceneTimeController_TimecodeClock>(); break;
		default:                           TimeController = MakeShared<FMovieSceneTimeController_Tick>();          break;
		}

		if (!ensureMsgf(TimeController.IsValid(), TEXT("No time controller specified for sequence playback. Falling back to Engine Tick clock source.")))
		{
			TimeController = MakeShared<FMovieSceneTimeController_Tick>();
		}
	}

	RootTemplateInstance.Initialize(*Sequence, *this, nullptr);

	// Set up playback position (with offset) after Stop(), which will reset the starting time to StartTime
	PlayPosition.Reset(StartTimeWithOffset);
	TimeController->Reset(GetCurrentTime());
}

void UGameActionSequencePlayer::SetFrameRange(int32 NewStartTime, int32 Duration)
{
	Duration = FMath::Max(Duration, 0);

	StartTime = NewStartTime;
	DurationFrames = Duration;

	TOptional<FFrameTime> CurrentTime = PlayPosition.GetCurrentPosition();
	if (CurrentTime.IsSet())
	{
		FFrameTime LastValidTime = GetLastValidTime();

		if (CurrentTime.GetValue() < StartTime)
		{
			PlayPosition.Reset(StartTime);
		}
		else if (CurrentTime.GetValue() > LastValidTime)
		{
			PlayPosition.Reset(LastValidTime);
		}
	}

	if (TimeController.IsValid())
	{
		TimeController->Reset(GetCurrentTime());
	}

	UpdateNetworkSyncProperties();
}

void UGameActionSequencePlayer::PlayInternal()
{
	if (bIsEvaluating)
	{
		LatentActions.Emplace(FLatentAction::EType::Play);
		return;
	}

	if (!IsPlaying() && Sequence)
	{
		// If at the end and playing forwards, rewind to beginning
		if (GetCurrentTime().Time == GetLastValidTime())
		{
			if (PlayRate > 0.f)
			{
				JumpToFrame(FFrameTime(StartTime));
			}
		}
		else if (GetCurrentTime().Time == FFrameTime(StartTime))
		{
			if (PlayRate < 0.f)
			{
				JumpToFrame(GetLastValidTime());
			}
		}

		// Start playing
		// @todo Sequencer playback: Should we recreate the instance every time?
		// We must not recreate the instance since it holds stateful information (such as which objects it has spawned). Recreating the instance would break any 
		// @todo: Is this still the case now that eval state is stored (correctly) in the player?
		if (!RootTemplateInstance.IsValid())
		{
			RootTemplateInstance.Initialize(*Sequence, *this, nullptr);
		}

		PreAnimatedState.EnableGlobalCapture();

		bPendingOnStartedPlaying = true;
		Status = EMovieScenePlayerStatus::Playing;
		TimeController->StartPlaying(GetCurrentTime());

		UMovieSceneSequence* MovieSceneSequence = RootTemplateInstance.GetSequence(MovieSceneSequenceID::Root);

		if (PlayPosition.GetEvaluationType() == EMovieSceneEvaluationType::FrameLocked)
		{
			if (!OldMaxTickRate.IsSet())
			{
				OldMaxTickRate = GEngine->GetMaxFPS();
			}

			GEngine->SetMaxFPS(1.f / PlayPosition.GetInputRate().AsInterval());
		}

		if (!PlayPosition.GetLastPlayEvalPostition().IsSet() || PlayPosition.GetLastPlayEvalPostition() != PlayPosition.GetCurrentPosition())
		{
			UpdateMovieSceneInstance(PlayPosition.PlayTo(PlayPosition.GetCurrentPosition()), EMovieScenePlayerStatus::Playing);
		}

		UpdateNetworkSyncProperties();

#if !UE_BUILD_SHIPPING
		if (MovieSceneSequence)
		{
			GameAction_Log(Verbose, "PlayInternal - GameActionSequence: %s", *MovieSceneSequence->GetTypedOuter<UGameActionSegmentBase>()->GetName());
		}
#endif

		//if (bReversePlayback)
		//{
		//	if (OnPlayReverse.IsBound())
		//	{
		//		OnPlayReverse.Broadcast();
		//	}
		//}
		//else
		//{
		//	if (OnPlay.IsBound())
		//	{
		//		OnPlay.Broadcast();
		//	}
		//}
	}
}

void UGameActionSequencePlayer::StopInternal(FFrameTime TimeToResetTo)
{
	TGuardValue<bool> SpawnRegisterIsPlayFinishedGuard(FGameActionPlayerContext::bIsPlayFinished, true);

	if (bIsEvaluating)
	{
		LatentActions.Emplace(FLatentAction::EType::Stop, TimeToResetTo);
		return;
	}

	if (IsPlaying() || IsPaused())
	{
		bIsEvaluating = true;
		Status = EMovieScenePlayerStatus::Stopped;

		// Put the cursor at the specified position
		PlayPosition.Reset(TimeToResetTo);
		if (TimeController.IsValid())
		{
			TimeController->StopPlaying(GetCurrentTime());
		}

		CurrentNumLoops = 0;
		LastTickGameTimeSeconds.Reset();

		RestorePreAnimatedState();

		if (RootTemplateInstance.IsValid())
		{
			RootTemplateInstance.Finish(*this);
		}

		if (OldMaxTickRate.IsSet())
		{
			GEngine->SetMaxFPS(OldMaxTickRate.GetValue());
			OldMaxTickRate.Reset();
		}

		//if (HasAuthority())
		//{
		//	// Explicitly handle Stop() events through an RPC call
		//	RPC_OnStopEvent(TimeToResetTo);
		//}
		UpdateNetworkSyncProperties();

		//OnStopped();

#if !UE_BUILD_SHIPPING
		if (RootTemplateInstance.IsValid())
		{
			UMovieSceneSequence* MovieSceneSequence = RootTemplateInstance.GetSequence(MovieSceneSequenceID::Root);
			if (MovieSceneSequence)
			{
				UE_LOG(GameAction_Log, Verbose, TEXT("Stop - GameActionSequence: %s"), *MovieSceneSequence->GetTypedOuter<UGameActionSegmentBase>()->GetName());
			}
		}
#endif

		//if (OnStop.IsBound())
		//{
		//	OnStop.Broadcast();
		//}
		bIsEvaluating = false;

		ApplyLatentActions();
	}
}

void UGameActionSequencePlayer::Scrub()
{
	// @todo Sequencer playback: Should we recreate the instance every time?
	// We must not recreate the instance since it holds stateful information (such as which objects it has spawned). Recreating the instance would break any 
	// @todo: Is this still the case now that eval state is stored (correctly) in the player?
	if (ensure(Sequence != nullptr))
	{
		if (!RootTemplateInstance.IsValid())
		{
			RootTemplateInstance.Initialize(*Sequence, *this, nullptr);
		}
	}

	Status = EMovieScenePlayerStatus::Scrubbing;
	TimeController->StopPlaying(GetCurrentTime());

	UpdateNetworkSyncProperties();
}

void UGameActionSequencePlayer::Pause()
{
	if (bIsEvaluating)
	{
		LatentActions.Emplace(FLatentAction::EType::Pause);
		return;
	}

	if (IsPlaying())
	{
		Status = EMovieScenePlayerStatus::Paused;
		TimeController->StopPlaying(GetCurrentTime());

		LastTickGameTimeSeconds.Reset();

		// Evaluate the sequence at its current time, with a status of 'stopped' to ensure that animated state pauses correctly. (ie. audio sounds should stop/pause)
		{
			bIsEvaluating = true;

			FMovieSceneEvaluationRange CurrentTimeRange = PlayPosition.GetCurrentPositionAsRange();
			const FMovieSceneContext Context(CurrentTimeRange, EMovieScenePlayerStatus::Stopped);
			RootTemplateInstance.Evaluate(Context, *this);

			bIsEvaluating = false;
		}

		ApplyLatentActions();
		UpdateNetworkSyncProperties();

#if !UE_BUILD_SHIPPING
		UMovieSceneSequence* MovieSceneSequence = RootTemplateInstance.GetSequence(MovieSceneSequenceID::Root);
		if (MovieSceneSequence)
		{
			UE_LOG(GameAction_Log, Verbose, TEXT("Pause - GameActionSequence: %s"), *MovieSceneSequence->GetTypedOuter<UGameActionSegmentBase>()->GetName());
		}
#endif

		//if (OnPause.IsBound())
		//{
		//	OnPause.Broadcast();
		//}
	}
}

void UGameActionSequencePlayer::JumpToFrame(FFrameTime NewPosition)
{
	UpdateTimeCursorPosition(NewPosition, EUpdatePositionMethod::Jump);

	TimeController->Reset(GetCurrentTime());

	if (HasAuthority())
	{
		//RPC_ExplicitServerUpdateEvent(EUpdatePositionMethod::Jump, NewPosition);
	}
}

void UGameActionSequencePlayer::PlayToFrame(FFrameTime NewPosition)
{
	UpdateTimeCursorPosition(NewPosition, EUpdatePositionMethod::Play);

	TimeController->Reset(GetCurrentTime());

	if (HasAuthority())
	{
		// RPC_ExplicitServerUpdateEvent(EUpdatePositionMethod::Play, NewPosition);
	}
}

void UGameActionSequencePlayer::ScrubToFrame(FFrameTime NewPosition)
{
	UpdateTimeCursorPosition(NewPosition, EUpdatePositionMethod::Scrub);

	TimeController->Reset(GetCurrentTime());

	if (HasAuthority())
	{
		// RPC_ExplicitServerUpdateEvent(EUpdatePositionMethod::Scrub, NewPosition);
	}
}

namespace GameActionPlayer
{
	EMovieScenePlayerStatus::Type UpdateMethodToStatus(EUpdatePositionMethod Method)
	{
		switch (Method)
		{
		case EUpdatePositionMethod::Scrub: return EMovieScenePlayerStatus::Scrubbing;
		case EUpdatePositionMethod::Jump:  return EMovieScenePlayerStatus::Stopped;
		case EUpdatePositionMethod::Play:  return EMovieScenePlayerStatus::Playing;
		default:                           return EMovieScenePlayerStatus::Stopped;
		}
	}

	FMovieSceneEvaluationRange UpdatePlayPosition(FMovieScenePlaybackPosition& InOutPlayPosition, FFrameTime NewTime, EUpdatePositionMethod Method)
	{
		if (Method == EUpdatePositionMethod::Play)
		{
			return InOutPlayPosition.PlayTo(NewTime);
		}

		return InOutPlayPosition.JumpTo(NewTime);
	}
}

void UGameActionSequencePlayer::UpdateTimeCursorPosition(FFrameTime NewPosition, EUpdatePositionMethod Method)
{
	if (bIsEvaluating)
	{
		LatentActions.Emplace(Method, NewPosition);
	}
	else
	{
		EMovieScenePlayerStatus::Type StatusOverride = GameActionPlayer::UpdateMethodToStatus(Method);

		const int32 Duration = DurationFrames;
		if (Duration == 0)
		{
			UE_LOG(GameAction_Log, Warning, TEXT("Attempting to play back a sequence with zero duration"));
			return;
		}

		if (bPendingOnStartedPlaying)
		{
			// OnStartedPlaying();
			bPendingOnStartedPlaying = false;
		}

		if (Method == EUpdatePositionMethod::Play && ShouldStopOrLoop(NewPosition))
		{
			// The actual start time taking into account reverse playback
			FFrameNumber StartTimeWithReversed = bReversePlayback ? GetLastValidTime().FrameNumber : StartTime;

			// The actual end time taking into account reverse playback
			FFrameTime EndTimeWithReversed = bReversePlayback ? StartTime : GetLastValidTime().FrameNumber;

			FFrameTime PositionRelativeToStart = NewPosition.FrameNumber - StartTimeWithReversed;

			const int32 NumTimesLooped = FMath::Abs(PositionRelativeToStart.FrameNumber.Value / Duration);

			// loop playback
			switch (PlayEndAction)
			{
			case EGameActionPlayerEndAction::Stop:
			{
				// Clamp the position to the duration
				NewPosition = FMath::Clamp(NewPosition, FFrameTime(StartTime), GetLastValidTime());

				FMovieSceneEvaluationRange Range = GameActionPlayer::UpdatePlayPosition(PlayPosition, NewPosition, Method);
				UpdateMovieSceneInstance(Range, StatusOverride);

				// 主端来决定行为的停止，模拟端使用行为片段中的中断
				if (IsLocalControlled())
				{
					StopInternal(NewPosition);
				}

				OnFinished.Broadcast();
			}
			break;
			case EGameActionPlayerEndAction::Loop:
			{
				CurrentNumLoops += NumTimesLooped;

				// Finish evaluating any frames left in the current loop in case they have events attached
				FFrameTime CurrentPosition = PlayPosition.GetCurrentPosition();
				if ((bReversePlayback && CurrentPosition > EndTimeWithReversed) ||
					(!bReversePlayback && CurrentPosition < EndTimeWithReversed))
				{
					FMovieSceneEvaluationRange Range = PlayPosition.PlayTo(EndTimeWithReversed);
					UpdateMovieSceneInstance(Range, StatusOverride);
				}


				const FFrameTime Overplay = FFrameTime(PositionRelativeToStart.FrameNumber.Value % Duration, PositionRelativeToStart.GetSubFrame());
				FFrameTime NewFrameOffset;

				if (bReversePlayback)
				{
					NewFrameOffset = (Overplay > 0) ? FFrameTime(Duration) + Overplay : Overplay;
				}
				else
				{
					NewFrameOffset = (Overplay < 0) ? FFrameTime(Duration) + Overplay : Overplay;
				}

				ForgetExternallyOwnedSpawnedObjects(State, *this);

				// Reset the play position, and generate a new range that gets us to the new frame time
				if (bReversePlayback)
				{
					PlayPosition.Reset(Overplay > 0 ? GetLastValidTime() : StartTimeWithReversed);
				}
				else
				{
					PlayPosition.Reset(Overplay < 0 ? GetLastValidTime() : StartTimeWithReversed);
				}

				FMovieSceneEvaluationRange Range = PlayPosition.PlayTo(StartTimeWithReversed + NewFrameOffset);

				const bool bHasJumped = true;
				UpdateMovieSceneInstance(Range, StatusOverride, bHasJumped);

				// Use the exact time here rather than a frame locked time to ensure we don't skip the amount that was overplayed in the time controller
				FQualifiedFrameTime ExactCurrentTime(StartTimeWithReversed + NewFrameOffset, PlayPosition.GetInputRate());
				TimeController->Reset(ExactCurrentTime);

				//OnLooped();
			}
			break;
			case EGameActionPlayerEndAction::Pause:
			{
				Pause();		
			}
			break;
			default:
				checkNoEntry();
			}
		}
		else
		{
			// Just update the time and sequence
			FMovieSceneEvaluationRange Range = GameActionPlayer::UpdatePlayPosition(PlayPosition, NewPosition, Method);
			UpdateMovieSceneInstance(Range, StatusOverride);
		}

		UpdateNetworkSyncProperties();
	}
}

void UGameActionSequencePlayer::UpdateMovieSceneInstance(FMovieSceneEvaluationRange InRange, EMovieScenePlayerStatus::Type PlayerStatus, bool bHasJumped)
{
#if !NO_LOGGING
	UMovieSceneSequence* MovieSceneSequence = RootTemplateInstance.GetSequence(MovieSceneSequenceID::Root);
	if (MovieSceneSequence)
	{
		const FQualifiedFrameTime CurrentTime = GetCurrentTime();
		GameAction_Log(VeryVerbose, "Evaluating sequence %s at frame %d, subframe %f (%f fps).", *MovieSceneSequence->GetName(), CurrentTime.Time.FrameNumber.Value, CurrentTime.Time.GetSubFrame(), CurrentTime.Rate.AsDecimal());
	}
#endif

	bIsEvaluating = true;

	FMovieSceneContext Context(InRange, PlayerStatus);
	Context.SetHasJumped(bHasJumped);

	RootTemplateInstance.Evaluate(Context, *this);

	bIsEvaluating = false;

	ApplyLatentActions();
}

void UGameActionSequencePlayer::UpdateNetworkSyncProperties()
{
	if (HasAuthority())
	{
		NetSyncProps.LastKnownPosition = PlayPosition.GetCurrentPosition();
		NetSyncProps.LastKnownStatus = Status;
		NetSyncProps.LastKnownNumLoops = CurrentNumLoops;
	}
}

void UGameActionSequencePlayer::ApplyLatentActions()
{
	// Swap to a stack array to ensure no reentrancy if we evaluate during a pause, for instance
	TArray<FLatentAction> TheseActions;
	Swap(TheseActions, LatentActions);

	for (const FLatentAction& LatentAction : TheseActions)
	{
		switch (LatentAction.Type)
		{
		case FLatentAction::EType::Stop:   StopInternal(LatentAction.Position); continue;
		case FLatentAction::EType::Pause:  Pause();                             continue;
		case FLatentAction::EType::Play:   PlayInternal();                      continue;
		}

		check(LatentAction.Type == FLatentAction::EType::Update);
		switch (LatentAction.UpdateMethod)
		{
		case EUpdatePositionMethod::Play:  PlayToFrame(LatentAction.Position); continue;
		case EUpdatePositionMethod::Jump:  JumpToFrame(LatentAction.Position); continue;
		case EUpdatePositionMethod::Scrub: ScrubToFrame(LatentAction.Position); continue;
		}
	}
}

void UGameActionSequencePlayer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UGameActionSequencePlayer, NetSyncProps, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(UGameActionSequencePlayer, StartTime, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(UGameActionSequencePlayer, DurationFrames, COND_SimulatedOnly);
}

void UGameActionSequencePlayer::PostNetReceive()
{
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Handle a passive update of the replicated status and time properties of the player.

	Super::PostNetReceive();

	if (!ensure(!HasAuthority()) || !Sequence)
	{
		// Never run network sync operations on authoritative players or players that have not been initialized yet
		return;
	}

	float PingMs = 0.f;

	constexpr int32 SequencerNetSyncThresholdMS = 200;
	
	UWorld* PlayWorld = GetWorld();
	if (PlayWorld)
	{
		UNetDriver* NetDriver = PlayWorld->GetNetDriver();
		if (NetDriver && NetDriver->ServerConnection && NetDriver->ServerConnection->PlayerController && NetDriver->ServerConnection->PlayerController->PlayerState)
		{
			PingMs = NetDriver->ServerConnection->PlayerController->PlayerState->ExactPing * (bReversePlayback ? -1.f : 1.f);
		}
	}

	const bool bHasStartedPlaying = NetSyncProps.LastKnownStatus == EMovieScenePlayerStatus::Playing && Status != EMovieScenePlayerStatus::Playing;
	const bool bHasChangedStatus = NetSyncProps.LastKnownStatus != Status;
	const bool bHasChangedTime = NetSyncProps.LastKnownPosition != PlayPosition.GetCurrentPosition();

	const FFrameTime PingLag = (PingMs / 1000.f) * PlayPosition.GetInputRate();
	//const FFrameTime LagThreshold = 0.2f * PlayPosition.GetInputRate();
	//const FFrameTime LagDisparity = FMath::Abs(PlayPosition.GetCurrentPosition() - NetSyncProps.LastKnownPosition);
	const FFrameTime LagThreshold = (SequencerNetSyncThresholdMS * 0.001f) * PlayPosition.GetInputRate();

	if (!bHasChangedStatus && !bHasChangedTime)
	{
		// Nothing to do
		return;
	}

#if !NO_LOGGING
	{
		const FFrameTime CurrentTime = PlayPosition.GetCurrentPosition();
		FString    SequenceName = RootTemplateInstance.GetSequence(MovieSceneSequenceID::Root)->GetName();

		AActor* Actor = GetTypedOuter<AActor>();
		if (Actor->GetWorld()->GetNetMode() == NM_Client)
		{
			SequenceName += FString::Printf(TEXT(" (client %d)"), GPlayInEditorID - 1);
		}

		GameAction_Log(VeryVerbose, "Network sync for sequence %s %s @ frame %d, subframe %f. Server is %s @ frame %d, subframe %f.",
			*SequenceName, *UEnum::GetValueAsString(TEXT("MovieScene.EMovieScenePlayerStatus"), Status.GetValue()), CurrentTime.FrameNumber.Value, CurrentTime.GetSubFrame(),
			*UEnum::GetValueAsString(TEXT("MovieScene.EMovieScenePlayerStatus"), NetSyncProps.LastKnownStatus.GetValue()), NetSyncProps.LastKnownPosition.FrameNumber.Value, NetSyncProps.LastKnownPosition.GetSubFrame());
	}
#endif

	// Deal with changes of state from stopped <-> playing separately, as they require slightly different considerations
	if (bHasStartedPlaying)
	{
		// Note: when starting playback, we assume that the client and server were at the same time prior to the server initiating playback

		// Initiate playback from our current position
		PlayInternal();

		const FFrameTime LagDisparity = FMath::Abs(PlayPosition.GetCurrentPosition() - (NetSyncProps.LastKnownPosition + PingLag));
		if (LagDisparity > LagThreshold)
		{
			// Synchronize to the server time as best we can if there is a large disparity
			PlayToFrame(NetSyncProps.LastKnownPosition + PingLag);
		}
	}
	else
	{
		if (bHasChangedTime)
		{
			// Make sure the client time matches the server according to the client's current status
			if (Status == EMovieScenePlayerStatus::Playing)
			{
				// When the server has looped back to the start but a client is near the end (and is thus about to loop), we don't want to forcibly synchronize the time unless
				// the *real* difference in time is above the threshold. We compute the real-time difference by adding SequenceDuration*LoopCountDifference to the server position:
				//		start	srv_time																																clt_time		end
				//		0		1		2		3		4		5		6		7		8		9		10		11		12		13		14		15		16		17		18		19		20
				//		|		|																																		|				|
				//
				//		Let NetSyncProps.LastKnownNumLoops = 1, CurrentNumLoops = 0, bReversePlayback = false
				//			=> LoopOffset = 1
				//			   OffsetServerTime = srv_time + FrameDuration*LoopOffset = 1 + 20*1 = 21
				//			   Difference = 21 - 18 = 3 frames
				const int32        LoopOffset = (NetSyncProps.LastKnownNumLoops - CurrentNumLoops) * (bReversePlayback ? -1 : 1);
				const FFrameTime   OffsetServerTime = (NetSyncProps.LastKnownPosition + PingLag) + DurationFrames * LoopOffset;
				const FFrameTime   Difference = FMath::Abs(PlayPosition.GetCurrentPosition() - OffsetServerTime);

				if (bHasChangedStatus)
				{
					// If the status has changed forcibly play to the server position before setting the new status
					PlayToFrame(NetSyncProps.LastKnownPosition + PingLag);
				}
				else if (Difference > LagThreshold + PingLag)
				{
#if !NO_LOGGING
					{
						const FFrameTime CurrentTime = PlayPosition.GetCurrentPosition();
						FString    SequenceName = RootTemplateInstance.GetSequence(MovieSceneSequenceID::Root)->GetName();

						AActor* Actor = GetTypedOuter<AActor>();
						if (Actor->GetWorld()->GetNetMode() == NM_Client)
						{
							SequenceName += FString::Printf(TEXT(" (client %d)"), GPlayInEditorID - 1);
						}

						GameAction_Log(Log, "Correcting de-synced play position for sequence %s %s @ frame %d, subframe %f. Server is %s @ frame %d, subframe %f. Client ping is %.2fms.",
							*SequenceName, *UEnum::GetValueAsString(TEXT("MovieScene.EMovieScenePlayerStatus"), Status.GetValue()), CurrentTime.FrameNumber.Value, CurrentTime.GetSubFrame(),
							*UEnum::GetValueAsString(TEXT("MovieScene.EMovieScenePlayerStatus"), NetSyncProps.LastKnownStatus.GetValue()), NetSyncProps.LastKnownPosition.FrameNumber.Value, NetSyncProps.LastKnownPosition.GetSubFrame(), PingMs);
					}
#endif
					// We're drastically out of sync with the server so we need to forcibly set the time.
					// Play to the time only if it is further on in the sequence (in our play direction)
					const bool bPlayToFrame = bReversePlayback ? NetSyncProps.LastKnownPosition < PlayPosition.GetCurrentPosition() : NetSyncProps.LastKnownPosition > PlayPosition.GetCurrentPosition();
					if (bPlayToFrame)
					{
						PlayToFrame(NetSyncProps.LastKnownPosition + PingLag);
					}
					else
					{
						JumpToFrame(NetSyncProps.LastKnownPosition + PingLag);
					}
				}
			}
			else if (Status == EMovieScenePlayerStatus::Scrubbing)
			{
				ScrubToFrame(NetSyncProps.LastKnownPosition);
			}
		}

		if (bHasChangedStatus)
		{
			switch (NetSyncProps.LastKnownStatus)
			{
			case EMovieScenePlayerStatus::Paused:    Pause(); break;
			case EMovieScenePlayerStatus::Playing:   PlayInternal();  break;
			case EMovieScenePlayerStatus::Scrubbing: Scrub(); break;
			}
		}
	}
}

FFrameTime UGameActionSequencePlayer::GetLastValidTime() const
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (MovieScene)
	{
		TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
		const FFrameNumber SrcEndFrame = UE::MovieScene::DiscreteExclusiveUpper(PlaybackRange) - 1;
		return ConvertFrameTime(SrcEndFrame, MovieScene->GetTickResolution(), MovieScene->GetDisplayRate());
	}

	return FFrameTime(StartTime);
}

bool UGameActionSequencePlayer::ShouldStopOrLoop(FFrameTime NewPosition) const
{
	bool bShouldStopOrLoop = false;
	if (IsPlaying())
	{
		if (!bReversePlayback)
		{
			bShouldStopOrLoop = NewPosition >= GetLastValidTime();
		}
		else
		{
			bShouldStopOrLoop = NewPosition.FrameNumber < StartTime;
		}
	}

	return bShouldStopOrLoop;
}

bool UGameActionSequencePlayer::HasAuthority() const
{
	AActor* Actor = GetTypedOuter<AActor>();
	check(Actor);
	return Actor->HasAuthority();
}

bool UGameActionSequencePlayer::IsLocalControlled() const
{
	AActor* Actor = GetTypedOuter<AActor>();
	check(Actor);
	const ENetMode NetMode = Actor->GetNetMode();

	if (NetMode == NM_Standalone)
	{
		// Not networked.
		return true;
	}

	if (NetMode == NM_Client && Actor->GetLocalRole() == ROLE_AutonomousProxy)
	{
		// Networked client in control.
		return true;
	}

	if (Actor->GetRemoteRole() != ROLE_AutonomousProxy && Actor->GetLocalRole() == ROLE_Authority)
	{
		// Local authority in control.
		return true;
	}

	return false;
}

UObject* UGameActionSequencePlayer::GetPlaybackContext() const
{
	return GameAction;
}

IMovieScenePlaybackClient* UGameActionSequencePlayer::GetPlaybackClient()
{
	return GameAction;
}

bool UGameActionSequencePlayer::IsLoopIndefinitely() const
{
	return PlayEndAction == EGameActionPlayerEndAction::Loop;
}

void UGameActionSequencePlayer::Update(const float DeltaSeconds)
{
	const float DoubleSubStepDuration = SubStepDuration * 2.f;
	bIsInSubStepState = DeltaSeconds > DoubleSubStepDuration;
	
	if (IsPlaying() && TimeController)
	{
		const float CurPlayRate = bReversePlayback ? -PlayRate : PlayRate;

		if (DeltaSeconds > DoubleSubStepDuration)
		{
			GameAction_Log(Display, "当前帧间隔 [%f] 大于子帧间隔 [%f] 两倍，进行子步解算", DeltaSeconds, SubStepDuration);

			const float SubStepLength = SubStepDuration;
			for (float SubStepProgress = 0.f; SubStepProgress < DeltaSeconds - SubStepLength; SubStepProgress += SubStepLength)
			{
				const bool IsLastSubStep = SubStepProgress + DoubleSubStepDuration > DeltaSeconds;
				const float SubDeltaSeconds = IsLastSubStep ? DeltaSeconds - SubStepProgress : SubStepLength;

				TimeController->Tick(SubDeltaSeconds, CurPlayRate);
				const FFrameTime NewTime = TimeController->RequestCurrentTime(GetCurrentTime(), CurPlayRate);
				UpdateTimeCursorPosition(NewTime, EUpdatePositionMethod::Play);
			}
		}
		else
		{
			TimeController->Tick(DeltaSeconds, CurPlayRate);
			const FFrameTime NewTime = TimeController->RequestCurrentTime(GetCurrentTime(), CurPlayRate);
			UpdateTimeCursorPosition(NewTime, EUpdatePositionMethod::Play);
		}
	}
}

namespace ULevelSequencePlayerHack
{
	TTuple<EViewTargetBlendFunction, float> BuiltInEasingTypeToBlendFunction(EMovieSceneBuiltInEasing EasingType)
	{
		using Return = TTuple<EViewTargetBlendFunction, float>;
		switch (EasingType)
		{
		case EMovieSceneBuiltInEasing::Linear:
			return Return(EViewTargetBlendFunction::VTBlend_Linear, 1.f);

		case EMovieSceneBuiltInEasing::QuadIn:
			return Return(EViewTargetBlendFunction::VTBlend_EaseIn, 2);
		case EMovieSceneBuiltInEasing::QuadOut:
			return Return(EViewTargetBlendFunction::VTBlend_EaseOut, 2);
		case EMovieSceneBuiltInEasing::QuadInOut:
			return Return(EViewTargetBlendFunction::VTBlend_EaseInOut, 2);

		case EMovieSceneBuiltInEasing::CubicIn:
			return Return(EViewTargetBlendFunction::VTBlend_EaseIn, 3);
		case EMovieSceneBuiltInEasing::CubicOut:
			return Return(EViewTargetBlendFunction::VTBlend_EaseOut, 3);
		case EMovieSceneBuiltInEasing::CubicInOut:
			return Return(EViewTargetBlendFunction::VTBlend_EaseInOut, 3);

		case EMovieSceneBuiltInEasing::QuartIn:
			return Return(EViewTargetBlendFunction::VTBlend_EaseIn, 4);
		case EMovieSceneBuiltInEasing::QuartOut:
			return Return(EViewTargetBlendFunction::VTBlend_EaseOut, 4);
		case EMovieSceneBuiltInEasing::QuartInOut:
			return Return(EViewTargetBlendFunction::VTBlend_EaseInOut, 4);

		case EMovieSceneBuiltInEasing::QuintIn:
			return Return(EViewTargetBlendFunction::VTBlend_EaseIn, 5);
		case EMovieSceneBuiltInEasing::QuintOut:
			return Return(EViewTargetBlendFunction::VTBlend_EaseOut, 5);
		case EMovieSceneBuiltInEasing::QuintInOut:
			return Return(EViewTargetBlendFunction::VTBlend_EaseInOut, 5);

			// UNSUPPORTED
		case EMovieSceneBuiltInEasing::SinIn:
		case EMovieSceneBuiltInEasing::SinOut:
		case EMovieSceneBuiltInEasing::SinInOut:
		case EMovieSceneBuiltInEasing::CircIn:
		case EMovieSceneBuiltInEasing::CircOut:
		case EMovieSceneBuiltInEasing::CircInOut:
		case EMovieSceneBuiltInEasing::ExpoIn:
		case EMovieSceneBuiltInEasing::ExpoOut:
		case EMovieSceneBuiltInEasing::ExpoInOut:
			break;
		}
		return Return(EViewTargetBlendFunction::VTBlend_Linear, 1.f);
	}
}

void UGameActionSequencePlayer::UpdateCameraCut(UObject* CameraObject, const EMovieSceneCameraCutParams& CameraCutParams)
{
	ACharacter* Owner = CastChecked<UGameActionInstanceBase>(GetOuter())->GetOwner();
	APlayerController* PC = Cast<APlayerController>(Owner->GetController());

	if (PC == nullptr || PC->PlayerCameraManager == nullptr)
	{
		return;
	}

	AActor* ViewTarget = PC->GetViewTarget();
	const bool bIsEntryCameraCutState = bInCameraCutState == false;
	const bool bIsLeaveCameraCutState = CameraObject == nullptr;
	bInCameraCutState = CameraObject != nullptr;
	
	// skip unlocking if the current view target differs
	// if unlockIfCameraActor is valid, release lock if currently locked to object
	AActor* UnlockIfCameraActor = Cast<AActor>(CameraCutParams.UnlockIfCameraObject);
	if (CameraObject == nullptr && UnlockIfCameraActor != nullptr && UnlockIfCameraActor != ViewTarget)
	{
		return;
	}

	AActor* CameraActor = CameraObject ? CastChecked<AActor>(CameraObject) : Owner;
	UCameraComponent* CameraComponent = MovieSceneHelpers::CameraComponentFromRuntimeObject(CameraActor);
	if (CameraActor == ViewTarget)
	{
		if (CameraCutParams.bJumpCut)
		{
			PC->PlayerCameraManager->SetGameCameraCutThisFrame();

			if (CameraComponent)
			{
				CameraComponent->NotifyCameraCut();
			}
		}
		return;
	}

	if (bIsEntryCameraCutState || bIsLeaveCameraCutState)
	{
		FViewTargetTransitionParams TransitionParams;
		TransitionParams.BlendTime = 0.25f;
		TransitionParams.BlendFunction = EViewTargetBlendFunction::VTBlend_EaseOut;
		TransitionParams.BlendExp = 3.f;
		const AActor* PendingViewTarget = PC->PlayerCameraManager->PendingViewTarget.Target;
		if (PendingViewTarget != CameraActor)
		{
			PC->SetViewTarget(CameraActor, TransitionParams);
		}
	}
	else
	{
		bool bDoSetViewTarget = true;
		FViewTargetTransitionParams TransitionParams;
		if (CameraCutParams.BlendType.IsSet())
		{
			// Convert known easing functions to their corresponding view target blend parameters.
			using namespace ULevelSequencePlayerHack;
			TTuple<EViewTargetBlendFunction, float> BlendFunctionAndExp = BuiltInEasingTypeToBlendFunction(CameraCutParams.BlendType.GetValue());
			TransitionParams.BlendTime = CameraCutParams.BlendTime;
			TransitionParams.BlendFunction = BlendFunctionAndExp.Get<0>();
			TransitionParams.BlendExp = BlendFunctionAndExp.Get<1>();

			// Calling SetViewTarget on a camera that we are currently transitioning to will 
			// result in that transition being aborted, and the view target being set immediately.
			// We want to avoid that, so let's leave the transition running if it's the case.
			const AActor* CurViewTarget = PC->PlayerCameraManager->ViewTarget.Target;
			const AActor* PendingViewTarget = PC->PlayerCameraManager->PendingViewTarget.Target;
			if (PendingViewTarget == CameraActor)
			{
				bDoSetViewTarget = false;
			}
		}
		if (bDoSetViewTarget)
		{
			PC->SetViewTarget(CameraActor, TransitionParams);
		}
	}

	if (CameraComponent)
	{
		CameraComponent->NotifyCameraCut();
	}

	PC->PlayerCameraManager->bClientSimulatingViewTarget = (CameraActor != nullptr);
	PC->PlayerCameraManager->SetGameCameraCutThisFrame();
}
