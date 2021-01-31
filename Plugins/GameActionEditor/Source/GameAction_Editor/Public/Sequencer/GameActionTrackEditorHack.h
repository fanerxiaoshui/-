// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <MovieSceneTrackEditor.h>


/**
 * 
 */
class GAMEACTION_EDITOR_API FGameActionTrackEditorHack : public FMovieSceneTrackEditor
{
	using Super = FMovieSceneTrackEditor;
public:
	FGameActionTrackEditorHack(TSharedRef<ISequencer> InSequencer)
		:Super(InSequencer)
	{}
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> InSequencer) { return MakeShareable(new FGameActionTrackEditorHack(InSequencer)); }
	bool SupportsType(TSubclassOf<class UMovieSceneTrack> TrackClass) const override { return false; }
	bool SupportsSequence(UMovieSceneSequence* InSequence) const override;
	void BuildAddTrackMenu(FMenuBuilder& MenuBuilder) override;
	void BuildObjectBindingEditButtons(TSharedPtr<SHorizontalBox> EditBox, const FGuid& ObjectBinding, const UClass* ObjectClass) override;
	void SyncViewPosition(const FVector& Location, const FRotator& Rotation, FGuid Guid);
	void BuildObjectBindingContextMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass) override;

private:
	void HandleAddEventTrackMenuEntryExecute(TArray<FGuid> InObjectBindingIDs, UClass* SectionType);
};