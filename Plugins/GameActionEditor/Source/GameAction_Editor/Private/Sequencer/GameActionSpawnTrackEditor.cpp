// Fill out your copyright notice in the Description page of Project Settings.


#include "Sequencer/GameActionSpawnTrackEditor.h"
#include <Framework/MultiBox/MultiBoxBuilder.h>
#include <ISequencerSection.h>
#include <SequencerSectionPainter.h>
#include <CommonMovieSceneTools.h>
#include <Channels/MovieSceneChannelProxy.h>
#include <Channels/MovieSceneBoolChannel.h>
#include <EditorStyleSet.h>
#include <Rendering/DrawElements.h>

#include "Sequence/GameActionDynamicSpawnTrack.h"
#include "Sequence/GameActionSequence.h"

#define LOCTEXT_NAMESPACE "GameActionSpawnTrackEditor"

FGameActionSpawnTrackEditor::FGameActionSpawnTrackEditor(TSharedRef<ISequencer> InSequencer)
	: Super(InSequencer)
{

}

bool FGameActionSpawnTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UGameActionDynamicSpawnTrack::StaticClass();
}

bool FGameActionSpawnTrackEditor::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	return InSequence->IsA<UGameActionSequence>();
}

TSharedRef<ISequencerSection> FGameActionSpawnTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	// FBoolPropertySection
	class FGameActionSpawnSection : public FSequencerSection
	{
	public:
		FGameActionSpawnSection(UMovieSceneSection& InSectionObject)
			: FSequencerSection(InSectionObject)
		{}

		int32 OnPaintSection(FSequencerSectionPainter& Painter) const override
		{
			// custom drawing for bool curves
			UGameActionDynamicSpawnSectionBase* DynamicSpawnSection = Cast<UGameActionDynamicSpawnSectionBase>(WeakSection.Get());

			TArray<FFrameTime> SectionSwitchTimes;

			const FTimeToPixel& TimeConverter = Painter.GetTimeConverter();

			// Add the start time
			const FFrameNumber StartTime = TimeConverter.PixelToFrame(0.f).FloorToFrame();
			const FFrameNumber EndTime = TimeConverter.PixelToFrame(Painter.SectionGeometry.GetLocalSize().X).CeilToFrame();

			SectionSwitchTimes.Add(StartTime);

			int32 LayerId = Painter.PaintSectionBackground();

			FMovieSceneBoolChannel* BoolChannel = DynamicSpawnSection->GetChannelProxy().GetChannel<FMovieSceneBoolChannel>(0);
			if (!BoolChannel)
			{
				return LayerId;
			}

			for (FFrameNumber Time : BoolChannel->GetTimes())
			{
				if (Time > StartTime&& Time < EndTime)
				{
					SectionSwitchTimes.Add(Time);
				}
			}

			SectionSwitchTimes.Add(EndTime);

			const ESlateDrawEffect DrawEffects = Painter.bParentEnabled
				? ESlateDrawEffect::None
				: ESlateDrawEffect::DisabledEffect;

			static const int32 Height = 5;
			const float VerticalOffset = Painter.SectionGeometry.GetLocalSize().Y * .5f - Height * .5f;

			const FSlateBrush* BoolOverlayBrush = FEditorStyle::GetBrush("Sequencer.Section.StripeOverlay");

			for (int32 i = 0; i < SectionSwitchTimes.Num() - 1; ++i)
			{
				FFrameTime ThisTime = SectionSwitchTimes[i];

				bool ValueAtTime = false;
				BoolChannel->Evaluate(ThisTime, ValueAtTime);

				const FColor Color = ValueAtTime ? FColor(0, 255, 0, 125) : FColor(255, 0, 0, 125);

				FVector2D StartPos(TimeConverter.FrameToPixel(ThisTime), VerticalOffset);
				FVector2D Size(TimeConverter.FrameToPixel(SectionSwitchTimes[i + 1]) - StartPos.X, Height);

				FSlateDrawElement::MakeBox(
					Painter.DrawElements,
					LayerId + 1,
					Painter.SectionGeometry.ToPaintGeometry(StartPos, Size),
					BoolOverlayBrush,
					DrawEffects,
					Color
				);
			}

			return LayerId + 1;
		}
	};
	return MakeShareable(new FGameActionSpawnSection(SectionObject));
}

#undef LOCTEXT_NAMESPACE
