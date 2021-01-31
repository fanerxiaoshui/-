// Fill out your copyright notice in the Description page of Project Settings.


#include "Sequencer/GameActionTimeTestingTrackEditor.h"
#include <ISequencerSection.h>
#include <SequencerSectionPainter.h>

#include "Blueprint/BPNode_SequenceTimeTestingNode.h"
#include "Sequence/GameActionSequence.h"
#include "Sequence/GameActionTimeTestingTrack.h"

#define LOCTEXT_NAMESPACE "GameActionTimeTestingTrackEditor"

FGameActionTimeTestingTrackEditor::FGameActionTimeTestingTrackEditor(TSharedRef<ISequencer> InSequencer)
	:Super(InSequencer)
{

}

bool FGameActionTimeTestingTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UGameActionTimeTestingTrack::StaticClass();
}

bool FGameActionTimeTestingTrackEditor::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	return InSequence->GetClass() == UGameActionSequence::StaticClass();
}

TSharedRef<ISequencerSection> FGameActionTimeTestingTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	class FGameActionTimeTestingSectionEditor : public FSequencerSection
	{
		using Super = FSequencerSection;
	public:
		FGameActionTimeTestingSectionEditor(UMovieSceneSection& InSection, TWeakPtr<ISequencer> InSequencer)
			:Super(InSection), Sequencer(InSequencer)
		{}

		TWeakPtr<ISequencer> Sequencer;

		FText GetSectionTitle() const override
		{
			if (UGameActionTimeTestingSection* TimeTestingSection = Cast<UGameActionTimeTestingSection>(WeakSection.Get()))
			{
				const double Duration = TimeTestingSection->GetRange().Size<FFrameNumber>() / Sequencer.Pin()->GetFocusedTickResolution();
				FNumberFormattingOptions NumberFormattingOptions;
				NumberFormattingOptions.MaximumFractionalDigits = 1;
				NumberFormattingOptions.MinimumFractionalDigits = 1;
				return FText::Format(LOCTEXT("TimeTestingSectionTitle", "({0}s){1}"), FText::AsNumber(Duration, &NumberFormattingOptions), FText::FromString(TimeTestingSection->GetTimeTestingNode()->DisplayName));
			}
			return FText::GetEmpty();
		}

		int32 OnPaintSection(FSequencerSectionPainter& Painter) const override
		{
			UGameActionTimeTestingSection* TimeTestingSection = Cast<UGameActionTimeTestingSection>(WeakSection.Get());
			const FLinearColor TintColor = FLinearColor(0.05f, 0.2f, 0.05f);
			int32 LayerId = Painter.PaintSectionBackground(TimeTestingSection && TimeTestingSection->bIsEnable == false ? TintColor / 4.f : TintColor);
			return LayerId + 1;
		}
	};

	return MakeShareable(new FGameActionTimeTestingSectionEditor(SectionObject, GetSequencer()));
}

#undef LOCTEXT_NAMESPACE
