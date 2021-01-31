// Fill out your copyright notice in the Description page of Project Settings.


#include "Sequencer/GameActionAnimationTrackEditor.h"
#include <AssetRegistryModule.h>
#include <CommonMovieSceneTools.h>
#include <ContentBrowserModule.h>
#include <IContentBrowserSingleton.h>
#include <IDetailChildrenBuilder.h>
#include <IDetailPropertyRow.h>
#include <IPropertyUtilities.h>
#include <PropertyCustomizationHelpers.h>
#include <SequencerSectionPainter.h>
#include <SequencerUtilities.h>
#include <Animation/AnimMontage.h>
#include <DragAndDrop/AssetDragDropOp.h>
#include <Engine/SCS_Node.h>
#include <Fonts/FontMeasure.h>
#include <Components/SkeletalMeshComponent.h>
#include <Misc/MessageDialog.h>
#include <Framework/MultiBox/MultiBoxBuilder.h>
#include <Animation/AnimSequence.h>
#include <Widgets/Layout/SBox.h>
#include <Framework/Application/SlateApplication.h>

#include "Sequence/GameActionAnimationTrack.h"
#include "Sequence/GameActionSequence.h"

#define LOCTEXT_NAMESPACE "GameActionAnimationTrackEditor"

USkeletalMeshComponent* AcquireSkeletalMeshFromObjectGuid(const FGuid& Guid, TSharedPtr<ISequencer> SequencerPtr)
{
	UObject* BoundObject = SequencerPtr.IsValid() ? SequencerPtr->FindSpawnedObjectOrTemplate(Guid) : nullptr;

	if (AActor* Actor = Cast<AActor>(BoundObject))
	{
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(Component))
			{
				return SkeletalMeshComp;
			}
		}
	}
	else if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(BoundObject))
	{
		if (SkeletalMeshComponent->SkeletalMesh)
		{
			return SkeletalMeshComponent;
		}
	}

	return nullptr;
}

USkeleton* GetSkeletonFromComponent(UActorComponent* InComponent)
{
	USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(InComponent);
	if (SkeletalMeshComp && SkeletalMeshComp->SkeletalMesh && SkeletalMeshComp->SkeletalMesh->Skeleton)
	{
		// @todo Multiple actors, multiple components
		return SkeletalMeshComp->SkeletalMesh->Skeleton;
	}

	return nullptr;
}

USkeleton* AcquireSkeletonFromObjectGuid(const FGuid& Guid, TSharedPtr<ISequencer> SequencerPtr)
{
	UObject* BoundObject = SequencerPtr.IsValid() ? SequencerPtr->FindSpawnedObjectOrTemplate(Guid) : nullptr;

	AActor* Actor = Cast<AActor>(BoundObject);

	if (!Actor)
	{
		if (UChildActorComponent* ChildActorComponent = Cast<UChildActorComponent>(BoundObject))
		{
			Actor = ChildActorComponent->GetChildActor();
		}
	}

	if (Actor)
	{
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (USkeleton* Skeleton = GetSkeletonFromComponent(Component))
			{
				return Skeleton;
			}
		}

		AActor* ActorCDO = Cast<AActor>(Actor->GetClass()->GetDefaultObject());
		if (ActorCDO)
		{
			for (UActorComponent* Component : ActorCDO->GetComponents())
			{
				if (USkeleton* Skeleton = GetSkeletonFromComponent(Component))
				{
					return Skeleton;
				}
			}
		}

		UBlueprintGeneratedClass* ActorBlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(Actor->GetClass());
		if (ActorBlueprintGeneratedClass)
		{
			const TArray<USCS_Node*>& ActorBlueprintNodes = ActorBlueprintGeneratedClass->SimpleConstructionScript->GetAllNodes();

			for (USCS_Node* Node : ActorBlueprintNodes)
			{
				if (Node->ComponentClass->IsChildOf(USkeletalMeshComponent::StaticClass()))
				{
					if (USkeleton* Skeleton = GetSkeletonFromComponent(Node->GetActualComponentTemplate(ActorBlueprintGeneratedClass)))
					{
						return Skeleton;
					}
				}
			}
		}
	}
	else if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(BoundObject))
	{
		if (USkeleton* Skeleton = GetSkeletonFromComponent(SkeletalMeshComponent))
		{
			return Skeleton;
		}
	}

	return nullptr;
}


class FGameActionAnimationParamsDetailCustomization : public IPropertyTypeCustomization
{
public:
	FGameActionAnimationParamsDetailCustomization(const FSequencerSectionPropertyDetailsViewCustomizationParams& InParams)
		: Params(InParams)
	{
	}

	// IDetailCustomization interface
	void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}

	void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		const FName AnimationPropertyName = GET_MEMBER_NAME_CHECKED(FGameActionAnimationParams, Animation);

		uint32 NumChildren;
		PropertyHandle->GetNumChildren(NumChildren);
		for (uint32 i = 0; i < NumChildren; ++i)
		{
			TSharedPtr<IPropertyHandle> ChildPropertyHandle = PropertyHandle->GetChildHandle(i);
			IDetailPropertyRow& ChildPropertyRow = ChildBuilder.AddProperty(ChildPropertyHandle.ToSharedRef());

			if (ChildPropertyHandle->GetProperty()->GetFName() == AnimationPropertyName)
			{
				FDetailWidgetRow& Row = ChildPropertyRow.CustomWidget();

				if (Params.ParentObjectBindingGuid.IsValid())
				{
					USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(Params.ParentObjectBindingGuid, Params.Sequencer);
					SkeletonName = FAssetData(Skeleton).GetExportTextName();

					TSharedPtr<IPropertyUtilities> PropertyUtilities = CustomizationUtils.GetPropertyUtilities();

					TSharedRef<SObjectPropertyEntryBox> ContentWidget = SNew(SObjectPropertyEntryBox)
						.PropertyHandle(ChildPropertyHandle)
						.AllowedClass(UAnimSequenceBase::StaticClass())
						.DisplayThumbnail(true)
						.ThumbnailPool(PropertyUtilities.IsValid() ? PropertyUtilities->GetThumbnailPool() : nullptr)
						.CustomResetToDefault(FResetToDefaultOverride::Hide())
						.OnShouldFilterAsset(FOnShouldFilterAsset::CreateRaw(this, &FGameActionAnimationParamsDetailCustomization::ShouldFilterAsset));

					Row.NameContent()[ChildPropertyHandle->CreatePropertyNameWidget()];
					Row.ValueContent()[ContentWidget];

					float MinDesiredWidth, MaxDesiredWidth;
					ContentWidget->GetDesiredWidth(MinDesiredWidth, MaxDesiredWidth);
					Row.ValueContent().MinWidth = MinDesiredWidth;
					Row.ValueContent().MaxWidth = MaxDesiredWidth;

					ChildPropertyHandle->SetInstanceMetaData(TEXT("NoResetToDefault"), TEXT("true"));

					ChildPropertyHandle->SetOnPropertyValuePreChange(FSimpleDelegate::CreateLambda([=]
					{
						TArray<UObject*> Outers;
						PropertyHandle->GetOuterObjects(Outers);
						if (Outers.Num() == 1 && Outers[0] != nullptr)
						{
							UObject* Outer = Outers[0];

							UAnimSequenceBase* Animation;
							TSharedPtr<IPropertyHandle> AnimationHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGameActionAnimationParams, Animation));
							AnimationHandle->GetValue(reinterpret_cast<UObject*&>(Animation));
							PreAnimation = Animation;
						}
					}));
					ChildPropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=]
					{
						TArray<UObject*> Outers;
						PropertyHandle->GetOuterObjects(Outers);
						if (Outers.Num() == 1 && Outers[0] != nullptr)
						{
							UObject* Outer = Outers[0];

							TSharedPtr<IPropertyHandle> MontageHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGameActionAnimationParams, Montage));
							UAnimMontage* Montage = nullptr;
							MontageHandle->GetValue(reinterpret_cast<UObject*&>(Montage));
							
							UAnimSequenceBase* Animation;
							TSharedPtr<IPropertyHandle> AnimationHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGameActionAnimationParams, Animation));
							AnimationHandle->GetValue(reinterpret_cast<UObject*&>(Animation));

							FGameActionAnimationParams* AnimationParams = PropertyHandle->GetProperty()->ContainerPtrToValuePtr<FGameActionAnimationParams>(Outer);
							check(AnimationParams);

							if (Montage && Montage->GetOuter() == Outer)
							{
								if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("覆盖内部蒙太奇资源警告", "该操作会导致内部的蒙太奇资源被覆盖，旧的蒙太奇资源会丢失，继续？")) != EAppReturnType::Yes)
								{
									AnimationParams->Animation = PreAnimation.Get();
									return;
								}
							}
							
							UAnimMontage* NewMontage = Cast<UAnimMontage>(Animation);
							if (Animation && NewMontage == nullptr)
							{
								NewMontage = UGameActionAnimationTrack::CreateMontage(Outer, Animation);
							}
							AnimationParams->Montage = NewMontage;
						}
						PreAnimation.Reset();
					}));
				}
			}
		}
	}

	bool ShouldFilterAsset(const FAssetData& AssetData) const
	{
		// Since the `SObjectPropertyEntryBox` doesn't support passing some `Filter` properties for the asset picker, 
		// we just combine the tag value filtering we want (i.e. checking the skeleton compatibility) along with the
		// other filtering we already get from the track editor's filter callback.
		FGameActionAnimationTrackEditor& TrackEditor = static_cast<FGameActionAnimationTrackEditor&>(Params.TrackEditor);
		if (TrackEditor.ShouldFilterAsset(AssetData))
		{
			return true;
		}

		if (!SkeletonName.IsEmpty())
		{
			const FString& SkeletonTag = AssetData.GetTagValueRef<FString>(TEXT("Skeleton"));
			if (SkeletonTag != SkeletonName)
			{
				return true;
			}
		}

		return false;
	}

private:
	FSequencerSectionPropertyDetailsViewCustomizationParams Params;
	FString SkeletonName;
	TWeakObjectPtr<UAnimSequenceBase> PreAnimation;
};


class FGameActionAnimationSection: public ISequencerSection, public TSharedFromThis<FGameActionAnimationSection>
{
public:
	FGameActionAnimationSection(UMovieSceneSection& InSection, TWeakPtr<ISequencer> InSequencer);

	virtual ~FGameActionAnimationSection() { }
public:
	// ISequencerSection interface

	UMovieSceneSection* GetSectionObject() override;
	FText GetSectionTitle() const override;
	float GetSectionHeight() const override;
	FMargin GetContentPadding() const override;
	int32 OnPaintSection(FSequencerSectionPainter& Painter) const override;
	void BeginResizeSection() override;
	void ResizeSection(ESequencerSectionResizeMode ResizeMode, FFrameNumber ResizeTime) override;
	void BeginSlipSection() override;
	void SlipSection(FFrameNumber SlipTime) override;
	void CustomizePropertiesDetailsView(TSharedRef<IDetailsView> DetailsView, const FSequencerSectionPropertyDetailsViewCustomizationParams& InParams) const override;
private:

	/** The section we are visualizing */
	UGameActionAnimationSection& Section;

	/** Used to draw animation frame, need selection state and local time*/
	TWeakPtr<ISequencer> Sequencer;

	/** Cached first loop start offset value valid only during resize */
	FFrameNumber InitialFirstLoopStartOffsetDuringResize;

	/** Cached start time valid only during resize */
	FFrameNumber InitialStartTimeDuringResize;
};


FGameActionAnimationSection::FGameActionAnimationSection(UMovieSceneSection& InSection, TWeakPtr<ISequencer> InSequencer)
	: Section(*CastChecked<UGameActionAnimationSection>(&InSection))
	, Sequencer(InSequencer)
	, InitialFirstLoopStartOffsetDuringResize(0)
	, InitialStartTimeDuringResize(0)
{ }


UMovieSceneSection* FGameActionAnimationSection::GetSectionObject()
{
	return &Section;
}


FText FGameActionAnimationSection::GetSectionTitle() const
{
	if (Section.Params.Animation != nullptr)
	{
		return FText::FromString(Section.Params.Animation->GetName());
	}
	return LOCTEXT("NoAnimationSection", "No Animation");
}


float FGameActionAnimationSection::GetSectionHeight() const
{
	return 20.f;
}


FMargin FGameActionAnimationSection::GetContentPadding() const
{
	return FMargin(8.0f, 8.0f);
}


int32 FGameActionAnimationSection::OnPaintSection(FSequencerSectionPainter& Painter) const
{
	const ESlateDrawEffect DrawEffects = Painter.bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	const FTimeToPixel& TimeToPixelConverter = Painter.GetTimeConverter();

	int32 LayerId = Painter.PaintSectionBackground();

	static const FSlateBrush* GenericDivider = FEditorStyle::GetBrush("Sequencer.GenericDivider");

	if (!Section.HasStartFrame() || !Section.HasEndFrame())
	{
		return LayerId;
	}

	FFrameRate TickResolution = TimeToPixelConverter.GetTickResolution();

	// Add lines where the animation starts and ends/loops
	const float AnimPlayRate = FMath::IsNearlyZero(Section.Params.PlayRate) || Section.Params.Animation == nullptr ? 1.0f : Section.Params.PlayRate * Section.Params.Animation->RateScale;
	const float SeqLength = (Section.Params.GetSequenceLength() - TickResolution.AsSeconds(Section.Params.StartFrameOffset + Section.Params.EndFrameOffset)) / AnimPlayRate;
	const float FirstLoopSeqLength = SeqLength - TickResolution.AsSeconds(Section.Params.FirstLoopStartFrameOffset) / AnimPlayRate;

	if (!FMath::IsNearlyZero(SeqLength, KINDA_SMALL_NUMBER) && SeqLength > 0)
	{
		float MaxOffset = Section.GetRange().Size<FFrameTime>() / TickResolution;
		float OffsetTime = FirstLoopSeqLength;
		float StartTime = Section.GetInclusiveStartFrame() / TickResolution;

		while (OffsetTime < MaxOffset)
		{
			float OffsetPixel = TimeToPixelConverter.SecondsToPixel(StartTime + OffsetTime) - TimeToPixelConverter.SecondsToPixel(StartTime);

			FSlateDrawElement::MakeBox(
				Painter.DrawElements,
				LayerId,
				Painter.SectionGeometry.MakeChild(
					FVector2D(2.f, Painter.SectionGeometry.Size.Y - 2.f),
					FSlateLayoutTransform(FVector2D(OffsetPixel, 1.f))
				).ToPaintGeometry(),
				GenericDivider,
				DrawEffects
			);

			OffsetTime += SeqLength;
		}
	}

	TSharedPtr<ISequencer> SequencerPtr = Sequencer.Pin();
	if (Painter.bIsSelected && SequencerPtr.IsValid())
	{
		FFrameTime CurrentTime = SequencerPtr->GetLocalTime().Time;
		if (Section.GetRange().Contains(CurrentTime.FrameNumber) && Section.Params.Animation != nullptr)
		{
			// Draw the current time next to the scrub handle
			const float AnimTime = Section.MapTimeToAnimation(CurrentTime, TickResolution);
			const int32 FrameNumber = Section.Params.Animation->GetFrameAtTime(AnimTime);

			// DrawFrameNumberHint无法Link，直接把实现抄过来

			const FString FrameString = FString::FromInt(FrameNumber);

			const FSlateFontInfo SmallLayoutFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
			const TSharedRef< FSlateFontMeasure > FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			const FVector2D TextSize = FontMeasureService->Measure(FrameString, SmallLayoutFont);

			const float PixelX = Painter.GetTimeConverter().FrameToPixel(CurrentTime);

			// Flip the text position if getting near the end of the view range
			static const float TextOffsetPx = 10.f;
			const bool bDrawLeft = (Painter.SectionGeometry.Size.X - PixelX) < (TextSize.X + 22.f) - TextOffsetPx;
			const float TextPosition = bDrawLeft ? PixelX - TextSize.X - TextOffsetPx : PixelX + TextOffsetPx;
			//handle mirrored labels
			const float MajorTickHeight = 9.0f;
			const FVector2D TextOffset(TextPosition, Painter.SectionGeometry.Size.Y - (MajorTickHeight + TextSize.Y));

			const FLinearColor DrawColor = FEditorStyle::GetSlateColor("SelectionColor").GetColor(FWidgetStyle());
			const FVector2D BoxPadding = FVector2D(4.0f, 2.0f);
			// draw time string

			FSlateDrawElement::MakeBox(
				Painter.DrawElements,
				Painter.LayerId + 5,
				Painter.SectionGeometry.ToPaintGeometry(TextOffset - BoxPadding, TextSize + 2.0f * BoxPadding),
				FEditorStyle::GetBrush("WhiteBrush"),
				ESlateDrawEffect::None,
				FLinearColor::Black.CopyWithNewOpacity(0.5f)
			);

			FSlateDrawElement::MakeText(
				Painter.DrawElements,
				Painter.LayerId + 6,
				Painter.SectionGeometry.ToPaintGeometry(TextOffset, TextSize),
				FrameString,
				SmallLayoutFont,
				DrawEffects,
				DrawColor
			);
		}
	}

	return LayerId;
}

void FGameActionAnimationSection::BeginResizeSection()
{
	InitialFirstLoopStartOffsetDuringResize = Section.Params.FirstLoopStartFrameOffset;
	InitialStartTimeDuringResize = Section.HasStartFrame() ? Section.GetInclusiveStartFrame() : 0;
}

void FGameActionAnimationSection::ResizeSection(ESequencerSectionResizeMode ResizeMode, FFrameNumber ResizeTime)
{
	// Adjust the start offset when resizing from the beginning
	if (ResizeMode == SSRM_LeadingEdge)
	{
		FFrameRate FrameRate = Section.GetTypedOuter<UMovieScene>()->GetTickResolution();
		FFrameNumber StartOffset = FrameRate.AsFrameNumber((ResizeTime - InitialStartTimeDuringResize) / FrameRate * Section.Params.PlayRate);

		StartOffset += InitialFirstLoopStartOffsetDuringResize;

		if (StartOffset < 0)
		{
			FFrameTime FrameTimeOver = FFrameTime::FromDecimal(StartOffset.Value / Section.Params.PlayRate);

			// Ensure start offset is not less than 0 and adjust ResizeTime
			ResizeTime = ResizeTime - FrameTimeOver.GetFrame();

			StartOffset = FFrameNumber(0);
		}
		else
		{
			// If the start offset exceeds the length of one loop, trim it back.
			const FFrameNumber SeqLength = FrameRate.AsFrameNumber(Section.Params.GetSequenceLength()) - Section.Params.StartFrameOffset - Section.Params.EndFrameOffset;
			StartOffset = StartOffset % SeqLength;
		}

		Section.Params.FirstLoopStartFrameOffset = StartOffset;
	}

	ISequencerSection::ResizeSection(ResizeMode, ResizeTime);
}

void FGameActionAnimationSection::BeginSlipSection()
{
	BeginResizeSection();
}

void FGameActionAnimationSection::SlipSection(FFrameNumber SlipTime)
{
	FFrameRate FrameRate = Section.GetTypedOuter<UMovieScene>()->GetTickResolution();
	FFrameNumber StartOffset = FrameRate.AsFrameNumber((SlipTime - InitialStartTimeDuringResize) / FrameRate * Section.Params.PlayRate);

	StartOffset += InitialFirstLoopStartOffsetDuringResize;

	if (StartOffset < 0)
	{
		// Ensure start offset is not less than 0 and adjust ResizeTime
		SlipTime = SlipTime - StartOffset;

		StartOffset = FFrameNumber(0);
	}
	else
	{
		// If the start offset exceeds the length of one loop, trim it back.
		const FFrameNumber SeqLength = FrameRate.AsFrameNumber(Section.Params.GetSequenceLength()) - Section.Params.StartFrameOffset - Section.Params.EndFrameOffset;
		StartOffset = StartOffset % SeqLength;
	}

	Section.Params.FirstLoopStartFrameOffset = StartOffset;

	ISequencerSection::SlipSection(SlipTime);
}

void FGameActionAnimationSection::CustomizePropertiesDetailsView(TSharedRef<IDetailsView> DetailsView, const FSequencerSectionPropertyDetailsViewCustomizationParams& InParams) const
{
	DetailsView->RegisterInstancedCustomPropertyTypeLayout(
		TEXT("GameActionAnimationParams"),
		FOnGetPropertyTypeCustomizationInstance::CreateLambda([=]() { return MakeShared<FGameActionAnimationParamsDetailCustomization>(InParams); }));
}

FGameActionAnimationTrackEditor::FGameActionAnimationTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{ }


TSharedRef<ISequencerTrackEditor> FGameActionAnimationTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShareable(new FGameActionAnimationTrackEditor(InSequencer));
}

bool FGameActionAnimationTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UGameActionAnimationTrack::StaticClass();
}

TSharedRef<ISequencerSection> FGameActionAnimationTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	check(SupportsType(SectionObject.GetOuter()->GetClass()));

	return MakeShareable(new FGameActionAnimationSection(SectionObject, GetSequencer()));
}

bool FGameActionAnimationTrackEditor::HandleAssetAdded(UObject* Asset, const FGuid& TargetObjectGuid)
{
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();

	if (Asset->IsA<UAnimSequenceBase>() && SequencerPtr.IsValid())
	{
		UAnimSequenceBase* AnimSequence = Cast<UAnimSequenceBase>(Asset);

		if (TargetObjectGuid.IsValid() && AnimSequence->CanBeUsedInComposition())
		{
			USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(TargetObjectGuid, GetSequencer());

			if (Skeleton && Skeleton == AnimSequence->GetSkeleton())
			{
				UObject* Object = SequencerPtr->FindSpawnedObjectOrTemplate(TargetObjectGuid);

				UMovieSceneTrack* Track = nullptr;

				const FScopedTransaction Transaction(LOCTEXT("AddAnimation_Transaction", "Add Animation"));

				int32 RowIndex = INDEX_NONE;
				AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FGameActionAnimationTrackEditor::AddKeyInternal, Object, AnimSequence, Track, RowIndex));

				return true;
			}
		}
	}
	return false;
}

void FGameActionAnimationTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	UMovieSceneSequence* RootMovieSceneSequence = GetSequencer()->GetRootMovieSceneSequence();
	if (RootMovieSceneSequence == nullptr || RootMovieSceneSequence->IsA<UGameActionSequence>() == false)
	{
		return;
	}
	
	if (ObjectClass->IsChildOf(USkeletalMeshComponent::StaticClass()) || ObjectClass->IsChildOf(AActor::StaticClass()) || ObjectClass->IsChildOf(UChildActorComponent::StaticClass()))
	{
		const TSharedPtr<ISequencer> ParentSequencer = GetSequencer();

		USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(ObjectBindings[0], GetSequencer());

		if (Skeleton)
		{
			// Load the asset registry module
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

			// Collect a full list of assets with the specified class
			TArray<FAssetData> AssetDataList;
			AssetRegistryModule.Get().GetAssetsByClass(UAnimSequenceBase::StaticClass()->GetFName(), AssetDataList, true);

			if (AssetDataList.Num())
			{
				UMovieSceneTrack* Track = nullptr;

				MenuBuilder.AddSubMenu(
					LOCTEXT("GameActionAnimation", "游戏行为动画"), NSLOCTEXT("Sequencer", "AddAnimationTooltip", "Adds an animation track."),
					FNewMenuDelegate::CreateRaw(this, &FGameActionAnimationTrackEditor::AddAnimationSubMenu, ObjectBindings, Skeleton, Track)
				);
			}
		}
	}
}

TSharedRef<SWidget> FGameActionAnimationTrackEditor::BuildAnimationSubMenu(FGuid ObjectBinding, USkeleton* Skeleton, UMovieSceneTrack* Track)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	TArray<FGuid> ObjectBindings;
	ObjectBindings.Add(ObjectBinding);

	AddAnimationSubMenu(MenuBuilder, ObjectBindings, Skeleton, Track);

	return MenuBuilder.MakeWidget();
}

bool FGameActionAnimationTrackEditor::ShouldFilterAsset(const FAssetData& AssetData)
{
	const FString EnumString = AssetData.GetTagValueRef<FString>(GET_MEMBER_NAME_CHECKED(UAnimSequence, AdditiveAnimType));
	if (EnumString.IsEmpty())
	{
		return false;
	}

	UEnum* AdditiveTypeEnum = StaticEnum<EAdditiveAnimationType>();
	return ((EAdditiveAnimationType)AdditiveTypeEnum->GetValueByName(*EnumString) == AAT_RotationOffsetMeshSpace);
}

void FGameActionAnimationTrackEditor::AddAnimationSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings, USkeleton* Skeleton, UMovieSceneTrack* Track)
{
	FAssetPickerConfig AssetPickerConfig;
	{
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateRaw(this, &FGameActionAnimationTrackEditor::OnAnimationAssetSelected, ObjectBindings, Track);
		AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateRaw(this, &FGameActionAnimationTrackEditor::OnAnimationAssetEnterPressed, ObjectBindings, Track);
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
		AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateRaw(this, &FGameActionAnimationTrackEditor::ShouldFilterAsset);
		AssetPickerConfig.Filter.bRecursiveClasses = true;
		AssetPickerConfig.Filter.ClassNames.Add(UAnimSequenceBase::StaticClass()->GetFName());
		AssetPickerConfig.Filter.TagsAndValues.Add(TEXT("Skeleton"), FAssetData(Skeleton).GetExportTextName());
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TSharedPtr<SBox> MenuEntry = SNew(SBox)
		.WidthOverride(300.0f)
		.HeightOverride(300.f)
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		];

	MenuBuilder.AddWidget(MenuEntry.ToSharedRef(), FText::GetEmpty(), true);
}

void FGameActionAnimationTrackEditor::OnAnimationAssetSelected(const FAssetData& AssetData, TArray<FGuid> ObjectBindings, UMovieSceneTrack* Track)
{
	FSlateApplication::Get().DismissAllMenus();

	UObject* SelectedObject = AssetData.GetAsset();
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();

	if (SelectedObject && SelectedObject->IsA(UAnimSequenceBase::StaticClass()) && SequencerPtr.IsValid())
	{
		UAnimSequenceBase* AnimSequence = CastChecked<UAnimSequenceBase>(AssetData.GetAsset());

		const FScopedTransaction Transaction(LOCTEXT("AddGameActionAnimation_Transaction", "Add Game Action Animation"));

		for (FGuid ObjectBinding : ObjectBindings)
		{
			UObject* Object = SequencerPtr->FindSpawnedObjectOrTemplate(ObjectBinding);
			int32 RowIndex = INDEX_NONE;
			AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FGameActionAnimationTrackEditor::AddKeyInternal, Object, AnimSequence, Track, RowIndex));
		}
	}
}

void FGameActionAnimationTrackEditor::OnAnimationAssetEnterPressed(const TArray<FAssetData>& AssetData, TArray<FGuid> ObjectBindings, UMovieSceneTrack* Track)
{
	if (AssetData.Num() > 0)
	{
		OnAnimationAssetSelected(AssetData[0].GetAsset(), ObjectBindings, Track);
	}
}

FKeyPropertyResult FGameActionAnimationTrackEditor::AddKeyInternal(FFrameNumber KeyTime, UObject* Object, class UAnimSequenceBase* AnimSequence, UMovieSceneTrack* Track, int32 RowIndex)
{
	FKeyPropertyResult KeyPropertyResult;

	FFindOrCreateHandleResult HandleResult = FindOrCreateHandleToObject(Object);
	FGuid ObjectHandle = HandleResult.Handle;
	KeyPropertyResult.bHandleCreated |= HandleResult.bWasCreated;
	if (ObjectHandle.IsValid())
	{
		if (!Track)
		{
			Track = AddTrack(GetSequencer()->GetFocusedMovieSceneSequence()->GetMovieScene(), ObjectHandle, UGameActionAnimationTrack::StaticClass(), NAME_None);
			KeyPropertyResult.bTrackCreated = true;
		}

		if (ensure(Track))
		{
			Track->Modify();

			UMovieSceneSection* NewSection = Cast<UGameActionAnimationTrack>(Track)->AddNewAnimationOnRow(KeyTime, AnimSequence, RowIndex);
			KeyPropertyResult.bTrackModified = true;

			GetSequencer()->EmptySelection();
			GetSequencer()->SelectSection(NewSection);
			GetSequencer()->ThrobSectionSelection();
		}
	}

	return KeyPropertyResult;
}

void FGameActionAnimationTrackEditor::BuildTrackContextMenu(FMenuBuilder& MenuBuilder, UMovieSceneTrack* Track)
{

}

TSharedPtr<SWidget> FGameActionAnimationTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(ObjectBinding, GetSequencer());

	if (Skeleton)
	{
		// Create a container edit box
		return SNew(SHorizontalBox)

			// Add the animation combo box
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				FSequencerUtilities::MakeAddButton(LOCTEXT("AddAnimation", "Animation"), FOnGetContent::CreateSP(this, &FGameActionAnimationTrackEditor::BuildAnimationSubMenu, ObjectBinding, Skeleton, Track), Params.NodeIsHovered, GetSequencer())
			];
	}

	else
	{
		return TSharedPtr<SWidget>();
	}
}

bool FGameActionAnimationTrackEditor::OnAllowDrop(const FDragDropEvent& DragDropEvent, UMovieSceneTrack* Track, int32 RowIndex, const FGuid& TargetObjectGuid)
{
	if (!Track->IsA(UGameActionAnimationTrack::StaticClass()))
	{
		return false;
	}

	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();

	if (!Operation.IsValid() || !Operation->IsOfType<FAssetDragDropOp>())
	{
		return false;
	}

	if (!TargetObjectGuid.IsValid())
	{
		return false;
	}

	USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(TargetObjectGuid, GetSequencer());

	TSharedPtr<FAssetDragDropOp> DragDropOp = StaticCastSharedPtr<FAssetDragDropOp>(Operation);

	for (const FAssetData& AssetData : DragDropOp->GetAssets())
	{
		UAnimSequenceBase* AnimSequence = Cast<UAnimSequenceBase>(AssetData.GetAsset());

		const bool bValidAnimSequence = AnimSequence && AnimSequence->CanBeUsedInComposition();
		if (bValidAnimSequence && Skeleton && Skeleton == AnimSequence->GetSkeleton())
		{
			return true;
		}
	}

	return false;
}

FReply FGameActionAnimationTrackEditor::OnDrop(const FDragDropEvent& DragDropEvent, UMovieSceneTrack* Track, int32 RowIndex, const FGuid& TargetObjectGuid)
{
	if (!Track->IsA(UGameActionAnimationTrack::StaticClass()))
	{
		return FReply::Unhandled();
	}

	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();

	if (!Operation.IsValid() || !Operation->IsOfType<FAssetDragDropOp>())
	{
		return FReply::Unhandled();
	}

	if (!TargetObjectGuid.IsValid())
	{
		return FReply::Unhandled();
	}

	USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(TargetObjectGuid, GetSequencer());

	TSharedPtr<FAssetDragDropOp> DragDropOp = StaticCastSharedPtr<FAssetDragDropOp>(Operation);

	bool bAnyDropped = false;
	for (const FAssetData& AssetData : DragDropOp->GetAssets())
	{
		UAnimSequenceBase* AnimSequence = Cast<UAnimSequenceBase>(AssetData.GetAsset());
		const bool bValidAnimSequence = AnimSequence && AnimSequence->CanBeUsedInComposition();
		if (bValidAnimSequence && Skeleton && Skeleton == AnimSequence->GetSkeleton())
		{
			UObject* Object = GetSequencer()->FindSpawnedObjectOrTemplate(TargetObjectGuid);

			AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FGameActionAnimationTrackEditor::AddKeyInternal, Object, AnimSequence, Track, RowIndex));

			bAnyDropped = true;
		}
	}

	return bAnyDropped ? FReply::Handled() : FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
