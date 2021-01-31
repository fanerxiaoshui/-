// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <MovieSceneTrackEditor.h>

/**
 * 
 */
class GAMEACTION_EDITOR_API FGameActionTimeTestingTrackEditor : public FMovieSceneTrackEditor
{
    using Super = FMovieSceneTrackEditor;
public:
    static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> InSequencer) { return MakeShareable(new FGameActionTimeTestingTrackEditor(InSequencer)); }

    FGameActionTimeTestingTrackEditor(TSharedRef<ISequencer> InSequencer);
    bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
    bool SupportsSequence(UMovieSceneSequence* InSequence) const override;
    TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;
};
