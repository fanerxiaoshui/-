// Fill out your copyright notice in the Description page of Project Settings.


#include "Sequencer/GameActionEventTrackEditor.h"
#include <Widgets/SBoxPanel.h>
#include <MovieSceneTrack.h>
#include <Framework/MultiBox/MultiBoxBuilder.h>
#include <Widgets/Text/STextBlock.h>
#include <Widgets/Layout/SConstraintCanvas.h>
#include <SequencerUtilities.h>
#include <MovieSceneSequenceEditor.h>
#include <MovieSceneSection.h>
#include <ISequencerSection.h>
#include <SequencerSectionPainter.h>
#include <EditorStyleSet.h>
#include <Styling/CoreStyle.h>
#include <Framework/Application/SlateApplication.h>
#include <Fonts/FontMeasure.h>
#include <ISequencer.h>
#include <CommonMovieSceneTools.h>
#include <AssetRegistryModule.h>
#include <Engine/Blueprint.h>
#include <Blueprint/BlueprintSupport.h>
#include <Curves/KeyHandle.h>
#include <KeyDrawParams.h>
#include <DetailWidgetRow.h>
#include <IDetailChildrenBuilder.h>
#include <ClassViewerModule.h>
#include <ClassViewerFilter.h>
#include <Misc/ScopeExit.h>
#include <DetailLayoutBuilder.h>
#include <Widgets/Layout/SBox.h>
#include <Widgets/Layout/SBorder.h>
#include <Widgets/Input/SComboButton.h>

#include "Sequence/GameActionEventTrack.h"
#include "Sequence/GameActionSequence.h"
#include "GameAction/GameActionEvent.h"

#define LOCTEXT_NAMESPACE "GameActionEventTrackEditor"

class FGameActionEventSectionBase
	: public FSequencerSection
{
public:
	FGameActionEventSectionBase(UMovieSceneSection& InSectionObject, TWeakPtr<ISequencer> InSequencer)
		: FSequencerSection(InSectionObject)
		, Sequencer(InSequencer)
	{}

protected:
	void PaintEventName(FSequencerSectionPainter& Painter, int32 LayerId, const FString& EventName, const bool bIsEventValid, float PixelPosition) const
	{
		static const int32   FontSize = 10;
		static const float   BoxOffsetPx = 10.f;
		static const TCHAR* WarningString = TEXT("\xf071");

		const FSlateFontInfo FontAwesomeFont = FEditorStyle::Get().GetFontStyle("FontAwesome.10");
		const FSlateFontInfo SmallLayoutFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
		const FLinearColor   DrawColor = FEditorStyle::GetSlateColor("SelectionColor").GetColor(FWidgetStyle());

		TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

		// Setup the warning size. Static since it won't ever change
		static FVector2D WarningSize = FontMeasureService->Measure(WarningString, FontAwesomeFont);
		const  FMargin   WarningPadding = (bIsEventValid || EventName.Len() == 0) ? FMargin(0.f) : FMargin(0.f, 0.f, 4.f, 0.f);
		const  FMargin   BoxPadding = FMargin(4.0f, 2.0f);

		const FVector2D  TextSize = FontMeasureService->Measure(EventName, SmallLayoutFont);
		const FVector2D  IconSize = bIsEventValid ? FVector2D::ZeroVector : WarningSize;
		const FVector2D  PaddedIconSize = IconSize + WarningPadding.GetDesiredSize();
		const FVector2D  BoxSize = FVector2D(TextSize.X + PaddedIconSize.X, FMath::Max(TextSize.Y, PaddedIconSize.Y)) + BoxPadding.GetDesiredSize();

		// Flip the text position if getting near the end of the view range
		bool  bDrawLeft = (Painter.SectionGeometry.Size.X - PixelPosition) < (BoxSize.X + 22.f) - BoxOffsetPx;
		float BoxPositionX = bDrawLeft ? PixelPosition - BoxSize.X - BoxOffsetPx : PixelPosition + BoxOffsetPx;
		if (BoxPositionX < 0.f)
		{
			BoxPositionX = 0.f;
		}

		FVector2D BoxOffset = FVector2D(BoxPositionX, Painter.SectionGeometry.Size.Y * .5f - BoxSize.Y * .5f);
		FVector2D IconOffset = FVector2D(BoxPadding.Left, BoxSize.Y * .5f - IconSize.Y * .5f);
		FVector2D TextOffset = FVector2D(IconOffset.X + PaddedIconSize.X, BoxSize.Y * .5f - TextSize.Y * .5f);

		// Draw the background box
		FSlateDrawElement::MakeBox(
			Painter.DrawElements,
			LayerId + 1,
			Painter.SectionGeometry.ToPaintGeometry(BoxOffset, BoxSize),
			FEditorStyle::GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			FLinearColor::Black.CopyWithNewOpacity(0.5f)
		);

		if (!bIsEventValid)
		{
			// Draw a warning icon for unbound repeaters
			FSlateDrawElement::MakeText(
				Painter.DrawElements,
				LayerId + 2,
				Painter.SectionGeometry.ToPaintGeometry(BoxOffset + IconOffset, IconSize),
				WarningString,
				FontAwesomeFont,
				Painter.bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
				FEditorStyle::GetWidgetStyle<FTextBlockStyle>("Log.Warning").ColorAndOpacity.GetSpecifiedColor()
			);
		}

		FSlateDrawElement::MakeText(
			Painter.DrawElements,
			LayerId + 2,
			Painter.SectionGeometry.ToPaintGeometry(BoxOffset + TextOffset, TextSize),
			EventName,
			SmallLayoutFont,
			Painter.bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
			DrawColor
		);
	}

	bool IsSectionSelected() const
	{
		TSharedPtr<ISequencer> SequencerPtr = Sequencer.Pin();

		TArray<UMovieSceneTrack*> SelectedTracks;
		SequencerPtr->GetSelectedTracks(SelectedTracks);

		UMovieSceneSection* Section = WeakSection.Get();
		UMovieSceneTrack* Track = Section ? CastChecked<UMovieSceneTrack>(Section->GetOuter()) : nullptr;
		return Track && SelectedTracks.Contains(Track);
	}

	TWeakPtr<ISequencer> Sequencer;
};

TSharedRef<SWidget> CreateKeyEditor(const TMovieSceneChannelHandle<FGameActionKeyEventChannel>& Channel, UMovieSceneSection* Section, const FGuid& InObjectBindingID, TWeakPtr<FTrackInstancePropertyBindings> PropertyBindings, TWeakPtr<ISequencer> InSequencer)
{
	return SNullWidget::NullWidget;
}

void DrawKeys(FGameActionKeyEventChannel* Channel, TArrayView<const FKeyHandle> InKeyHandles, const UMovieSceneSection* InOwner, TArrayView<FKeyDrawParams> OutKeyDrawParams)
{
	FKeyDrawParams ValidEventParams, InvalidEventParams;

	ValidEventParams.BorderBrush = FEditorStyle::Get().GetBrush("Sequencer.KeyDiamond");
	ValidEventParams.FillBrush = ValidEventParams.BorderBrush;

	InvalidEventParams.FillBrush = FEditorStyle::Get().GetBrush("Sequencer.KeyDiamond");
	InvalidEventParams.BorderBrush = InvalidEventParams.FillBrush;
	InvalidEventParams.FillTint = FLinearColor(1.f, 0.f, 0.f, .2f);

	TMovieSceneChannelData<FGameActionKeyEventValue> ChannelData = Channel->GetData();
	TArrayView<FGameActionKeyEventValue>             Events = ChannelData.GetValues();

	for (int32 Index = 0; Index < InKeyHandles.Num(); ++Index)
	{
		int32 KeyIndex = ChannelData.GetIndex(InKeyHandles[Index]);

		if (KeyIndex != INDEX_NONE && Events[KeyIndex].KeyEvent)
		{
			const FLinearColor KeyColor = Events[KeyIndex].KeyEvent->EventColor;
			ValidEventParams.FillTint = KeyColor;
			OutKeyDrawParams[Index] = ValidEventParams;
		}
		else
		{
			OutKeyDrawParams[Index] = InvalidEventParams;
		}
	}
}

void FGameActionKeyEventValueCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget(LOCTEXT("事件配置", "事件配置"), LOCTEXT("配置关键帧事件", "配置关键帧事件"))
		]
		.ValueContent()
		[
			StructPropertyHandle->CreatePropertyValueWidget()
		];
}

void FGameActionKeyEventValueCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	uint32 ChildNum;

	TSharedPtr<IPropertyHandle> KeyEventHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGameActionKeyEventValue, KeyEvent));
	KeyEventHandle->GetNumChildren(ChildNum);
	if (ChildNum == 1)
	{
		TSharedPtr<IPropertyHandle> KeyEventObjectHandle = KeyEventHandle->GetChildHandle(0);
		uint32 KeyEventChildNum;
		KeyEventObjectHandle->GetNumChildren(KeyEventChildNum);
		for (uint32 Idx = 0; Idx < KeyEventChildNum; ++Idx)
		{
			TSharedPtr<IPropertyHandle> ChildHandle = KeyEventObjectHandle->GetChildHandle(Idx);
			FProperty* Property = ChildHandle->GetProperty();
			if (Property && Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance) == false)
			{
				ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
			}
		}
	}

	StructPropertyHandle->GetNumChildren(ChildNum);
	for (uint32 Idx = 0; Idx < ChildNum; ++Idx)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(Idx);
		if (ChildHandle->GetProperty()->GetFName() != GET_MEMBER_NAME_CHECKED(FGameActionKeyEventValue, KeyEvent))
		{
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}
}

namespace GameActionEventTrackUtils
{
	const UClass* GetBindingObjectClass(UMovieScene& MovieScene, const FGuid& Guid)
	{
		if (FMovieScenePossessable* Possessable = MovieScene.FindPossessable(Guid))
		{
			return Possessable->GetPossessedObjectClass();
		}
		else if (FMovieSceneSpawnable* Spawnable = MovieScene.FindSpawnable(Guid))
		{
			return Spawnable->GetObjectTemplate()->GetClass();
		}
		return nullptr;
	}
}

FGameActionKeyEventTrackEditor::FGameActionKeyEventTrackEditor(TSharedRef<ISequencer> InSequencer)
	:Super(InSequencer)
{

}

bool FGameActionKeyEventTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UGameActionKeyEventTrack::StaticClass();
}

bool FGameActionKeyEventTrackEditor::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	return InSequence->IsA<UGameActionSequence>();
}

void FGameActionKeyEventTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	UMovieSceneSequence* RootMovieSceneSequence = GetSequencer()->GetRootMovieSceneSequence();
	if (RootMovieSceneSequence && RootMovieSceneSequence->IsA<UGameActionSequence>())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("关键帧事件", "关键帧事件"),
			LOCTEXT("添加关键帧事件导轨提示", "添加关键帧事件导轨"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.Tracks.Event"),
			FUIAction(FExecuteAction::CreateLambda([=]
			{
				UMovieScene* FocusedMovieScene = GetFocusedMovieScene();
				TSharedPtr<ISequencer> SequencerPtr = GetSequencer();

				if (FocusedMovieScene == nullptr || FocusedMovieScene->IsReadOnly() || SequencerPtr.IsValid() == false)
				{
					return;
				}

				const FScopedTransaction Transaction(NSLOCTEXT("Sequencer", "AddGameActionEventTrack_Transaction", "Add Game Action Key Event Track"));
				FocusedMovieScene->Modify();

				TArray<UGameActionKeyEventTrack*> NewTracks;

				for (FGuid InObjectBindingID : ObjectBindings)
				{
					if (InObjectBindingID.IsValid() && FocusedMovieScene->FindTrack<UGameActionKeyEventTrack>(InObjectBindingID) == nullptr)
					{
						UGameActionKeyEventTrack* NewObjectTrack = FocusedMovieScene->AddTrack<UGameActionKeyEventTrack>(InObjectBindingID);
						NewTracks.Add(NewObjectTrack);

						SequencerPtr->OnAddTrack(NewObjectTrack, InObjectBindingID);
					}
				}

				for (UGameActionKeyEventTrack* NewTrack : NewTracks)
				{
					NewTrack->Modify();
					NewTrack->SetDisplayName(LOCTEXT("KeySectionName", "关键帧事件"));

					UGameActionKeyEventSection* NewSection = NewObject<UGameActionKeyEventSection>(NewTrack);
					check(NewSection);

					NewTrack->AddSection(*NewSection);
					NewTrack->UpdateEasing();

					SequencerPtr->EmptySelection();
					SequencerPtr->SelectSection(NewSection);
					SequencerPtr->ThrobSectionSelection();

					SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
				}
			}))
		);
	}
}

void FGameActionKeyEventTrackEditor::BuildTrackContextMenu(FMenuBuilder& MenuBuilder, UMovieSceneTrack* Track)
{
	Super::BuildTrackContextMenu(MenuBuilder, Track);
}

TSharedPtr<SWidget> FGameActionKeyEventTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	check(Track);

	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (!SequencerPtr.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	FGuid TheObjectBinding;
	GetFocusedMovieScene()->FindTrackBinding(*Track, TheObjectBinding);

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(0.f)
			[
				SNew(SConstraintCanvas)
				+ SConstraintCanvas::Slot()
				.Offset(FMargin(21.f, 11.5f, 12.f, 13.f)) // 偏移该按钮挡住默认Key的按钮
				[
					SNew(SBorder)
					.Padding(0)
					.BorderImage(FEditorStyle::GetBrush("NoBorder"))
					.ColorAndOpacity_Lambda([=]
					{
						return Params.NodeIsHovered.Get() ? FLinearColor(1, 1, 1, 0.9f) : FLinearColor(1, 1, 1, 0.4f);
					})
					.IsEnabled(GetSequencer()->IsReadOnly() == false)
					[
						SNew(SComboButton)
						.HasDownArrow(false)
						.ButtonStyle(FEditorStyle::Get(), "FlatButton")
						.ToolTipText(LOCTEXT("新增关键帧事件", "新增关键帧事件"))
						.OnGetMenuContent_Lambda([this, WeakTrack = TWeakObjectPtr<UMovieSceneTrack>(Track), RowIndex = Track->GetMaxRowIndex() != 0 ? Params.TrackInsertRowIndex : 0, TheObjectBinding]
						{
							UMovieSceneTrack* TrackPtr = WeakTrack.Get();
							if (TrackPtr == nullptr)
							{
								return SNullWidget::NullWidget;
							}
							
							FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
							FClassViewerInitializationOptions Options;
							Options.Mode = EClassViewerMode::ClassPicker;
							Options.DisplayMode = EClassViewerDisplayMode::ListView;
							Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;
							Options.bIsBlueprintBaseOnly = false;
							class FKeyEventClassFilter : public IClassViewerFilter
							{
							public:
								FKeyEventClassFilter(const UClass* ObjectClass, const TSubclassOf<UGameActionInstanceBase>& ParentInstanceClass)
									: ObjectClass(ObjectClass), ParentInstanceClass(ParentInstanceClass)
								{}
								const UClass* ObjectClass;
								const TSubclassOf<UGameActionInstanceBase> ParentInstanceClass;
								bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
								{
									return InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated) == false && InClass->IsChildOf<UGameActionKeyEvent>() && InClass->GetDefaultObject<UGameActionKeyEvent>()->IsSupportClass(ObjectClass, ParentInstanceClass);
								}
								bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
								{
									return InUnloadedClassData->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated) == false && InUnloadedClassData->IsChildOf(UGameActionKeyEvent::StaticClass());
								}
							};
							Options.ClassFilter = MakeShareable(new FKeyEventClassFilter(GameActionEventTrackUtils::GetBindingObjectClass(*GetFocusedMovieScene(), TheObjectBinding), TSubclassOf<UGameActionInstanceBase>(TrackPtr->GetTypedOuter<UBlueprint>()->ParentClass)));

							TSharedRef<SWidget> ClassPickerWidget = ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateLambda([=](UClass* KeyEventClass)
								{
									ON_SCOPE_EXIT
									{
										FSlateApplication::Get().DismissAllMenus();
									};

									UMovieSceneTrack* TrackPtr = WeakTrack.Get();
									TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
									if (TrackPtr == nullptr || SequencerPtr.IsValid() == false || KeyEventClass == nullptr)
									{
										return;
									}

									const FFrameNumber CurrentTime = SequencerPtr->GetLocalTime().Time.FrameNumber;
									for (UMovieSceneSection* Section : TrackPtr->GetAllSections())
									{
										if (Section->GetRowIndex() == RowIndex)
										{
											UGameActionKeyEventSection* KeyEventSection = CastChecked<UGameActionKeyEventSection>(Section);
											TMovieSceneChannelData<FGameActionKeyEventValue> EventChannel = KeyEventSection->KeyEventChannel.GetData();
											if (EventChannel.FindKey(CurrentTime) == INDEX_NONE)
											{
												FGameActionKeyEventValue KeyEventValue;
												KeyEventValue.KeyEvent = NewObject<UGameActionKeyEvent>(KeyEventSection, KeyEventClass, NAME_None, RF_Transactional);
												EventChannel.AddKey(CurrentTime, KeyEventValue);
												SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
												return;
											}
										}
									}

									// 假如当前时间已存在关键帧，尝试在别的Section创建
									for (UMovieSceneSection* Section : TrackPtr->GetAllSections())
									{
										UGameActionKeyEventSection* KeyEventSection = CastChecked<UGameActionKeyEventSection>(Section);
										TMovieSceneChannelData<FGameActionKeyEventValue> EventChannel = KeyEventSection->KeyEventChannel.GetData();
										if (EventChannel.FindKey(CurrentTime) == INDEX_NONE)
										{
											FGameActionKeyEventValue KeyEventValue;
											KeyEventValue.KeyEvent = NewObject<UGameActionKeyEvent>(KeyEventSection, KeyEventClass, NAME_None, RF_Transactional);
											EventChannel.AddKey(CurrentTime, KeyEventValue);
											SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
											return;
										}
									}

									// 没有合适的Section时新建Section
									{
										UGameActionKeyEventSection* KeyEventSection = NewObject<UGameActionKeyEventSection>(TrackPtr);
										for (UMovieSceneSection* Section : TrackPtr->GetAllSections())
										{
											if (Section->GetRowIndex() >= RowIndex)
											{
												Section->SetRowIndex(Section->GetRowIndex() + 1);
											}
										}
										KeyEventSection->SetRowIndex(RowIndex);
										TrackPtr->AddSection(*KeyEventSection);
										TrackPtr->UpdateEasing();
										TMovieSceneChannelData<FGameActionKeyEventValue> EventChannel = KeyEventSection->KeyEventChannel.GetData();
										FGameActionKeyEventValue KeyEventValue;
										KeyEventValue.KeyEvent = NewObject<UGameActionKeyEvent>(KeyEventSection, KeyEventClass, NAME_None, RF_Transactional);
										EventChannel.AddKey(CurrentTime, KeyEventValue);
										SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
										return;
									}
								}));

							FMenuBuilder MenuBuilder(true, nullptr);
							MenuBuilder.AddWidget(ClassPickerWidget, FText::GetEmpty(), true);
							return MenuBuilder.MakeWidget();
						})
						.ForegroundColor( FSlateColor::UseForeground() )
						.ContentPadding(0)
						.IsFocusable(false)
						.ButtonContent()
						[
							SNew(STextBlock)
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.7"))
							.Text(FText::FromString(FString(TEXT("\xf055"))) /*fa-plus-circle*/)
						]
					]
				]
			]
		];
}

TSharedRef<ISequencerSection> FGameActionKeyEventTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	class FGameActionKeyEventSectionEditor : public FGameActionEventSectionBase
	{
		using Super = FGameActionEventSectionBase;
	public:
		FGameActionKeyEventSectionEditor(UMovieSceneSection& InSection, TWeakPtr<ISequencer> InSequencer)
			:Super(InSection, InSequencer)
		{}

		int32 OnPaintSection(FSequencerSectionPainter& Painter) const override
		{
			const int32 LayerId = Painter.PaintSectionBackground();

			UGameActionKeyEventSection* Section = CastChecked<UGameActionKeyEventSection>(WeakSection.Get());

			const FTimeToPixel& TimeToPixelConverter = Painter.GetTimeConverter();

			TArrayView<const FFrameNumber> Times = Section->KeyEventChannel.GetData().GetTimes();
			TArrayView<const FGameActionKeyEventValue> Events = Section->KeyEventChannel.GetData().GetValues();

			TRange<FFrameNumber> EventSectionRange = Section->GetRange();
			for (int32 KeyIndex = 0; KeyIndex < Times.Num(); ++KeyIndex)
			{
				FFrameNumber EventTime = Times[KeyIndex];
				if (EventSectionRange.Contains(EventTime))
				{
					const FGameActionKeyEventValue& Event = Events[KeyIndex];

					const float PixelPos = TimeToPixelConverter.FrameToPixel(EventTime);

					const bool bIsEventValid = Event.KeyEvent ? true : false;
					const FString EventName = bIsEventValid ? Event.KeyEvent->GetEventName() : TEXT("Missing");
					PaintEventName(Painter, LayerId, EventName, bIsEventValid, PixelPos);
				}
			}

			return LayerId + 3;
		}
	};

	return MakeShareable(new FGameActionKeyEventSectionEditor(SectionObject, GetSequencer()));
}

TSharedRef<SWidget> CreateKeyEditor(const TMovieSceneChannelHandle<FGameActionStateEventInnerKeyChannel>& Channel, UMovieSceneSection* Section, const FGuid& InObjectBindingID, TWeakPtr<FTrackInstancePropertyBindings> PropertyBindings, TWeakPtr<ISequencer> InSequencer)
{
	return SNullWidget::NullWidget;
}

void DrawKeys(FGameActionStateEventInnerKeyChannel* Channel, TArrayView<const FKeyHandle> InKeyHandles, const UMovieSceneSection* InOwner, TArrayView<FKeyDrawParams> OutKeyDrawParams)
{
	FKeyDrawParams ValidEventParams, InvalidEventParams;

	ValidEventParams.BorderBrush = FEditorStyle::Get().GetBrush("Sequencer.KeyDiamond");
	ValidEventParams.FillBrush = ValidEventParams.BorderBrush;

	InvalidEventParams.FillBrush = FEditorStyle::Get().GetBrush("Sequencer.KeyDiamond");
	InvalidEventParams.BorderBrush = InvalidEventParams.FillBrush;
	InvalidEventParams.FillTint = FLinearColor(1.f, 0.f, 0.f, .2f);

	TMovieSceneChannelData<FGameActionStateEventInnerKeyValue> ChannelData = Channel->GetData();
	TArrayView<FGameActionStateEventInnerKeyValue>             Events = ChannelData.GetValues();

	for (int32 Index = 0; Index < InKeyHandles.Num(); ++Index)
	{
		int32 KeyIndex = ChannelData.GetIndex(InKeyHandles[Index]);

		if (KeyIndex != INDEX_NONE && Events[KeyIndex].KeyEvent)
		{
			const FLinearColor KeyColor = Events[KeyIndex].KeyEvent->EventColor;
			ValidEventParams.FillTint = KeyColor;
			OutKeyDrawParams[Index] = ValidEventParams;
		}
		else
		{
			OutKeyDrawParams[Index] = InvalidEventParams;
		}
	}
}

void FGameActionStateEventDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	UObject* Object = Objects.Num() == 1 ? Objects[0].Get() : nullptr;
	if (Object && Object->HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FProperty* InstanceProperty = *It;
			if (InstanceProperty->HasAnyPropertyFlags(CPF_DisableEditOnInstance) || InstanceProperty->HasAllPropertyFlags(CPF_Edit) == false)
			{
				DetailBuilder.HideProperty(InstanceProperty->GetFName(), InstanceProperty->GetOwnerClass());
			}
		}
	}
}

FGameActionStateEventTrackEditor::FGameActionStateEventTrackEditor(TSharedRef<ISequencer> InSequencer)
	:Super(InSequencer)
{

}

bool FGameActionStateEventTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UGameActionStateEventTrack::StaticClass();
}

bool FGameActionStateEventTrackEditor::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	return InSequence->IsA<UGameActionSequence>();
}

class FStateEventClassFilter : public IClassViewerFilter
{
public:
	FStateEventClassFilter(const UClass* ObjectClass, const TSubclassOf<UGameActionInstanceBase>& ParentInstanceClass)
		: ObjectClass(ObjectClass), ParentInstanceClass(ParentInstanceClass)
	{}
	const UClass* ObjectClass;
	const TSubclassOf<UGameActionInstanceBase> ParentInstanceClass;
	bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		return InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated) == false && InClass->IsChildOf<UGameActionStateEvent>() && InClass->GetDefaultObject<UGameActionStateEvent>()->IsSupportClass(ObjectClass, ParentInstanceClass);
	}
	bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		return InUnloadedClassData->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated) == false && InUnloadedClassData->IsChildOf(UGameActionStateEvent::StaticClass());
	}
};

void FGameActionStateEventTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	UMovieSceneSequence* RootMovieSceneSequence = GetSequencer()->GetRootMovieSceneSequence();
	if (RootMovieSceneSequence && RootMovieSceneSequence->IsA<UGameActionSequence>())
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("状态事件", "状态事件"),
			LOCTEXT("状态事件提示", "新增状态事件"),
			FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder)
			{
				FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
				FClassViewerInitializationOptions Options;
				Options.Mode = EClassViewerMode::ClassPicker;
				Options.DisplayMode = EClassViewerDisplayMode::ListView;
				Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;
				Options.bIsBlueprintBaseOnly = false;
				Options.ClassFilter = MakeShareable(new FStateEventClassFilter(ObjectClass, TSubclassOf<UGameActionInstanceBase>(RootMovieSceneSequence->GetTypedOuter<UBlueprint>()->ParentClass)));

				TSharedRef<SWidget> ClassPickerWidget = ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateLambda([=](UClass* StateEventClass)
				{
					ON_SCOPE_EXIT
					{
						FSlateApplication::Get().DismissAllMenus();
					};

					UMovieScene* FocusedMovieScene = GetFocusedMovieScene();

					if (FocusedMovieScene == nullptr || FocusedMovieScene->IsReadOnly())
					{
						return;
					}

					const FScopedTransaction Transaction(NSLOCTEXT("Sequencer", "AddGameActionEventTrack_Transaction", "Add Game Action State Event Track"));
					FocusedMovieScene->Modify();

					TArray<UGameActionStateEventTrack*> NewTracks;

					for (const FGuid& ObjectBindingID : ObjectBindings)
					{
						if (ObjectBindingID.IsValid() && FocusedMovieScene->FindTrack<UGameActionStateEventTrack>(ObjectBindingID) == nullptr)
						{
							UGameActionStateEventTrack* NewObjectTrack = FocusedMovieScene->AddTrack<UGameActionStateEventTrack>(ObjectBindingID);
							NewTracks.Add(NewObjectTrack);

							if (GetSequencer().IsValid())
							{
								GetSequencer()->OnAddTrack(NewObjectTrack, ObjectBindingID);
							}
						}
					}

					for (UGameActionStateEventTrack* NewTrack : NewTracks)
					{
						CreateNewSection(NewTrack, TSubclassOf<UGameActionStateEvent>(StateEventClass), 0, false);
						NewTrack->SetDisplayName(LOCTEXT("StateSectionName", "状态事件"));
					}
				}));
				
				SubMenuBuilder.AddWidget(ClassPickerWidget, FText::GetEmpty(), true);
			}),
			false,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.Tracks.Event")
		);
	}
}

void FGameActionStateEventTrackEditor::BuildTrackContextMenu(FMenuBuilder& MenuBuilder, UMovieSceneTrack* Track)
{
	Super::BuildTrackContextMenu(MenuBuilder, Track);
}

TSharedPtr<SWidget> FGameActionStateEventTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	check(Track);

	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (!SequencerPtr.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	FGuid TheObjectBinding;
	GetFocusedMovieScene()->FindTrackBinding(*Track, TheObjectBinding);

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			FSequencerUtilities::MakeAddButton(LOCTEXT("状态事件", "状态事件"), FOnGetContent::CreateLambda([this, WeakTrack = TWeakObjectPtr<UMovieSceneTrack>(Track), RowIndex = Params.TrackInsertRowIndex, TheObjectBinding]
			{
				UMovieSceneTrack* TrackPtr = WeakTrack.Get();
				if (TrackPtr == nullptr)
				{
					return SNullWidget::NullWidget;
				}
				
				FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
				FClassViewerInitializationOptions Options;
				Options.Mode = EClassViewerMode::ClassPicker;
				Options.DisplayMode = EClassViewerDisplayMode::ListView;
				Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;
				Options.bIsBlueprintBaseOnly = false;
				Options.ClassFilter = MakeShareable(new FStateEventClassFilter(GameActionEventTrackUtils::GetBindingObjectClass(*GetFocusedMovieScene(), TheObjectBinding), TSubclassOf<UGameActionInstanceBase>(TrackPtr->GetTypedOuter<UBlueprint>()->ParentClass)));

				TSharedRef<SWidget> ClassPickerWidget = ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateLambda([=](UClass* StateEventClass)
				{
					ON_SCOPE_EXIT
					{
						FSlateApplication::Get().DismissAllMenus();
					};

					if (UMovieSceneTrack* TrackPtr = WeakTrack.Get())
					{
						FGameActionStateEventTrackEditor::CreateNewSection(TrackPtr, TSubclassOf<UGameActionStateEvent>(StateEventClass), RowIndex + 1, true);
					}
				}));

				FMenuBuilder MenuBuilder(true, nullptr);
				MenuBuilder.AddWidget(ClassPickerWidget, FText::GetEmpty(), true);
				return MenuBuilder.MakeWidget();
			}), Params.NodeIsHovered, GetSequencer())
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(0.f)
			[
				SNew(SConstraintCanvas)
				+ SConstraintCanvas::Slot()
				.Offset(FMargin(21.f, 11.5f, 12.f, 13.f)) // 偏移该按钮挡住默认Key的按钮
				[
					SNew(SBorder)
					.Padding(0)
					.BorderImage(FEditorStyle::GetBrush("NoBorder"))
					.ColorAndOpacity_Lambda([=]
					{
						return Params.NodeIsHovered.Get() ? FLinearColor(1, 1, 1, 0.9f) : FLinearColor(1, 1, 1, 0.4f);
					})
					.IsEnabled(GetSequencer()->IsReadOnly() == false)
					[
						SNew(SComboButton)
						.HasDownArrow(false)
						.ButtonStyle(FEditorStyle::Get(), "FlatButton")
						.ToolTipText(LOCTEXT("添加状态关键帧事件", "添加状态关键帧事件"))
						.OnGetMenuContent_Lambda([this, WeakTrack = TWeakObjectPtr<UMovieSceneTrack>(Track), RowIndex = Track->GetMaxRowIndex() != 0 ? Params.TrackInsertRowIndex : 0, TheObjectBinding]
						{
							UGameActionStateEventSection* KeyEventSection = nullptr;
							UMovieSceneTrack* TrackPtr = WeakTrack.Get();
							const FFrameNumber CurrentTime = GetSequencer()->GetLocalTime().Time.FrameNumber;
							for (UMovieSceneSection* Section : TrackPtr->GetAllSections())
							{
								if (Section->GetRowIndex() == RowIndex && Section->GetRange().Contains(CurrentTime))
								{
									KeyEventSection = CastChecked<UGameActionStateEventSection>(Section);
									break;
								}
							}
							UClass* EventClass = KeyEventSection && KeyEventSection->StateEvent ? KeyEventSection->StateEvent->GetClass() : nullptr;

							FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
							FClassViewerInitializationOptions Options;
							Options.Mode = EClassViewerMode::ClassPicker;
							Options.DisplayMode = EClassViewerDisplayMode::ListView;
							Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;
							Options.bIsBlueprintBaseOnly = false;
							class FStateEventInnerKeyClassFilter : public IClassViewerFilter
							{
							public:
								FStateEventInnerKeyClassFilter(const UClass* StateEventClass) : StateEventClass(StateEventClass) {}
								const UClass* StateEventClass;
								bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
								{
									return InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated) == false && InClass->IsChildOf<UGameActionStateInnerKeyEvent>() && InClass->GetDefaultObject<UGameActionStateInnerKeyEvent>()->IsSupportState(StateEventClass);
								}
								bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
								{
									return InUnloadedClassData->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated) == false && InUnloadedClassData->IsChildOf(UGameActionStateInnerKeyEvent::StaticClass());
								}
							};

							Options.ClassFilter = MakeShareable(new FStateEventInnerKeyClassFilter(EventClass));

							TSharedRef<SWidget> ClassPickerWidget = ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateLambda([=](UClass* KeyEventClass)
							{
								ON_SCOPE_EXIT
								{
									FSlateApplication::Get().DismissAllMenus();
								};

								if (TrackPtr == nullptr || KeyEventClass == nullptr)
								{
									return;
								}

								if (ensure(KeyEventClass->GetDefaultObject<UGameActionStateInnerKeyEvent>()->IsSupportState(EventClass)))
								{
									TMovieSceneChannelData<FGameActionStateEventInnerKeyValue> EventChannel = KeyEventSection->InnerKeyChannel.GetData();
									FGameActionStateEventInnerKeyValue KeyEventValue;
									KeyEventValue.KeyEvent = NewObject<UGameActionStateInnerKeyEvent>(KeyEventSection, KeyEventClass, NAME_None, RF_Transactional);
									EventChannel.AddKey(CurrentTime, KeyEventValue);
									GetSequencer()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
								}
							}));
							return ClassPickerWidget;
						})
						.ForegroundColor( FSlateColor::UseForeground() )
						.ContentPadding(0)
						.IsFocusable(false)
						.ButtonContent()
						[
							SNew(STextBlock)
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.7"))
							.Text(FText::FromString(FString(TEXT("\xf055"))) /*fa-plus-circle*/)
						]
					]
				]
			]
		];
}

TSharedRef<ISequencerSection> FGameActionStateEventTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	class FGameActionStateEventSectionEditor : public FGameActionEventSectionBase
	{
		using Super = FGameActionEventSectionBase;
	public:
		FGameActionStateEventSectionEditor(UMovieSceneSection& InSection, TWeakPtr<ISequencer> InSequencer)
			:Super(InSection, InSequencer)
		{}

		int32 OnPaintSection(FSequencerSectionPainter& Painter) const override
		{
			UGameActionStateEventSection* Section = CastChecked<UGameActionStateEventSection>(WeakSection.Get());
			const FLinearColor BackgroundColor = Section->StateEvent ? Section->StateEvent->EventColor / 2.f : FLinearColor::Red;
			int32 LayerId = Painter.PaintSectionBackground(BackgroundColor);
			{
				const float TextOffsetX = Section->GetRange().GetLowerBound().IsClosed() ? FMath::Max(0.f, Painter.GetTimeConverter().FrameToPixel(Section->GetRange().GetLowerBoundValue())) : 0.f;
				const bool bIsEventValid = Section->StateEvent ? true : false;
				const FString EventName = bIsEventValid ? Section->StateEvent->GetEventName() : TEXT("Missing");
				PaintEventName(Painter, LayerId, EventName, bIsEventValid, TextOffsetX);
			}
			LayerId += 1;
			
			const FTimeToPixel& TimeToPixelConverter = Painter.GetTimeConverter();
			TArrayView<const FFrameNumber> Times = Section->InnerKeyChannel.GetData().GetTimes();
			TArrayView<const FGameActionStateEventInnerKeyValue> Events = Section->InnerKeyChannel.GetData().GetValues();

			TRange<FFrameNumber> EventSectionRange = Section->GetRange();
			for (int32 KeyIndex = 0; KeyIndex < Times.Num(); ++KeyIndex)
			{
				FFrameNumber EventTime = Times[KeyIndex];
				if (EventSectionRange.Contains(EventTime))
				{
					const FGameActionStateEventInnerKeyValue& Event = Events[KeyIndex];

					const float PixelPos = TimeToPixelConverter.FrameToPixel(EventTime);
					const bool bIsEventValid = Event.KeyEvent ? true : false;
					const FString EventName = bIsEventValid ? Event.KeyEvent->GetEventName() : TEXT("Missing");
					PaintEventName(Painter, LayerId, EventName, bIsEventValid, PixelPos);
				}
			}
			return LayerId + 3;
		}
	};

	return MakeShareable(new FGameActionStateEventSectionEditor(SectionObject, GetSequencer()));
}

void FGameActionStateEventTrackEditor::CreateNewSection(UMovieSceneTrack* Track, TSubclassOf<UGameActionStateEvent> EventType, int32 RowIndex, bool bSelect)
{
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (SequencerPtr.IsValid())
	{
		UMovieScene* FocusedMovieScene = GetFocusedMovieScene();
		FQualifiedFrameTime CurrentTime = SequencerPtr->GetLocalTime();

		FScopedTransaction Transaction(LOCTEXT("CreateNewSectionTransactionText", "Add Section"));

		UGameActionStateEventSection* NewSection = NewObject<UGameActionStateEventSection>(Track, NAME_None, RF_Transactional);
		check(NewSection);

		NewSection->StateEvent = NewObject<UGameActionStateEvent>(NewSection, EventType, NAME_None, RF_Transactional);

		int32 OverlapPriority = 0;
		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			if (Section->GetRowIndex() >= RowIndex)
			{
				Section->SetRowIndex(Section->GetRowIndex() + 1);
			}
			OverlapPriority = FMath::Max(Section->GetOverlapPriority() + 1, OverlapPriority);
		}

		Track->Modify();

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
		NewSection->SetOverlapPriority(OverlapPriority);
		NewSection->SetRowIndex(RowIndex);

		Track->AddSection(*NewSection);
		Track->UpdateEasing();

		if (bSelect)
		{
			SequencerPtr->EmptySelection();
			SequencerPtr->SelectSection(NewSection);
			SequencerPtr->ThrobSectionSelection();
		}

		SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	}
}

#undef LOCTEXT_NAMESPACE
