// Fill out your copyright notice in the Description page of Project Settings.


#include "Blueprint/GameActionSegmentNodeDetails.h"
#include <DetailLayoutBuilder.h>
#include <ObjectEditorUtils.h>
#include <DetailWidgetRow.h>
#include <IDetailPropertyRow.h>
#include <DetailLayoutBuilder.h>
#include <DetailCategoryBuilder.h>
#include <IDetailsView.h>
#include <Widgets/SCompoundWidget.h>
#include <Widgets/SBoxPanel.h>
#include <Widgets/Text/STextBlock.h>
#include <Widgets/Input/SCheckBox.h>
#include <Widgets/SBoxPanel.h>
#include <Widgets/SNullWidget.h>

#include "Blueprint/BPNode_GameActionSegment.h"
#include "GameAction/GameActionSegment.h"

#define LOCTEXT_NAMESPACE "DispatchableActionNodeDetails"

class SGameActionSegmentShowPinWidget : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SGameActionSegmentShowPinWidget) {}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<IPropertyHandle>& InPropertyHandle, const FOptionalPinFromProperty& OptionalPin)
	{
		PropertyHandle = InPropertyHandle;

		TSharedRef<SCheckBox> CheckBox = SNew(SCheckBox)
			.ToolTipText(LOCTEXT("AsPinEnableTooltip", "显示/隐藏 这个属性在节点上的引脚"))
			.IsChecked(this, &SGameActionSegmentShowPinWidget::IsChecked)
			.OnCheckStateChanged(this, &SGameActionSegmentShowPinWidget::OnCheckStateChanged);

		ChildSlot
			[
				CheckBox
			];
	}

	ECheckBoxState IsChecked() const
	{
		bool bValue;
		FPropertyAccess::Result Result = PropertyHandle->GetValue(bValue);
		if (Result == FPropertyAccess::MultipleValues)
		{
			return ECheckBoxState::Undetermined;
		}
		else
		{
			return bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}

		return ECheckBoxState::Unchecked;
	}

	void OnCheckStateChanged(ECheckBoxState InCheckBoxState)
	{
		bool bValue = InCheckBoxState == ECheckBoxState::Checked;
		PropertyHandle->SetValue(bValue);
	}

	TSharedPtr<IPropertyHandle> PropertyHandle;
};

void FGameActionSegmentNodeDetails::CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> SelectedObjectsList;
	DetailBuilder.GetObjectsBeingCustomized(SelectedObjectsList);

	// Hide the pin options property; it's represented inline per-property instead
	IDetailCategoryBuilder& PinOptionsCategory = DetailBuilder.EditCategory("PinOptions");
	TSharedRef<IPropertyHandle> AvailablePins = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBPNode_GameActionSegmentBase, ShowPinForProperties), UBPNode_GameActionSegmentBase::StaticClass());
	DetailBuilder.HideProperty(AvailablePins);

	// get first dispatchable action nodes
	UBPNode_GameActionSegmentBase* DispatchableActionBPNode = Cast<UBPNode_GameActionSegmentBase>(SelectedObjectsList[0].Get());
	if (DispatchableActionBPNode == nullptr)
	{
		return;
	}

	// make sure type matches with all the nodes. 
	const UBPNode_GameActionSegmentBase* FirstNodeType = DispatchableActionBPNode;
	for (int32 Index = 1; Index < SelectedObjectsList.Num(); ++Index)
	{
		UBPNode_GameActionSegmentBase* CurrentNode = Cast<UBPNode_GameActionSegmentBase>(SelectedObjectsList[Index].Get());
		if (!CurrentNode || CurrentNode->GetClass() != FirstNodeType->GetClass())
		{
			// if type mismatches, multi selection doesn't work, just return
			return;
		}
	}

	// Get the node property
	const UGameActionSegmentBase* ActionTemplate = DispatchableActionBPNode->GameActionSegment;
	if (ActionTemplate == nullptr)
	{
		return;
	}

	auto BuildPinOptionalDetails = [&](UClass* Class, TSubclassOf<UBPNode_GameActionSegmentBase> NodeClass, const TArray<FOptionalPinFromProperty>& ShowPinOptions, const FString& ShowPinForPropertiesPropertyName)
	{
		for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FProperty* InstanceProperty = *It;

			if (InstanceProperty->HasAnyPropertyFlags(CPF_DisableEditOnInstance) || InstanceProperty->HasAllPropertyFlags(CPF_Edit) == false)
			{
				DetailBuilder.HideProperty(InstanceProperty->GetFName(), InstanceProperty->GetOwnerClass());
				continue;
			}

			const FName PropertyName = InstanceProperty->GetFName();
			int32 CustomPinIndex = ShowPinOptions.IndexOfByPredicate([&](const FOptionalPinFromProperty& InOptionalPin)
			{
				return PropertyName == InOptionalPin.PropertyName;
			});
			if (CustomPinIndex == INDEX_NONE)
			{
				continue;
			}

			TSharedRef<IPropertyHandle> TargetPropertyHandle = DetailBuilder.GetProperty(InstanceProperty->GetFName(), InstanceProperty->GetOwnerClass());
			FProperty* TargetProperty = TargetPropertyHandle->GetProperty();
			if (InstanceProperty != TargetProperty)
			{
				continue;
			}

			IDetailCategoryBuilder& CurrentCategory = DetailBuilder.EditCategory(FObjectEditorUtils::GetCategoryFName(TargetProperty));
			const FOptionalPinFromProperty& OptionalPin = ShowPinOptions[CustomPinIndex];

			if (OptionalPin.bCanToggleVisibility)
			{
				if (!TargetPropertyHandle->GetProperty())
				{
					continue;
				}

				// if customized, do not do anything
				if (TargetPropertyHandle->IsCustomized())
				{
					continue;
				}

				// sometimes because of order of customization
				// this gets called first for the node you'd like to customize
				// then the above statement won't work
				// so you can mark certain property to have meta data "CustomizeProperty"
				// which will trigger below statement
				if (OptionalPin.bPropertyIsCustomized)
				{
					continue;
				}

				IDetailPropertyRow& PropertyRow = CurrentCategory.AddProperty(TargetPropertyHandle, TargetProperty->HasAnyPropertyFlags(CPF_AdvancedDisplay) ? EPropertyLocation::Advanced : EPropertyLocation::Default);

				TSharedPtr<SWidget> NameWidget;
				TSharedPtr<SWidget> ValueWidget;
				FDetailWidgetRow Row;
				TargetProperty->SetMetaData(TEXT("NoResetToDefault"), TEXT("True"));
				PropertyRow.GetDefaultWidgets(NameWidget, ValueWidget, Row);
				TargetProperty->RemoveMetaData(TEXT("NoResetToDefault"));
				if (TargetProperty->HasMetaData(TEXT("EditCondition")))
				{
					NameWidget = TargetPropertyHandle->CreatePropertyNameWidget();
				}

				const FName OptionalPinArrayEntryName(*FString::Printf(TEXT("%s[%d].bShowPin"), *ShowPinForPropertiesPropertyName, CustomPinIndex));
				TSharedRef<IPropertyHandle> ShowHidePropertyHandle = DetailBuilder.GetProperty(OptionalPinArrayEntryName, NodeClass);

				ShowHidePropertyHandle->MarkHiddenByCustomization();

				auto GetVisibilityOfProperty = [ShowHidePropertyHandle]
				{
					bool bShowAsPin;
					if (FPropertyAccess::Success == ShowHidePropertyHandle->GetValue(/*out*/ bShowAsPin))
					{
						return bShowAsPin ? EVisibility::Hidden : EVisibility::Visible;
					}
					else
					{
						return EVisibility::Visible;
					}
				};
				ValueWidget->SetVisibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda(GetVisibilityOfProperty)));

				// we only show children if visibility is one
				// whenever toggles, this gets called, so it will be refreshed
				const bool bShowChildren = GetVisibilityOfProperty() == EVisibility::Visible;
				PropertyRow.CustomWidget(bShowChildren)
					.NameContent()
					.MinDesiredWidth(Row.NameWidget.MinWidth)
					.MaxDesiredWidth(Row.NameWidget.MaxWidth)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SGameActionSegmentShowPinWidget, ShowHidePropertyHandle, OptionalPin)
						]
						+ SHorizontalBox::Slot()
						.Padding(4.f, 0.f)
						.AutoWidth()
						[
							NameWidget.ToSharedRef()
						]
					]
				.ValueContent()
					.MinDesiredWidth(Row.ValueWidget.MinWidth)
					.MaxDesiredWidth(Row.ValueWidget.MaxWidth)
					[
						OptionalPin.bIsOverrideEnabled ? ValueWidget.ToSharedRef() : SNullWidget::NullWidget
					];
			}
			else
			{
				&CurrentCategory.AddProperty(TargetPropertyHandle);
			}
		}
	};
	BuildPinOptionalDetails(ActionTemplate->GetClass(), UBPNode_GameActionSegmentBase::StaticClass(), DispatchableActionBPNode->ShowPinForProperties, GET_MEMBER_NAME_STRING_CHECKED(UBPNode_GameActionSegmentBase, ShowPinForProperties));
}

#undef LOCTEXT_NAMESPACE
