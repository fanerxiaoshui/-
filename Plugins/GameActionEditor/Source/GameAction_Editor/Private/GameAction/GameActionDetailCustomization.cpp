// Fill out your copyright notice in the Description page of Project Settings.


#include "GameAction/GameActionDetailCustomization.h"
#include <DetailLayoutBuilder.h>
#include <DetailCategoryBuilder.h>
#include <DetailWidgetRow.h>
#include <Widgets/Text/STextBlock.h>
#include <Framework/MultiBox/MultiBoxBuilder.h>
#include <Widgets/Input/SButton.h>
#include <Widgets/Input/SComboButton.h>
#include <Widgets/Images/SImage.h>
#include <K2Node_FunctionEntry.h>
#include <Kismet2/BlueprintEditorUtils.h>
#include <ScopedTransaction.h>

#include "Blueprint/BPNode_GameActionSegment.h"
#include "Blueprint/BPNode_GameActionTransition.h"
#include "GameAction/GameActionInstance.h"
#include "GameAction/GameActionSegment.h"

#define LOCTEXT_NAMESPACE "GameActionInstanceDetails"

void FGameActionInstanceDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = DetailBuilder.GetSelectedObjects();
	if (SelectedObjects.Num() != 1)
	{
		return;
	}

	Instance = Cast<UGameActionInstanceBase>(SelectedObjects[0]);
	if (Instance.IsValid() && Instance->GetOwner())
	{
		PreActivedSegment = Instance->ActivedSegment;

		TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
		for (TFieldIterator<FStructProperty> It(Instance->GetClass()); It; ++It)
		{
			if (It->Struct->IsChildOf(StaticStruct<FGameActionEntry>()))
			{
				Content->AddSlot()
				[
					SNew(SButton)
					.Text(It->GetDisplayNameText())
					.OnClicked_Lambda([=, EntryProperty = *It]
					{
						if (Instance.IsValid())
						{
							if (FGameActionEntry* Entry = EntryProperty->ContainerPtrToValuePtr<FGameActionEntry>(Instance.Get()))
							{
								Instance->TryStartEntry(*Entry);
							}
						}
						return FReply::Handled();
					})
					.IsEnabled_Lambda([=]
					{
						return Instance.IsValid() && Instance->IsActived() == false;
					})
				];
			}
		}
		Content->AddSlot()
			[
				SNew(SButton)
				.Text(LOCTEXT("中断行为", "中断行为"))
				.OnClicked_Lambda([=]
				{
					Instance->AbortGameAction();
					return FReply::Handled();
				})
				.IsEnabled_Lambda([=]
				{
					return Instance.IsValid() && Instance->IsActived();
				})
			];

		
		IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(TEXT("Runtime Debug"), LOCTEXT("运行时调试", "运行时调试"), ECategoryPriority::Important);
		CategoryBuilder.AddCustomRow(FText::GetEmpty())
		.NameContent()
		[
			SNew(STextBlock).Text(LOCTEXT("控制行为", "控制行为"))
		]
		.ValueContent()
		[
			Content
		];
		
		CategoryBuilder.AddCustomRow(FText::GetEmpty())
		.NameContent()
		.VAlign(EVerticalAlignment::VAlign_Top)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("当前激活节点事件", "当前激活节点事件"))
			.Margin(FMargin(0.f, 4.f))
		]
		.ValueContent()
		[
			SAssignNew(EventList, SVerticalBox)
		];

		Tick(0.f);
	}
}

void FGameActionInstanceDetails::Tick(float DeltaTime)
{
	if (UGameActionInstanceBase* ActionInstance = Instance.Get())
	{
		if (ActionInstance->ActivedSegment != PreActivedSegment.Get())
		{
			UGameActionSegmentBase* ActivedSegment = ActionInstance->ActivedSegment;
			PreActivedSegment = ActionInstance->ActivedSegment;
			EventList->ClearChildren();
			
			if (ActivedSegment == nullptr)
			{
				return;
			}
			for (const FGameActionEventTransition& EventTransition : ActivedSegment->EventTransitions)
			{
				EventList->AddSlot()
				[
					SNew(SButton)
					.Text(FText::FromName(EventTransition.EventName))
					.OnClicked_Lambda([=]
					{
						ActivedSegment->InvokeEventTransition(EventTransition.EventName);
						return FReply::Handled();
					})
					.IsEnabled_Lambda([=]
					{
						return EventTransition.CanTransition(ActivedSegment, false);
					})
				];
			}
		}
	}
}

void FGameActionTransitionNodeBaseDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = DetailBuilder.GetSelectedObjects();
	if (SelectedObjects.Num() != 1)
	{
		return;
	}

	UBPNode_GameActionTransitionBase* TransitionNode = Cast<UBPNode_GameActionTransitionBase>(SelectedObjects[0]);
	if (TransitionNode == nullptr)
	{
		return;
	}

	UK2Node_FunctionEntry* FuncEntry = TransitionNode->BoundGraph->EntryNode;

	IDetailCategoryBuilder& DefaultValueCategory = DetailBuilder.EditCategory(TEXT("TransitionSettings"), LOCTEXT("TransitionSettings", "跳转配置"));

	for (const FBPVariableDescription& VariableDesc : FuncEntry->LocalVariables)
	{
		const FName VarName = VariableDesc.VarName;

		UBlueprint* Blueprint = TransitionNode->GetBlueprint();

		UFunction* StructScope = Blueprint->SkeletonGeneratedClass->FindFunctionByName(FuncEntry->CustomGeneratedFunctionName);
		FProperty* VariableProperty = FindFProperty<FProperty>(StructScope, VarName);

		TSharedPtr<FStructOnScope> StructData = MakeShareable(new FStructOnScope(StructScope));

		for (const FBPVariableDescription& LocalVar : FuncEntry->LocalVariables)
		{
			if (LocalVar.VarName == VariableProperty->GetFName())
			{
				// Only set the default value if there is one
				if (!LocalVar.DefaultValue.IsEmpty())
				{
					FBlueprintEditorUtils::PropertyValueFromString(VariableProperty, LocalVar.DefaultValue, StructData->GetStructMemory());
				}
				break;
			}
		}

		IDetailPropertyRow* Row = DefaultValueCategory.AddExternalStructureProperty(StructData, VariableProperty->GetFName());
		Row->GetPropertyHandle()->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=]
		{
			FString DefaultValueString;
			FBlueprintEditorUtils::PropertyValueToString(VariableProperty, StructData->GetStructMemory(), DefaultValueString, FuncEntry);
			
			for (FBPVariableDescription& LocalVar : FuncEntry->LocalVariables)
			{
				if (LocalVar.VarName == VariableProperty->GetFName())
				{
					const FScopedTransaction Transaction(LOCTEXT("ChangeDefaults", "Change Defaults"));

					FuncEntry->Modify();
					Blueprint->Modify();
					LocalVar.DefaultValue = DefaultValueString;
					FuncEntry->RefreshFunctionVariableCache();
					FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
					break;
				}
			}
		}));
	}
}

void FGameActionTransitionNodeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = DetailBuilder.GetSelectedObjects();
	if (SelectedObjects.Num() != 1)
	{
		return;
	}

	UBPNode_GameActionTransition* TransitionNode = Cast<UBPNode_GameActionTransition>(SelectedObjects[0]);
	if (TransitionNode == nullptr)
	{
		return;
	}

	TSharedRef<IPropertyHandle> EventHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBPNode_GameActionTransition, EventName));

	FMenuBuilder DefaultEventBuilder(true, nullptr, nullptr, true);
	if (UBPNode_GameActionSegmentBase* SegmentNode = TransitionNode->FromNode)
	{
		if (UGameActionSegmentBase* Segment = SegmentNode->GameActionSegment)
		{
			DefaultEventBuilder.BeginSection(TEXT("SegmentDefaultEvents"), LOCTEXT("片段默认事件", "片段默认事件"));
			for (const FGameActionEventEntry& DefaultEvent : Segment->DefaultEvents)
			{
				DefaultEventBuilder.AddMenuEntry(
					FText::FromName(DefaultEvent.EventName),
					DefaultEvent.Tooltip,
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([=]
					{
						EventHandle->SetValue(DefaultEvent.EventName);
					}))
				);
			}
			DefaultEventBuilder.EndSection();
		}

		if (UClass* GameInstanceClass = SegmentNode->GetBlueprint()->GeneratedClass)
		{
			UGameActionInstanceBase* GameActionInstance = GameInstanceClass->GetDefaultObject<UGameActionInstanceBase>();

			DefaultEventBuilder.BeginSection(TEXT("SegmentDefaultEvents"), LOCTEXT("实例默认事件", "实例默认事件"));
			for (const FGameActionEventEntry& DefaultEvent : GameActionInstance->DefaultEvents)
			{
				DefaultEventBuilder.AddMenuEntry(
					FText::FromName(DefaultEvent.EventName),
					DefaultEvent.Tooltip,
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([=]
					{
						EventHandle->SetValue(DefaultEvent.EventName);
					}))
				);
			}
			DefaultEventBuilder.EndSection();
		}
	}

	IDetailPropertyRow& PropertyRow = DetailBuilder.AddPropertyToCategory(EventHandle);
	PropertyRow.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateWeakLambda(TransitionNode, [TransitionNode]
	{
		return TransitionNode->TransitionType == EGameActionTransitionType::Event ? EVisibility::Visible : EVisibility::Collapsed;
	})));
	PropertyRow.CustomWidget()
	.NameContent()
	[
		EventHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			EventHandle->CreatePropertyValueWidget()
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SComboButton)
			.ButtonStyle(FEditorStyle::Get(), TEXT("HoverHintOnly"))
			.ContentPadding(2.0f)
			.ForegroundColor(FSlateColor::UseForeground())
			.HasDownArrow(false)
			.ButtonContent()
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush(TEXT("DetailsView.PulldownArrow.Down")))
			]
			.MenuContent()
			[
				DefaultEventBuilder.MakeWidget()
			]
			.ToolTipText(LOCTEXT("使用默认事件", "使用默认事件"))
		]
	];
}

#undef LOCTEXT_NAMESPACE
