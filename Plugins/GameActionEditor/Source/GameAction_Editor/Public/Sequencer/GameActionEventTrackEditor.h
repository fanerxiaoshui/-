// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include <CoreMinimal.h>
#include <MovieSceneTrackEditor.h>
#include <MovieSceneCommonHelpers.h>
#include <IPropertyTypeCustomization.h>
#include <IDetailCustomization.h>

class UGameActionStateEvent;
struct FGameActionKeyEventChannel;
class SWidget;
struct FKeyDrawParams;
struct FKeyHandle;
struct FGameActionStateEventInnerKeyChannel;

/**
 * 
 */
inline bool CanCreateKeyEditor(const FGameActionKeyEventChannel* Channel) { return false; }
TSharedRef<SWidget> CreateKeyEditor(const TMovieSceneChannelHandle<FGameActionKeyEventChannel>& Channel, UMovieSceneSection* Section, const FGuid& InObjectBindingID, TWeakPtr<FTrackInstancePropertyBindings> PropertyBindings, TWeakPtr<ISequencer> InSequencer);
void DrawKeys(FGameActionKeyEventChannel* Channel, TArrayView<const FKeyHandle> InKeyHandles, const UMovieSceneSection* InOwner, TArrayView<FKeyDrawParams> OutKeyDrawParams);

class GAMEACTION_EDITOR_API FGameActionKeyEventValueCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance() { return MakeShareable(new FGameActionKeyEventValueCustomization()); }

	void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
};

class GAMEACTION_EDITOR_API FGameActionKeyEventTrackEditor : public FMovieSceneTrackEditor
{
	using Super = FMovieSceneTrackEditor;
public:
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> InSequencer) { return MakeShareable(new FGameActionKeyEventTrackEditor(InSequencer)); }

	FGameActionKeyEventTrackEditor(TSharedRef<ISequencer> InSequencer);

	// ISequencerTrackEditor interface

	bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
	bool SupportsSequence(UMovieSceneSequence* InSequence) const override;
	const FSlateBrush* GetIconBrush() const override { return FEditorStyle::GetBrush("Sequencer.Tracks.Event"); }
	void BuildAddTrackMenu(FMenuBuilder& MenuBuilder) override {}
	void BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass) override;
	void BuildTrackContextMenu(FMenuBuilder& MenuBuilder, UMovieSceneTrack* Track) override;
	TSharedPtr<SWidget> BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params) override;
	TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;
};

inline bool CanCreateKeyEditor(const FGameActionStateEventInnerKeyChannel* Channel) { return false; }
TSharedRef<SWidget> CreateKeyEditor(const TMovieSceneChannelHandle<FGameActionStateEventInnerKeyChannel>& Channel, UMovieSceneSection* Section, const FGuid& InObjectBindingID, TWeakPtr<FTrackInstancePropertyBindings> PropertyBindings, TWeakPtr<ISequencer> InSequencer);
void DrawKeys(FGameActionStateEventInnerKeyChannel* Channel, TArrayView<const FKeyHandle> InKeyHandles, const UMovieSceneSection* InOwner, TArrayView<FKeyDrawParams> OutKeyDrawParams);

class FGameActionStateEventDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShareable(new FGameActionStateEventDetails()); }

	void CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder) override;
};

class GAMEACTION_EDITOR_API FGameActionStateEventTrackEditor : public FMovieSceneTrackEditor
{
	using Super = FMovieSceneTrackEditor;
public:
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> InSequencer) { return MakeShareable(new FGameActionStateEventTrackEditor(InSequencer)); }

	FGameActionStateEventTrackEditor(TSharedRef<ISequencer> InSequencer);

	// ISequencerTrackEditor interface

	bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
	bool SupportsSequence(UMovieSceneSequence* InSequence) const override;
	const FSlateBrush* GetIconBrush() const override { return FEditorStyle::GetBrush("Sequencer.Tracks.Event"); }
	void BuildAddTrackMenu(FMenuBuilder& MenuBuilder) override {}
	void BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass) override;
	void BuildTrackContextMenu(FMenuBuilder& MenuBuilder, UMovieSceneTrack* Track) override;
	TSharedPtr<SWidget> BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params) override;
	TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;
private:
 	void CreateNewSection(UMovieSceneTrack* Track, TSubclassOf<UGameActionStateEvent> EventType, int32 RowIndex, bool bSelect);
};
