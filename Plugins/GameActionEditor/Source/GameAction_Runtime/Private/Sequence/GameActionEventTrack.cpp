// Fill out your copyright notice in the Description page of Project Settings.


#include "Sequence/GameActionEventTrack.h"
#include <Channels/MovieSceneChannelProxy.h>
#include <IMovieSceneTracksModule.h>
#include <Tracks/MovieSceneSpawnTrack.h>
#include <IMovieScenePlayer.h>
#include <MovieSceneExecutionToken.h>
#include <Evaluation/MovieSceneEvaluationTrack.h>

#include "GameAction/GameActionEvent.h"
#include "Sequence/GameActionSequencePlayer.h"

#define LOCTEXT_NAMESPACE "GameActionEventTrack"

UGameActionKeyEventTrack::UGameActionKeyEventTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	TrackTint = FLinearColor(0.2f, 0.2f, 0.05f).ToFColor(true);
#endif
}

#if WITH_EDITORONLY_DATA
FText UGameActionKeyEventTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("行为关键帧事件轨", "行为关键帧事件轨");
}
#endif

bool UGameActionKeyEventTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UGameActionKeyEventSection::StaticClass();
}

UMovieSceneSection* UGameActionKeyEventTrack::CreateNewSection()
{
	return NewObject<UGameActionKeyEventSection>(this, NAME_None, RF_Transactional);
}

FMovieSceneEvalTemplatePtr UGameActionKeyEventTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FGameActionKeyEventSectionTemplate(CastChecked<UGameActionKeyEventSection>(&InSection));
}

void UGameActionKeyEventTrack::PostCompile(FMovieSceneEvaluationTrack& Track, const FMovieSceneTrackCompilerArgs& Args) const
{
	Track.SetEvaluationGroup(IMovieSceneTracksModule::GetEvaluationGroupName(EBuiltInEvaluationGroup::SpawnObjects));
	Track.SetEvaluationPriority(UMovieSceneSpawnTrack::GetEvaluationPriority() - 100);
	Track.SetEvaluationMethod(EEvaluationMethod::Swept);
}

UGameActionKeyEventSection::UGameActionKeyEventSection()
{
	bSupportsInfiniteRange = true;
	SectionRange.Value = TRange<FFrameNumber>::All();
#if WITH_EDITOR
	FMovieSceneChannelMetaData MovieSceneChannelMetaData;
	MovieSceneChannelMetaData.Color = FColor::Blue;
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(KeyEventChannel, MovieSceneChannelMetaData);
#else
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(KeyEventChannel);
#endif
}

void FGameActionKeyEventSectionTemplate::EvaluateSwept(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const TRange<FFrameNumber>& SweptRange, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	if (Context.GetStatus() == EMovieScenePlayerStatus::Stopped || Context.IsSilent())
	{
		return;
	}

	const bool bBackwards = Context.GetDirection() == EPlayDirection::Backwards;

	const FGameActionKeyEventChannel& KeyEventChannel = Section->KeyEventChannel;
	TArrayView<const FFrameNumber> EventTimes = KeyEventChannel.GetKeyTimes();
	TArrayView<const FGameActionKeyEventValue> Events = KeyEventChannel.GetKeyValues();

	TArray<FGameActionKeyEventValue> EventsToTrigger;
	if (bBackwards)
	{
		// Trigger events backwards
		for (int32 KeyIndex = EventTimes.Num() - 1; KeyIndex >= 0; --KeyIndex)
		{
			FFrameNumber Time = EventTimes[KeyIndex];
			if (Events[KeyIndex].KeyEvent && SweptRange.Contains(Time))
			{
				EventsToTrigger.Add(Events[KeyIndex]);
			}
		}
	}
	// Trigger events forwards
	else for (int32 KeyIndex = 0; KeyIndex < EventTimes.Num(); ++KeyIndex)
	{
		FFrameNumber Time = EventTimes[KeyIndex];
		if (Events[KeyIndex].KeyEvent && SweptRange.Contains(Time))
		{
			EventsToTrigger.Add(Events[KeyIndex]);
		}
	}

	if (EventsToTrigger.Num())
	{
		struct FGameActionKeyEventExecutionToken : IMovieSceneExecutionToken
		{
			FGameActionKeyEventExecutionToken(TArray<FGameActionKeyEventValue>&& InEvents)
				: Events(MoveTemp(InEvents))
			{}

			void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
			{
				MOVIESCENE_DETAILED_SCOPE_CYCLE_COUNTER(GameActionEval_KeyEventTrack_TokenExecute);

				if (Operand.ObjectBindingID.IsValid())
				{
					for (const TWeakObjectPtr<>& Object : Player.FindBoundObjects(Operand))
					{
						UObject* Obj = Object.Get();
						if (Obj == nullptr)
						{
							continue;
						}

						for (const FGameActionKeyEventValue& KeyEventValue : Events)
						{
							if (ensure(KeyEventValue.KeyEvent))
							{
								KeyEventValue.KeyEvent->ExecuteEvent(Obj, Player);
							}
						}
					}
				}
			}

			TArray<FGameActionKeyEventValue> Events;
		};
		ExecutionTokens.Add(FGameActionKeyEventExecutionToken(MoveTemp(EventsToTrigger)));
	}
}

UGameActionStateEventTrack::UGameActionStateEventTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	TrackTint = FLinearColor(0.2f, 0.2f, 0.05f).ToFColor(true);
#endif
}

#if WITH_EDITORONLY_DATA
FText UGameActionStateEventTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("行为状态事件轨", "行为状态事件轨");
}
#endif

bool UGameActionStateEventTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UGameActionStateEventSection::StaticClass();
}

UMovieSceneSection* UGameActionStateEventTrack::CreateNewSection()
{
	return NewObject<UGameActionStateEventSection>(this, NAME_None, RF_Transactional);
}

FMovieSceneEvalTemplatePtr UGameActionStateEventTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FGameActionStateEventSectionTemplate(CastChecked<UGameActionStateEventSection>(&InSection));
}

void UGameActionStateEventTrack::PostCompile(FMovieSceneEvaluationTrack& Track, const FMovieSceneTrackCompilerArgs& Args) const
{
	Track.SetEvaluationGroup(IMovieSceneTracksModule::GetEvaluationGroupName(EBuiltInEvaluationGroup::SpawnObjects));
	Track.SetEvaluationPriority(UMovieSceneSpawnTrack::GetEvaluationPriority() - 100);
	Track.SetEvaluationMethod(EEvaluationMethod::Swept);
}

UGameActionStateEventSection::UGameActionStateEventSection()
{
#if WITH_EDITOR
	FMovieSceneChannelMetaData MovieSceneChannelMetaData;
	MovieSceneChannelMetaData.Color = FColor::Blue;
	MovieSceneChannelMetaData.bCanCollapseToTrack = true;
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(InnerKeyChannel, MovieSceneChannelMetaData);
#else
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(InnerKeyChannel);
#endif
}

struct FGameActionStateEvaluationData : public IPersistentEvaluationData
{
	FGameActionStateEvaluationData()
		: bIsActived(false)
	{}
	FMovieSceneEvaluationOperand OwnerOperand;
	uint8 bIsActived : 1;
	UGameActionStateEvent* Instance = nullptr;
};

void FGameActionStateEventSectionTemplate::Initialize(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const
{
	FGameActionStateEvaluationData& EvaluationData = PersistentData.GetOrAddSectionData<FGameActionStateEvaluationData>();
	if (EvaluationData.bIsActived == false)
	{
		EvaluationData.bIsActived = true;
		EvaluationData.OwnerOperand = Operand;
		if (Section->StateEvent && Operand.ObjectBindingID.IsValid())
		{
			UGameActionStateEvent* StateEvent = Section->StateEvent;
			if (Section->StateEvent->bInstanced)
			{
				StateEvent = NewObject<UGameActionStateEvent>(Player.GetPlaybackContext(), Section->StateEvent->GetClass(), NAME_None, RF_StrongRefOnFrame, Section->StateEvent);
				EvaluationData.Instance = StateEvent;
			}
			
			for (const TWeakObjectPtr<>& Object : Player.FindBoundObjects(Operand))
			{
				UObject* Obj = Object.Get();
				if (Obj == nullptr)
				{
					continue;
				}
				StateEvent->StartEvent(Obj, Player);
			}
		}
	}
}

void FGameActionStateEventSectionTemplate::EvaluateSwept(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const TRange<FFrameNumber>& SweptRange, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	using FEventArray = TArray<FGameActionStateEventInnerKeyValue, TInlineAllocator<2>>;
	struct FGameActionStateEventExecutionToken : IMovieSceneExecutionToken
	{
		FGameActionStateEventExecutionToken(UGameActionStateEvent* Event, const FEventArray& EventsToTrigger)
			: Event(Event), EventsToTrigger(EventsToTrigger)
		{}

		void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
		{
			MOVIESCENE_DETAILED_SCOPE_CYCLE_COUNTER(GameActionEval_StateEventTrack_TokenExecute);

			if (Operand.ObjectBindingID.IsValid())
			{
				FGameActionStateEvaluationData* EvaluationData = PersistentData.FindSectionData<FGameActionStateEvaluationData>();
				if (ensure(EvaluationData))
				{
					UGameActionStateEvent* StateEvent = EvaluationData->Instance ? EvaluationData->Instance : Event;

					const float DeltaSeconds = Context.GetDelta() / Context.GetFrameRate();
					for (const TWeakObjectPtr<>& Object : Player.FindBoundObjects(Operand))
					{
						UObject* Obj = Object.Get();
						if (Obj == nullptr)
						{
							continue;
						}

						for (const FGameActionStateEventInnerKeyValue& KeyEventValue : EventsToTrigger)
						{
							if (ensure(KeyEventValue.KeyEvent))
							{
								KeyEventValue.KeyEvent->ExecuteEvent(Obj, StateEvent, Player);
							}
						}
						
						StateEvent->TickEvent(Obj, Player, DeltaSeconds);
					}
				}
			}
		}

		UGameActionStateEvent* Event;
		FEventArray EventsToTrigger;
	};

	if (Context.GetStatus() == EMovieScenePlayerStatus::Stopped || Context.IsSilent())
	{
		return;
	}

	const bool bBackwards = Context.GetDirection() == EPlayDirection::Backwards;

	const FGameActionStateEventInnerKeyChannel& KeyEventChannel = Section->InnerKeyChannel;
	TArrayView<const FFrameNumber> EventTimes = KeyEventChannel.GetKeyTimes();
	TArrayView<const FGameActionStateEventInnerKeyValue> Events = KeyEventChannel.GetKeyValues();

	FEventArray EventsToTrigger;
	if (bBackwards)
	{
		// Trigger events backwards
		for (int32 KeyIndex = EventTimes.Num() - 1; KeyIndex >= 0; --KeyIndex)
		{
			FFrameNumber Time = EventTimes[KeyIndex];
			if (Events[KeyIndex].KeyEvent && SweptRange.Contains(Time))
			{
				EventsToTrigger.Add(Events[KeyIndex]);
			}
		}
	}
	// Trigger events forwards
	else for (int32 KeyIndex = 0; KeyIndex < EventTimes.Num(); ++KeyIndex)
	{
		FFrameNumber Time = EventTimes[KeyIndex];
		if (Events[KeyIndex].KeyEvent && SweptRange.Contains(Time))
		{
			EventsToTrigger.Add(Events[KeyIndex]);
		}
	}

	if (Section->StateEvent)
	{
		ExecutionTokens.Add(FGameActionStateEventExecutionToken(Section->StateEvent, EventsToTrigger));
	}
}

void FGameActionStateEventSectionTemplate::TearDown(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const
{
	FGameActionStateEvaluationData& EvaluationData = PersistentData.GetOrAddSectionData<FGameActionStateEvaluationData>();
	if (ensureAlways(EvaluationData.bIsActived == true))
	{
		EvaluationData.bIsActived = false;
		if (Section->StateEvent && EvaluationData.OwnerOperand.ObjectBindingID.IsValid())
		{
			UGameActionStateEvent* StateEvent = EvaluationData.Instance ? EvaluationData.Instance : Section->StateEvent;
			for (const TWeakObjectPtr<>& Object : Player.FindBoundObjects(EvaluationData.OwnerOperand))
			{
				UObject* Obj = Object.Get();
				if (Obj == nullptr)
				{
					continue;
				}
				StateEvent->EndEvent(Obj, Player, FGameActionPlayerContext::bIsPlayAborted == false);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
