// Fill out your copyright notice in the Description page of Project Settings.


#include "Sequencer/GameActionTrackEditorHack.h"
#include <Tracks/MovieSceneCameraCutTrack.h>
#include <Tracks/MovieSceneEventTrack.h>
#include <Tracks/MovieSceneAudioTrack.h>
#include <Styling/SlateIconFinder.h>
#include <Framework/MultiBox/MultiBoxBuilder.h>
#include <Widgets/Input/SCheckBox.h>
#include <EditorStyleSet.h>
#include <Widgets/SBoxPanel.h>
#include <Framework/Application/SlateApplication.h>

#include "Blueprint/GameActionBlueprint.h"
#include "Editor/GameActionPreviewScene.h"
#include "Sections/MovieSceneEventRepeaterSection.h"
#include "Sections/MovieSceneEventTriggerSection.h"
#include "Sequence/GameActionSequence.h"

#define LOCTEXT_NAMESPACE "GameActionTrackEditorHack"

bool FGameActionTrackEditorHack::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	return InSequence && InSequence->IsA<UGameActionSequence>();
}

void FGameActionTrackEditorHack::BuildAddTrackMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddTrack", "Audio Track"),
		LOCTEXT("AddTooltip", "Adds a new master audio track that can play sounds."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.Tracks.Audio"),
		FUIAction(
			FExecuteAction::CreateLambda([this]
			{
				UMovieScene* FocusedMovieScene = GetFocusedMovieScene();

				if (FocusedMovieScene == nullptr)
				{
					return;
				}

				if (FocusedMovieScene->IsReadOnly())
				{
					return;
				}

				const FScopedTransaction Transaction(NSLOCTEXT("Sequencer", "AddAudioTrack_Transaction", "Add Audio Track"));
				FocusedMovieScene->Modify();
				
				auto NewTrack = FocusedMovieScene->AddMasterTrack<UMovieSceneAudioTrack>();
				ensure(NewTrack);

				NewTrack->SetDisplayName(LOCTEXT("AudioTrackName", "Audio"));

				if (GetSequencer().IsValid())
				{
					GetSequencer()->OnAddTrack(NewTrack, FGuid());
				}
			})
		)
	);

	MenuBuilder.AddSubMenu(
		LOCTEXT("AddEventTrack", "Event Track"),
		LOCTEXT("AddEventTooltip", "Adds a new event track that can trigger events on the timeline."),
		FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder)
		{
			SubMenuBuilder.AddMenuEntry(
				LOCTEXT("AddNewTriggerSection", "Trigger"),
				LOCTEXT("AddNewTriggerSectionTooltip", "Adds a new section that can trigger a specific event at a specific time"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FGameActionTrackEditorHack::HandleAddEventTrackMenuEntryExecute, TArray<FGuid>(), UMovieSceneEventTriggerSection::StaticClass())
				)
			);

			SubMenuBuilder.AddMenuEntry(
				LOCTEXT("AddNewRepeaterSection", "Repeater"),
				LOCTEXT("AddNewRepeaterSectionTooltip", "Adds a new section that triggers an event every time it's evaluated"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FGameActionTrackEditorHack::HandleAddEventTrackMenuEntryExecute, TArray<FGuid>(), UMovieSceneEventRepeaterSection::StaticClass())
				)
			);
		}),
		false,
		FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.Tracks.Event")
	);
	
	MenuBuilder.AddMenuEntry(
		LOCTEXT("添加相机导轨", "添加相机导轨"),
		LOCTEXT("添加相机导轨提示", "添加游戏行为相机导轨"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.Tracks.CameraCut"),
		FUIAction(
			FExecuteAction::CreateLambda([this]
			{
				UMovieScene* FocusedMovieScene = GetFocusedMovieScene();
				if (FocusedMovieScene->IsReadOnly())
				{
					return;
				}

				UMovieSceneCameraCutTrack* CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(FocusedMovieScene->GetCameraCutTrack());
				if (CameraCutTrack == nullptr)
				{
					const FScopedTransaction Transaction(LOCTEXT("添加相机导轨_Transaction", "添加相机导轨"));
					FocusedMovieScene->Modify();
					CameraCutTrack = CastChecked<UMovieSceneCameraCutTrack>(FocusedMovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
					if (GetSequencer().IsValid())
					{
						GetSequencer()->OnAddTrack(CameraCutTrack, FGuid());
					}
				}
			}),
			FCanExecuteAction::CreateLambda([this]
			{
				UMovieScene* FocusedMovieScene = GetFocusedMovieScene();
				return ((FocusedMovieScene != nullptr) && (FocusedMovieScene->GetCameraCutTrack() == nullptr));
			})
		)
	);
}

void FGameActionTrackEditorHack::BuildObjectBindingEditButtons(TSharedPtr<SHorizontalBox> EditBox, const FGuid& ObjectBinding, const UClass* ObjectClass)
{
	UMovieSceneSequence* MovieSceneSequence = GetSequencer()->GetFocusedMovieSceneSequence();
	if (MovieSceneSequence == nullptr || MovieSceneSequence->IsA<UGameActionSequence>() == false)
	{
		return;
	}

	// HACK：将默认的F3DTransformTrackEditor上的 button to lock the viewport to the camera 移除
	if (EditBox->NumSlots() > 2)
	{
		EditBox->RemoveSlot(EditBox->GetChildren()->GetChildAt(2));
	}
	EditBox.Get()->AddSlot()
	       .VAlign(VAlign_Center)
	       .HAlign(HAlign_Right)
	       .AutoWidth()
	       .Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SCheckBox)
				.IsFocusable(false)
				.Visibility([=]
				{
					if (AActor* Actor = Cast<AActor>(GetSequencer()->FindSpawnedObjectOrTemplate(ObjectBinding)))
					{
						if (UCameraComponent* CameraComponent = MovieSceneHelpers::CameraComponentFromActor(Actor))
						{
			               return EVisibility::Visible;
						}
					}
					return EVisibility::Collapsed;
				}())
				.IsChecked_Lambda([=]
				{
					if (UGameActionSequence* GameActionSequence = Cast<UGameActionSequence>(GetSequencer()->GetFocusedMovieSceneSequence()))
					{
						if (GameActionSequence->BelongToEditScene.IsValid())
						{
							if (GameActionSequence->BelongToEditScene.Pin()->ToViewActor == GetSequencer()->FindSpawnedObjectOrTemplate(ObjectBinding))
							{
								return ECheckBoxState::Checked;
							}
						}
					}
					return ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([=](ECheckBoxState CheckBoxState)
	            {
					if (UGameActionSequence* GameActionSequence = Cast<UGameActionSequence>(GetSequencer()->GetFocusedMovieSceneSequence()))
					{
						if (GameActionSequence->BelongToEditScene.IsValid())
						{
							UObject* Template = GetSequencer()->FindSpawnedObjectOrTemplate(ObjectBinding);
							TSharedPtr<FGameActionEditScene> EditScene = GameActionSequence->BelongToEditScene.Pin();
							if (EditScene->ToViewActor != Template)
							{
								EditScene->ToViewActor = Cast<AActor>(Template);
								EditScene->OnMoveViewActor.BindSP(SharedThis(this), &FGameActionTrackEditorHack::SyncViewPosition, ObjectBinding);
								EditScene->CameraCutsViewActor.Reset();
								GetSequencer()->SetPerspectiveViewportCameraCutEnabled(false);
							}
							else
							{
								EditScene->ToViewActor.Reset();
							}
						}
					}
				})
				.ToolTipText(LOCTEXT("预览该Camera", "预览该Camera"))
				.ForegroundColor(FLinearColor::White)
				.CheckedImage(FEditorStyle::GetBrush("Sequencer.LockCamera"))
				.CheckedHoveredImage(FEditorStyle::GetBrush("Sequencer.LockCamera"))
				.CheckedPressedImage(FEditorStyle::GetBrush("Sequencer.LockCamera"))
				.UncheckedImage(FEditorStyle::GetBrush("Sequencer.UnlockCamera"))
				.UncheckedHoveredImage(FEditorStyle::GetBrush("Sequencer.UnlockCamera"))
				.UncheckedPressedImage(FEditorStyle::GetBrush("Sequencer.UnlockCamera"))
			];
}

void FGameActionTrackEditorHack::SyncViewPosition(const FVector& Location, const FRotator& Rotation, FGuid Guid)
{
	UMovieScene* MovieScene = GetSequencer()->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(Guid))
	{
		if (AActor* Actor = Cast<AActor>(Spawnable->GetObjectTemplate()))
		{
			Actor->Modify();
			Actor->SetActorLocationAndRotation(Location, Rotation);
		}
	}
}

void FGameActionTrackEditorHack::BuildObjectBindingContextMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	UGameActionSequence* GameActionSequence = Cast<UGameActionSequence>(GetSequencer()->GetFocusedMovieSceneSequence());
	if (GameActionSequence == nullptr)
	{
		return;
	}
	
	if (ObjectBindings.Num() == 1)
	{
		const FGuid ObjectBinding = ObjectBindings[0];
		if (GetSequencer()->FindSpawnedObjectOrTemplate(ObjectBinding) == nullptr)
		{
			MenuBuilder.AddSubMenu(LOCTEXT("重新绑定Possessable对象", "重新绑定Possessable对象"), LOCTEXT("修复失效的Possessable对象", "修复失效的Possessable对象"),
			FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder)
			{
				const TSharedPtr<ISequencer> Sequencer = GetSequencer();
				TSet<FName> ExistingPossessedObjects;
				if (Sequencer.IsValid())
				{
					UMovieSceneSequence* MovieSceneSequence = Sequencer->GetFocusedMovieSceneSequence();
					UMovieScene* MovieScene = MovieSceneSequence->GetMovieScene();
					if (MovieScene)
					{
						for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); Index++)
						{
							const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(Index);
							if (GetSequencer()->FindSpawnedObjectOrTemplate(Possessable.GetGuid()) != nullptr)
							{
								ExistingPossessedObjects.Add(*Possessable.GetName());
							}
						}
					}
				}

				FMovieScenePossessable* RebindingPossessable = GameActionSequence->GetMovieScene()->FindPossessable(ObjectBinding);
				check(RebindingPossessable);
				
				TArray<FObjectProperty*> CanRebindingPossessable;
				if (const UGameActionBlueprint* GameActionBlueprint = GameActionSequence->GetTypedOuter<UGameActionBlueprint>())
				{
					if (UClass* SkeletonGeneratedClass = GameActionBlueprint->SkeletonGeneratedClass)
					{
						for (TFieldIterator<FObjectProperty> It(SkeletonGeneratedClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
						{
							FObjectProperty* ObjectProperty = *It;
							if (ObjectProperty->PropertyClass && ObjectProperty->PropertyClass->IsChildOf(RebindingPossessable->GetPossessedObjectClass()) && ObjectProperty->GetBoolMetaData(UGameActionBlueprint::MD_GameActionPossessableReference))
							{
								if (ExistingPossessedObjects.Contains(ObjectProperty->GetFName()) == false)
								{
									CanRebindingPossessable.Add(ObjectProperty);
								}
							}
						}
					}
				}

				for (FObjectProperty* ObjectProperty : CanRebindingPossessable)
				{
					const FName PropertyName = ObjectProperty->GetFName();
					const FSlateIcon ActorIcon = FSlateIconFinder::FindIconForClass(ObjectProperty->PropertyClass);
					SubMenuBuilder.AddMenuEntry(FText::FromName(PropertyName), FText(), ActorIcon, FExecuteAction::CreateLambda([=]
					{
						FMovieScenePossessable* RebindingPossessable = GameActionSequence->GetMovieScene()->FindPossessable(ObjectBinding);
						check(RebindingPossessable);
						RebindingPossessable->SetName(PropertyName.ToString());
						GameActionSequence->PossessableActors.FindOrAdd(ObjectBinding) = PropertyName;
						
						FSlateApplication::Get().DismissAllMenus();
					}));
				}

				if (CanRebindingPossessable.Num() == 0)
				{
					SubMenuBuilder.AddWidget(SNew(STextBlock).Text(LOCTEXT("没有可供重新绑定的Possessable引用", "没有可供重新绑定的Possessable引用")), FText::GetEmpty());
				}
			}));
		}
	}
}

void FGameActionTrackEditorHack::HandleAddEventTrackMenuEntryExecute(TArray<FGuid> InObjectBindingIDs, UClass* SectionType)
{
	UMovieScene* FocusedMovieScene = GetFocusedMovieScene();

	if (FocusedMovieScene == nullptr)
	{
		return;
	}

	if (FocusedMovieScene->IsReadOnly())
	{
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("Sequencer", "AddEventTrack_Transaction", "Add Event Track"));
	FocusedMovieScene->Modify();

	TArray<UMovieSceneEventTrack*> NewTracks;

	for (FGuid InObjectBindingID : InObjectBindingIDs)
	{
		if (InObjectBindingID.IsValid())
		{
			UMovieSceneEventTrack* NewObjectTrack = FocusedMovieScene->AddTrack<UMovieSceneEventTrack>(InObjectBindingID);
			NewTracks.Add(NewObjectTrack);

			if (GetSequencer().IsValid())
			{
				GetSequencer()->OnAddTrack(NewObjectTrack, InObjectBindingID);
			}
		}
	}

	if (!NewTracks.Num())
	{
		UMovieSceneEventTrack* NewMasterTrack = FocusedMovieScene->AddMasterTrack<UMovieSceneEventTrack>();
		NewTracks.Add(NewMasterTrack);
		if (GetSequencer().IsValid())
		{
			GetSequencer()->OnAddTrack(NewMasterTrack, FGuid());
		}
	}

	check(NewTracks.Num() != 0);

	for (UMovieSceneEventTrack* NewTrack : NewTracks)
	{
		TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
		if (SequencerPtr.IsValid())
		{
			FQualifiedFrameTime CurrentTime = SequencerPtr->GetLocalTime();

			UMovieSceneSection* NewSection = NewObject<UMovieSceneSection>(NewTrack, SectionType);
			check(NewSection);

			const int32 RowIndex = 0;
			int32 OverlapPriority = 0;
			for (UMovieSceneSection* Section : NewTrack->GetAllSections())
			{
				if (Section->GetRowIndex() >= RowIndex)
				{
					Section->SetRowIndex(Section->GetRowIndex() + 1);
				}
				OverlapPriority = FMath::Max(Section->GetOverlapPriority() + 1, OverlapPriority);
			}

			NewTrack->Modify();

			if (SectionType == UMovieSceneEventTriggerSection::StaticClass())
			{
				NewSection->SetRange(TRange<FFrameNumber>::All());
			}
			else
			{

				TRange<FFrameNumber> NewSectionRange;

				if (CurrentTime.Time.FrameNumber < FocusedMovieScene->GetPlaybackRange().GetUpperBoundValue())
				{
					NewSectionRange = TRange<FFrameNumber>(CurrentTime.Time.FrameNumber, FocusedMovieScene->GetPlaybackRange().GetUpperBoundValue());
				}
				else
				{
					const float DefaultLengthInSeconds = 5.f;
					NewSectionRange = TRange<FFrameNumber>(CurrentTime.Time.FrameNumber, CurrentTime.Time.FrameNumber + (DefaultLengthInSeconds * SequencerPtr->GetFocusedTickResolution()).FloorToFrame());
				}

				NewSection->SetRange(NewSectionRange);
			}

			NewSection->SetOverlapPriority(OverlapPriority);
			NewSection->SetRowIndex(RowIndex);

			NewTrack->AddSection(*NewSection);
			NewTrack->UpdateEasing();

			SequencerPtr->EmptySelection();
			SequencerPtr->SelectSection(NewSection);
			SequencerPtr->ThrobSectionSelection();

			SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
		}
		
		NewTrack->SetDisplayName(LOCTEXT("TrackName", "Events"));
	}
}

#undef LOCTEXT_NAMESPACE
