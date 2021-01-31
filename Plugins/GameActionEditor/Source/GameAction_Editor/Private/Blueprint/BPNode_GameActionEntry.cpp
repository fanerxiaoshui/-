// Fill out your copyright notice in the Description page of Project Settings.


#include "Blueprint/BPNode_GameActionEntry.h"
#include <EdGraphSchema_K2.h>
#include <KismetNodes/SGraphNodeK2Default.h>
#include <SGraphPin.h>
#include <BlueprintActionDatabaseRegistrar.h>
#include <BlueprintNodeSpawner.h>
#include <Widgets/SBoxPanel.h>

#include "Blueprint/BPNode_GameActionSegment.h"
#include "Blueprint/EdGraph_GameAction.h"
#include "GameAction/GameActionInstance.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "BPNode_GameActionEntry"

UBPNode_GameActionEntry::UBPNode_GameActionEntry()
{
	bInternalEvent = true;
	bIsEditable = false;
}

FText UBPNode_GameActionEntry::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::MenuTitle)
	{
		if (FStructProperty* Property = EntryTransitionProperty.Get())
		{
			return FText::Format(LOCTEXT("Add Entry", "Add Entry {0}"), Property->GetDisplayNameText());
		}
		else
		{
			return LOCTEXT("Add All Miss Entry", "Add Entry [添加缺失的入口]");
		}
	}
	if (TitleType == ENodeTitleType::FullTitle)
	{
		if (FStructProperty* Property = EntryTransitionProperty.Get())
		{
			return FText::Format(LOCTEXT("游戏行为入口节点标题", "{0}\n游戏行为入口"), Property->GetDisplayNameText());
		}
		else
		{
			return LOCTEXT("失效的行为入口", "失效的行为入口");
		}
	}
	return Super::GetNodeTitle(TitleType);
}

FText UBPNode_GameActionEntry::GetTooltipText() const
{
	if (FStructProperty* Property = EntryTransitionProperty.Get())
	{
		return Property->GetToolTipText();
	}
	return Super::GetTooltipText();
}

FText UBPNode_GameActionEntry::GetMenuCategory() const
{
	return LOCTEXT("Game Action", "Game Action");
}

void UBPNode_GameActionEntry::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	UEdGraphPin* Then = FindPinChecked(UEdGraphSchema_K2::PN_Then);
	Then->PinType.PinSubCategory = GameActionNodeUtils::GameActionExecPinCategory;
	if (FStructProperty* Property = EntryTransitionProperty.Get())
	{
		Then->PinFriendlyName = Property->GetDisplayNameText();
	}
	else
	{
		Then->PinFriendlyName = LOCTEXT("失效的行为入口", "失效的行为入口");
	}
}

void UBPNode_GameActionEntry::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	ExpandSplitPins(CompilerContext, SourceGraph);

	BreakAllNodeLinks();
	SetEnabledState(ENodeEnabledState::Disabled, false);
}

TSharedPtr<SGraphNode> UBPNode_GameActionEntry::CreateVisualWidget()
{
	class SGameActionEntryNode : public SGraphNodeK2Default
	{
		using Super = SGraphNodeK2Default;
	public:
		void Construct(const FArguments& InArgs, UBPNode_GameActionEntry* InNode)
		{
			Super::Construct(InArgs, InNode);
		}
		void UpdateGraphNode() override
		{
			InputPins.Empty();
			OutputPins.Empty();
	
			// Reset variables that are going to be exposed, in case we are refreshing an already setup node.
			RightNodeBox.Reset();
			LeftNodeBox.Reset();

			FLinearColor TitleShadowColor(0.6f, 0.6f, 0.6f);

			this->ContentScale.Bind( this, &SGraphNode::GetContentScale );
			this->GetOrAddSlot( ENodeZone::Center )
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SBorder)
					.BorderImage( FEditorStyle::GetBrush( "Graph.StateNode.Body" ) )
					.Padding(0)
					.BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.08f))
					[
						SNew(SOverlay)

						// PIN AREA
						+SOverlay::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Fill)
						.Padding(10.0f)
						[
							SAssignNew(RightNodeBox, SVerticalBox)
						]
					]
				];

			CreatePinWidgets();
		}
		void CreatePinWidgets() override
		{
			for (UEdGraphPin* CurPin : GraphNode->Pins)
			{
				if (CurPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					CreateStandardPinWidget(CurPin);
				}
			}
		}
		void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override
		{
			PinToAdd->SetOwner(SharedThis(this));
			RightNodeBox->AddSlot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.FillHeight(1.0f)
				[
					PinToAdd
				];
			OutputPins.Add(PinToAdd);
		}
	};
	return SNew(SGameActionEntryNode, this);
}

void UBPNode_GameActionEntry::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		{
			TArray<UClass*> DerivedClasses;
			GetDerivedClasses(UGameActionInstanceBase::StaticClass(), DerivedClasses);
			DerivedClasses.Add(UGameActionInstanceBase::StaticClass());
			TSet<FStructProperty*> GameActionEntryProperties;
			for (UClass* DerivedClass : DerivedClasses)
			{
				if (DerivedClass == nullptr || DerivedClass->HasAnyClassFlags(CLASS_Deprecated))
				{
					continue;
				}

				for (TFieldIterator<FStructProperty> It(DerivedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
				{
					if (It->Struct->IsChildOf(FGameActionEntry::StaticStruct()))
					{
						GameActionEntryProperties.Add(*It);
					}
				}
			}

			for (FStructProperty* EntryProperty : GameActionEntryProperties)
			{
				UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
				check(NodeSpawner != nullptr);

				NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda([=](UEdGraphNode* NewNode, bool bIsTemplateNode) mutable
				{
					UBPNode_GameActionEntry* GameActionEntryNode = CastChecked<UBPNode_GameActionEntry>(NewNode);
					GameActionEntryNode->CustomFunctionName = *FString::Printf(TEXT("__%s_ActionEntry"), *EntryProperty->GetDisplayNameText().ToString());
					GameActionEntryNode->EntryTransitionProperty = EntryProperty;
				});
				ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
			}
		}

		{
			UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
			check(NodeSpawner != nullptr);
			NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda([=](UEdGraphNode* NewNode, bool bIsTemplateNode) mutable
			{
				UBPNode_GameActionEntry* GameActionEntry = CastChecked<UBPNode_GameActionEntry>(NewNode);

				if (bIsTemplateNode == false)
				{
					UEdGraph_GameAction* GameActionGraph = Cast<UEdGraph_GameAction>(NewNode->GetGraph());
					check(GameActionGraph);
					TArray<FStructProperty*> NoEntryNodeProperties = GameActionGraph->GetNoEntryNodeProperties();
					check(NoEntryNodeProperties.Num() > 0);
					GameActionEntry->EntryTransitionProperty = NoEntryNodeProperties[0];

					for (int32 Idx = 1, NextNodePosY = GameActionEntry->NodePosY + 200; Idx < NoEntryNodeProperties.Num(); ++Idx, NextNodePosY += 200)
					{
						FGraphNodeCreator<UBPNode_GameActionEntry> GameActionEntryCreator(*GameActionGraph);
						UBPNode_GameActionEntry* GameActionNodeEntry = GameActionEntryCreator.CreateNode();
						GameActionNodeEntry->CustomFunctionName = *FString::Printf(TEXT("__%s_ActionEntry"), *NoEntryNodeProperties[Idx]->GetDisplayNameText().ToString());
						GameActionNodeEntry->NodePosX = GameActionEntry->NodePosX;
						GameActionNodeEntry->NodePosY = NextNodePosY;
						GameActionNodeEntry->EntryTransitionProperty = NoEntryNodeProperties[Idx];
						GameActionEntryCreator.Finalize();
					}
				}
			});
			ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
		}
	}
}

bool UBPNode_GameActionEntry::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
	if (EntryTransitionProperty.Get() != nullptr)
	{
		return true;
	}
	else
	{
		if (const UEdGraph_GameAction* GameActionGraph = Cast<UEdGraph_GameAction>(TargetGraph))
		{
			return GameActionGraph->GetNoEntryNodeProperties().Num() != 0;
		}
	}
	return false;
}

bool UBPNode_GameActionEntry::IsActionFilteredOut(FBlueprintActionFilter const& Filter)
{
	if (FStructProperty* EntryProperty = EntryTransitionProperty.Get())
	{
		for (UBlueprint* Blueprint : Filter.Context.Blueprints)
		{
			if (Blueprint->SkeletonGeneratedClass == nullptr || Blueprint->SkeletonGeneratedClass->HasProperty(EntryProperty) == false)
			{
				return true;
			}
		}
		for (UEdGraph* Graph : Filter.Context.Graphs)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UBPNode_GameActionEntry* ExistEventNode = Cast<UBPNode_GameActionEntry>(Node))
				{
					if (ExistEventNode->EntryTransitionProperty == EntryTransitionProperty)
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
