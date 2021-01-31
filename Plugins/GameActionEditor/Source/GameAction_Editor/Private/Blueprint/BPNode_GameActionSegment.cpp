// Fill out your copyright notice in the Description page of Project Settings.


#include "Blueprint/BPNode_GameActionSegment.h"
#include <EdGraphSchema_K2.h>
#include <BlueprintNodeSpawner.h>
#include <BlueprintActionDatabaseRegistrar.h>
#include <Kismet2/BlueprintEditorUtils.h>
#include <Kismet2/Kismet2NameValidators.h>
#include <MovieScene.h>
#include <KismetNodes/SGraphNodeK2Default.h>
#include <Widgets/Text/SInlineEditableTextBlock.h>
#include <SGraphPin.h>
#include <KismetCompiler.h>
#include <Classes/EditorStyleSettings.h>
#include <EditorStyleSet.h>
#include <Widgets/SBoxPanel.h>
#include <Widgets/SOverlay.h>
#include <Widgets/Text/STextBlock.h>
#include <K2Node_CallArrayFunction.h>
#include <K2Node_CustomEvent.h>
#include <K2Node_VariableGet.h>
#include <Widgets/Images/SImage.h>
#include <Editor.h>

#include "Sequence/GameActionSequence.h"
#include "GameAction/GameActionSegment.h"
#include "GameAction/GameActionInstance.h"
#include "Blueprint/EdGraph_GameAction.h"
#include "Blueprint/GameActionBlueprint.h"
#include "Blueprint/BPNode_GameActionTransition.h"
#include "GameAction/GameActionCompilerContext.h"

#define LOCTEXT_NAMESPACE "BPNode_GameAction"

UBPNode_GameActionSegmentEvent::UBPNode_GameActionSegmentEvent()
{
	bOverrideFunction = false;
	bInternalEvent = true;
}

FText UBPNode_GameActionSegmentEvent::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return DelegateProperty.Get() ? DelegateProperty->GetDisplayNameText() : Super::GetNodeTitle(TitleType);
}

void UBPNode_GameActionSegmentEvent::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	FDelegateProperty* Delegate = DelegateProperty.Get();
	if (Delegate == nullptr)
	{
		CompilerContext.MessageLog.Error(TEXT("@@ 的中事件委托已失效，请删除该行为事件节点"), this);
		return;
	}
	
	const FName EventName = *FString::Printf(TEXT("%s_%s"), *SegmentNode->GetRefVarName().ToString(), *Delegate->GetName());
	CustomFunctionName = EventName;
	
	Super::ExpandNode(CompilerContext, SourceGraph);
}

FText UBPNode_GameActionSegmentEvent::GetMenuCategory() const
{
	return LOCTEXT("Game Action|Segment Event", "Game Action|Segment Event");
}

void UBPNode_GameActionSegmentEvent::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		TArray<UClass*> DerivedClasses;
		GetDerivedClasses(UGameActionSegmentBase::StaticClass(), DerivedClasses);
		DerivedClasses.Add(UGameActionSegmentBase::StaticClass());
		TSet<FDelegateProperty*> GameActionSegmentEventProperties;
		for (UClass* DerivedClass : DerivedClasses)
		{
			if (DerivedClass == nullptr || DerivedClass->HasAnyClassFlags(CLASS_Deprecated))
			{
				continue;
			}

			for (TFieldIterator<FDelegateProperty> It(DerivedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				if (It->GetBoolMetaData(TEXT("GameActionSegmentEvent")))
				{
					GameActionSegmentEventProperties.Add(*It);
				}
			}

			for (FDelegateProperty* EventProperty : GameActionSegmentEventProperties)
			{
				UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
				check(NodeSpawner != nullptr);

				NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda([=](UEdGraphNode* NewNode, bool bIsTemplateNode) mutable
				{
					UBPNode_GameActionSegmentEvent* GameActionSegmentEventNode = CastChecked<UBPNode_GameActionSegmentEvent>(NewNode);
					GameActionSegmentEventNode->EventReference.SetExternalDelegateMember(EventProperty->SignatureFunction->GetFName());
					GameActionSegmentEventNode->DelegateProperty = EventProperty;
				});
				ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
			}
		}
	}
}

bool UBPNode_GameActionSegmentEvent::IsActionFilteredOut(FBlueprintActionFilter const& Filter)
{
	for (UEdGraph* Graph : Filter.Context.Graphs)
	{
		UBPNode_GameActionSegmentBase* ActionSegmentNode = Graph->GetTypedOuter<UBPNode_GameActionSegmentBase>();
		if (ActionSegmentNode == nullptr || ActionSegmentNode->GameActionSegment == nullptr)
		{
			return true;
		}
		
		if (ActionSegmentNode->GameActionSegment->GetClass()->HasProperty(DelegateProperty.Get()) == false)
		{
			return true;
		}
		
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UBPNode_GameActionSegmentEvent* ExistEventNode = Cast<UBPNode_GameActionSegmentEvent>(Node))
			{
				if (ExistEventNode->DelegateProperty == DelegateProperty)
				{
					return true;
				}
			}
		}
	}
	return false;
}

void UBPNode_GameActionSegmentEvent::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();

	SegmentNode = GetTypedOuter<UBPNode_GameActionSegmentBase>();
	check(SegmentNode);
}

FText UBPNode_GameActionSegmentEvent::GetTooltipText() const
{
	if (FDelegateProperty* Delegate = DelegateProperty.Get())
	{
		return Delegate->GetToolTipText();
	}
	return FText::GetEmpty();
}

bool UBPNode_GameActionSegmentEvent::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
	return TargetGraph->GetTypedOuter<UBPNode_GameActionSegmentBase>() != nullptr;
}

UBPNode_GameActionSegmentBase::UBPNode_GameActionSegmentBase()
{
	AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
	bCanRenameNode = true;
}

FText UBPNode_GameActionSegmentBase::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
	{
		if (GameActionSegment)
		{
			return FText::Format(LOCTEXT("Add Action Title", "Add Action [{0}]"), GameActionSegment->GetClass()->GetDisplayNameText());
		}
	}
	if (TitleType == ENodeTitleType::EditableTitle)
	{
		return FText::FromName(GetRefVarName());
	}
	return FText::Format(LOCTEXT("Action Node Full Title", "{0}\n{1}"), FText::FromName(GetRefVarName()), GameActionSegment ? GameActionSegment->GetClass()->GetDisplayNameText() : LOCTEXT("None", "None"));
}

FText UBPNode_GameActionSegmentBase::GetMenuCategory() const
{
	return LOCTEXT("Game Action", "Game Action");
}

bool UBPNode_GameActionSegmentBase::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
	return TargetGraph->IsA<UEdGraph_GameAction>();
}

void UBPNode_GameActionSegmentBase::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, GameActionNodeUtils::GameActionExecPinCategory, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, GameActionNodeUtils::GameActionExecPinCategory, UEdGraphSchema_K2::PN_Then);

	if (GameActionSegment)
	{
		BuildOptionPins(ShowPinForProperties, GameActionSegment->GetClass());
	}
	
	UEdGraphPin* ReturnPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, GameActionSegment ? GameActionSegment->GetClass() : UGameActionSegmentBase::StaticClass(), UEdGraphSchema_K2::PN_ReturnValue);
	ReturnPin->bHidden = true;
}

TSharedPtr<class INameValidatorInterface> UBPNode_GameActionSegmentBase::MakeNameValidator() const
{
	struct FNameValidatorInterface : public INameValidatorInterface
	{
		FNameValidatorInterface(const UBPNode_GameActionSegmentBase* TestNode)
			:TestNode(TestNode)
		{}
		const UBPNode_GameActionSegmentBase* TestNode;
		EValidatorResult IsValid(const FName& Name, bool bOriginal) override
		{
			if (Name.GetStringLength() == 0)
			{
				return EValidatorResult::EmptyName;
			}
			FText Result;
			if (Name.IsValidObjectName(Result) == false)
			{
				return EValidatorResult::ContainsInvalidCharacters;
			}
			return IsUniqueRefVarName(TestNode->GetGraph(), Name) ? EValidatorResult::Ok : EValidatorResult::ExistingName;
		}
		EValidatorResult IsValid(const FString& Name, bool bOriginal) override
		{
			return IsValid(FName(Name), bOriginal);
		}
	};
	return MakeShareable(new FNameValidatorInterface(this));
}

void UBPNode_GameActionSegmentBase::OnRenameNode(const FString& NewName)
{
	if (RefVarName.ToString() == NewName)
	{
		return;
	}
	if (ensureAlways(IsUniqueRefVarName(GetGraph(), *NewName)))
	{
		const FName OldName = RefVarName;
		RefVarName = *NewName;

		if (OldName != NAME_None)
		{
			for (UEdGraphPin* Pin : FindPinChecked(UEdGraphSchema_K2::PN_Execute)->LinkedTo)
			{
				if (UBPNode_GameActionTransitionBase* TransitionNode = Cast<UBPNode_GameActionTransitionBase>(Pin->GetOwningNode()))
				{
					TransitionNode->UpdateBoundGraphName();
				}
			}

			for (UEdGraphPin* Pin : FindPinChecked(UEdGraphSchema_K2::PN_Then)->LinkedTo)
			{
				if (UBPNode_GameActionTransitionBase* TransitionNode = Cast<UBPNode_GameActionTransitionBase>(Pin->GetOwningNode()))
				{
					TransitionNode->UpdateBoundGraphName();
				}
			}

			if (UBlueprint* Blueprint = GetBlueprint())
			{
				FBlueprintEditorUtils::ReplaceVariableReferences(Blueprint, OldName, *NewName);
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			}
			if (EventGraph)
			{
				EventGraph->Rename(*GetEventGraphName().ToString());
			}
		}
	}
}

void UBPNode_GameActionSegmentBase::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();

	check(GameActionSegment);
	OnRenameNode(MakeUniqueRefVarName(GetGraph(), GameActionSegment->GetClass()->GetDisplayNameText().ToString()));

	RecompileSkeletalBlueprintDelayed();
}

void UBPNode_GameActionSegmentBase::PostPasteNode()
{
	Super::PostPasteNode();

	if (GameActionSegment)
	{
		OnRenameNode(MakeUniqueRefVarName(GetGraph(), GameActionSegment->GetClass()->GetDisplayNameText().ToString()));
	}

	RecompileSkeletalBlueprintDelayed();
}

void UBPNode_GameActionSegmentBase::DestroyNode()
{
	Super::DestroyNode();

	if (EventGraph)
	{
		GetGraph()->SubGraphs.Remove(EventGraph);
	}
	
	RecompileSkeletalBlueprintDelayed();
}

void UBPNode_GameActionSegmentBase::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		RecompileSkeletalBlueprintDelayed();
	}
}

TSharedPtr<SGraphNode> UBPNode_GameActionSegmentBase::CreateVisualWidget()
{
	// SGraphNodeAnimState
	class SGameActionNode : public SGraphNodeK2Default
	{
		using Super = SGraphNodeK2Default;
		TSharedPtr<SOverlay> ActionPinBox;
	public:
		void Construct(const FArguments& InArgs, UBPNode_GameActionSegmentBase* InNode)
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
			ActionPinBox.Reset();

			const static FSlateBrush* NodeTypeIcon = FEditorStyle::GetBrush(TEXT("Graph.StateNode.Icon"));

			FLinearColor TitleShadowColor(0.6f, 0.6f, 0.6f);
			TSharedPtr<SErrorText> ErrorText;
			TSharedPtr<SNodeTitle> NodeTitle = SNew(SNodeTitle, GraphNode);

			this->ContentScale.Bind( this, &SGraphNode::GetContentScale );
			this->GetOrAddSlot( ENodeZone::Center )
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SBorder)
					.BorderImage( FEditorStyle::GetBrush( "Graph.StateNode.Body" ) )
					.Padding(0)
					.BorderBackgroundColor_Lambda([=]
					{
						const FLinearColor InactiveStateColor(0.08f, 0.08f, 0.08f);
						const FLinearColor ActiveStateColorBright(1.f, 0.6f, 0.35f);
						return CastChecked<UBPNode_GameActionSegmentBase>(GetNodeObj())->DebugState == EDebugState::Actived ? ActiveStateColorBright : InactiveStateColor;
					})
					[
						SNew(SOverlay)
						+SOverlay::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Fill)
						[
							SAssignNew(ActionPinBox, SOverlay)
						]

						// STATE NAME AREA
						+SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Padding(10.0f)
						[
							SNew(SBorder)
							.BorderImage( FEditorStyle::GetBrush("Graph.StateNode.ColorSpill") )
							.BorderBackgroundColor( TitleShadowColor )
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.Visibility(EVisibility::Visible)
							[
								SNew(SVerticalBox)
								+SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(SHorizontalBox)
									+SHorizontalBox::Slot()
									.AutoWidth()
									[
										// POPUP ERROR MESSAGE
										SAssignNew(ErrorText, SErrorText )
										.BackgroundColor( this, &SGameActionNode::GetErrorColor )
										.ToolTipText( this, &SGameActionNode::GetErrorMsgToolTip )
									]
									+SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									[
										SNew(SImage)
										.Image(NodeTypeIcon)
									]
									+SHorizontalBox::Slot()
									.Padding(FMargin(4.0f, 0.0f, 4.0f, 0.0f))
									[
										SNew(SVerticalBox)
										+SVerticalBox::Slot()
										.AutoHeight()
										[
											SAssignNew(InlineEditableText, SInlineEditableTextBlock)
											.Style( FEditorStyle::Get(), "Graph.StateNode.NodeTitleInlineEditableText" )
											.Text( NodeTitle.Get(), &SNodeTitle::GetHeadTitle )
											.OnVerifyTextChanged(this, &SGameActionNode::OnVerifyNameTextChanged)
											.OnTextCommitted(this, &SGameActionNode::OnNameTextCommited)
											.IsReadOnly( this, &SGameActionNode::IsNameReadOnly )
											.IsSelected(this, &SGameActionNode::IsSelectedExclusively)
										]
										+SVerticalBox::Slot()
										.AutoHeight()
										[
											NodeTitle.ToSharedRef()
										]
									]
								]
								// PIN AREA
								+SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.HAlign(HAlign_Fill)
									.VAlign(VAlign_Fill)
									[
										SAssignNew(LeftNodeBox, SVerticalBox)
									]
									+ SHorizontalBox::Slot()
									.HAlign(HAlign_Fill)
									.VAlign(VAlign_Fill)
									[
										SAssignNew(RightNodeBox, SVerticalBox)
									]
								]
							]
						]
					]
				];

			ErrorReporting = ErrorText;
			ErrorReporting->SetError(ErrorMsg);
			CreatePinWidgets();
		}
		void CreatePinWidgets() override
		{
			for (int32 PinIndex = 0; PinIndex < GraphNode->Pins.Num(); ++PinIndex)
			{
				UEdGraphPin* CurPin = GraphNode->Pins[PinIndex];

				if (CurPin->PinType.PinSubCategory == GameActionNodeUtils::GameActionExecPinCategory)
				{
					class SGameActionExecPin : public SGraphPin
					{
					public:
						SLATE_BEGIN_ARGS(SGameActionExecPin) {}
						SLATE_END_ARGS()

						void Construct(const FArguments& InArgs, UEdGraphPin* InPin)
						{
							this->SetCursor(EMouseCursor::Default);

							bShowLabel = true;

							GraphPinObj = InPin;
							check(GraphPinObj != NULL);

							const UEdGraphSchema* Schema = GraphPinObj->GetSchema();
							check(Schema);

							// Set up a hover for pins that is tinted the color of the pin.
							SBorder::Construct(SBorder::FArguments()
								.BorderImage_Lambda([=]
								{
									return (IsHovered())
										? FEditorStyle::GetBrush(TEXT("Graph.StateNode.Pin.BackgroundHovered"))
										: FEditorStyle::GetBrush(TEXT("Graph.StateNode.Pin.Background"));
								})
								.BorderBackgroundColor(this, &SGameActionExecPin::GetPinColor)
								.OnMouseButtonDown(this, &SGameActionExecPin::OnPinMouseDown)
								.Cursor(this, &SGameActionExecPin::GetPinCursor)
							);
						}
					protected:
						TSharedRef<SWidget>	GetDefaultValueWidget() override
						{
							return SNew(STextBlock);
						}
					};
 					TSharedRef<SGraphPin> PinToAdd = SNew(SGameActionExecPin, CurPin);
					PinToAdd->SetOwner(SharedThis(this));
 					if (PinToAdd->GetDirection() == EEdGraphPinDirection::EGPD_Input)
 					{
						ActionPinBox->AddSlot()
 							.HAlign(HAlign_Center)
 							.VAlign(VAlign_Center)
 							[
 								PinToAdd
 							];
 						InputPins.Add(PinToAdd);
 					}
 					else
 					{
						ActionPinBox->AddSlot()
 							.HAlign(HAlign_Fill)
 							.VAlign(VAlign_Fill)
 							[
 								PinToAdd
 							];
 						OutputPins.Add(PinToAdd);
 					}
				}
				else
				{
					CreateStandardPinWidget(CurPin);
				}
			}
		}
	};
	return SNew(SGameActionNode, this);
}

namespace GameActionEventUtil
{
	static UBPNode_GameActionSegmentEvent* AddEventNode(const UBPNode_GameActionSegmentBase* SegmentNode, UEdGraph* InGraph, FDelegateProperty* DelegateProperty, int32& InOutNodePosY)
	{
		// Snap the new position to the grid
		const UEditorStyleSettings* StyleSettings = GetDefault<UEditorStyleSettings>();
		if (StyleSettings)
		{
			const uint32 GridSnapSize = StyleSettings->GridSnapSize;
			InOutNodePosY = GridSnapSize * FMath::RoundFromZero(InOutNodePosY / (float)GridSnapSize);
		}

		// Add the event
		UBPNode_GameActionSegmentEvent* NewEventNode = NewObject<UBPNode_GameActionSegmentEvent>(InGraph);
		NewEventNode->EventReference.SetExternalDelegateMember(DelegateProperty->SignatureFunction->GetFName());
		NewEventNode->DelegateProperty = DelegateProperty;
		NewEventNode->CreateNewGuid();
		NewEventNode->PostPlacedNewNode();
		NewEventNode->SetFlags(RF_Transactional);
		NewEventNode->bCommentBubblePinned = true;
		NewEventNode->bCommentBubbleVisible = true;
		NewEventNode->MakeAutomaticallyPlacedGhostNode();
		NewEventNode->AllocateDefaultPins();
		
		NewEventNode->NodePosY = InOutNodePosY;
		UEdGraphSchema_K2::SetNodeMetaData(NewEventNode, FNodeMetadata::DefaultGraphNode);
		InOutNodePosY = NewEventNode->NodePosY + NewEventNode->NodeHeight + 200;

		InGraph->AddNode(NewEventNode);

		return NewEventNode;
	}
}

UObject* UBPNode_GameActionSegmentBase::GetJumpTargetForDoubleClick() const
{
	if (EventGraph == nullptr && GameActionSegment)
	{
		using namespace GameActionEventUtil;
		int32 EventNodePosY = 0.f;
		EventGraph = FBlueprintEditorUtils::CreateNewGraph(const_cast<UBPNode_GameActionSegmentBase*>(this), GetEventGraphName(), UEdGraph::StaticClass(), UEdGraphSchema_GameActionEvents::StaticClass());
		EventGraph->bAllowDeletion = false;
		EventGraph->bAllowRenaming = false;
		for (TFieldIterator<FDelegateProperty> It(GameActionSegment->GetClass()); It; ++It)
		{
			if (It->GetBoolMetaData(TEXT("GameActionSegmentEvent")))
			{
				AddEventNode(this, EventGraph, *It, EventNodePosY);
			}
		}
		GetGraph()->SubGraphs.Add(EventGraph);
	}
	return EventGraph;
}

void UBPNode_GameActionSegmentBase::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	if (GameActionSegment == nullptr)
	{
		CompilerContext.MessageLog.Error(TEXT("@@ 的中不存在GameActionSegment"), this);
		return;
	}
	
	if (ShowPinForProperties.ContainsByPredicate([](const FOptionalPinFromProperty& E) { return E.bShowPin; }))
	{
		UK2Node_CustomEvent* EvaluateExposedInputsEventNode = CompilerContext.SpawnIntermediateEventNode<UK2Node_CustomEvent>(this, nullptr, SourceGraph);
		EvaluateExposedInputsEventNode->CustomFunctionName = *FString::Printf(TEXT("%s_EvaluateExposedInputsEvent"), *GetRefVarName().ToString());
		EvaluateExposedInputsEventNode->AllocateDefaultPins();
		UEdGraphPin* ThenPin = EvaluateExposedInputsEventNode->FindPinChecked(UEdGraphSchema_K2::PN_Then);

		UClass* GameActionClass = CompilerContext.Blueprint->SkeletonGeneratedClass;
		UK2Node_VariableGet* GetInstanceNode = CompilerContext.SpawnIntermediateNode<UK2Node_VariableGet>(this, SourceGraph);
		FObjectProperty* TemplateProperty = FindFProperty<FObjectProperty>(GameActionClass, GetRefVarName());
		GetInstanceNode->VariableReference.SetFromField<FObjectProperty>(TemplateProperty, GameActionClass);
		GetInstanceNode->AllocateDefaultPins();
		UEdGraphPin* InstancePin = GetInstanceNode->FindPinChecked(TemplateProperty->GetName());
		
		static const FName ObjectParamName(TEXT("Object"));
		static const FName ValueParamName(TEXT("Value"));
		static const FName PropertyNameParamName(TEXT("PropertyName"));

		const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

		for (TFieldIterator<FProperty> Property(GameActionSegment->GetClass()); Property; ++Property)
		{
			if (Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance) || Property->HasAllPropertyFlags(CPF_ExposeOnSpawn) == false)
			{
				continue;
			}

			UEdGraphPin* OrgPin = FindPin(Property->GetFName(), EGPD_Input);
			if (OrgPin)
			{
				if (OrgPin->LinkedTo.Num() == 0)
				{
					CompilerContext.MessageLog.Error(TEXT("@@ 的引脚 @@ 必须存在连接，否则请取消显示引脚状态"), this, OrgPin);
					continue;
				}
				
				UFunction* SetByNameFunction = Schema->FindSetVariableByNameFunction(OrgPin->PinType);
				if (SetByNameFunction)
				{
					UK2Node_CallFunction* SetVarNode = nullptr;
					if (OrgPin->PinType.IsArray())
					{
						SetVarNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallArrayFunction>(this, SourceGraph);
					}
					else
					{
						SetVarNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
					}
					SetVarNode->SetFromFunction(SetByNameFunction);
					SetVarNode->AllocateDefaultPins();

					// Connect this node into the exec chain
					Schema->TryCreateConnection(ThenPin, SetVarNode->GetExecPin());
					ThenPin = SetVarNode->GetThenPin();

					// Connect the new actor to the 'object' pin
					UEdGraphPin* ObjectPin = SetVarNode->FindPinChecked(ObjectParamName);
					InstancePin->MakeLinkTo(ObjectPin);

					// Fill in literal for 'property name' pin - name of pin is property name
					UEdGraphPin* PropertyNamePin = SetVarNode->FindPinChecked(PropertyNameParamName);
					PropertyNamePin->DefaultValue = Property->GetName();

					UEdGraphPin* ValuePin = SetVarNode->FindPinChecked(ValueParamName);
					// For non-array struct pins that are not linked, transfer the pin type so that the node will expand an auto-ref that will assign the value by-ref.
					if (OrgPin->PinType.IsArray() == false && OrgPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct &&
						OrgPin->LinkedTo.Num() == 0)
					{
						ValuePin->PinType.PinCategory = OrgPin->PinType.PinCategory;
						ValuePin->PinType.PinSubCategory = OrgPin->PinType.PinSubCategory;
						ValuePin->PinType.PinSubCategoryObject = OrgPin->PinType.PinSubCategoryObject;

						CompilerContext.MovePinLinksToIntermediate(*OrgPin, *ValuePin);
					}
					else
					{
						CompilerContext.MovePinLinksToIntermediate(*OrgPin, *ValuePin);
						SetVarNode->PinConnectionListChanged(ValuePin);
					}
				}
			}
		}
	}
	FGameActionCompilerContext& GameActionCompilerContext = StaticCast<FGameActionCompilerContext&>(CompilerContext);
	FGameActionCompilerContext::FTransitionData& ActionTransitionData = GameActionCompilerContext.ActionTransitionDatas.Add(GetRefVarName());
	const TArray<UEdGraphPin*> LinkedTo = FindPinChecked(UEdGraphSchema_K2::PN_Then)->LinkedTo;
	FindPinChecked(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
	for (int32 Idx = 0; Idx < LinkedTo.Num(); ++Idx)
	{
		UBPNode_GameActionTransition* TransitionNode = CastChecked<UBPNode_GameActionTransition>(LinkedTo[Idx]->GetOwningNode());
		UBPNode_GameActionSegmentBase* NextSegmentNode = TransitionNode->ToNode;
		const FName TransitionConditionName = TransitionNode->GetTransitionName();

		switch (TransitionNode->TransitionType)
		{
		case EGameActionTransitionType::Tick:
		{
			auto& TickData = ActionTransitionData.TickDatas.AddDefaulted_GetRef();
			TickData.Condition = TransitionConditionName;
			TickData.SegmentName = NextSegmentNode->GetRefVarName();
			TickData.Order = TransitionNode->NodePosY;
		}
		break;
		case EGameActionTransitionType::Event:
		{
			auto& EventData = ActionTransitionData.EventDatas.AddDefaulted_GetRef();
			EventData.Event = TransitionNode->EventName;
			EventData.Order = TransitionNode->NodePosY;
			EventData.Condition = TransitionConditionName;
			EventData.SegmentName = NextSegmentNode->GetRefVarName();
		}
		break;
		default:
			checkNoEntry();
			break;
		}
	}

	BreakAllNodeLinks();
}

void UBPNode_GameActionSegmentBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(FOptionalPinFromProperty, bShowPin))
	{
		GetSchema()->ReconstructNode(*this);
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

bool UBPNode_GameActionSegmentBase::IsUniqueRefVarName(const UEdGraph* Graph, const FName& Name)
{
	for (UEdGraphNode* EdNode : Graph->Nodes)
	{
		if (UBPNode_GameActionSegmentBase* GameActionSegmentNode = Cast<UBPNode_GameActionSegmentBase>(EdNode))
		{
			if (GameActionSegmentNode->GetRefVarName() == Name)
			{
				return false;
			}
		}
	}
	return StaticFindObjectFast(nullptr, const_cast<UEdGraph*>(Graph), Name) == nullptr;
}

FString UBPNode_GameActionSegmentBase::MakeUniqueRefVarName(const UEdGraph* Graph, const FString& BaseName)
{
	FString TestName;
	int32 UniqueNumber = 0;
	do
	{
		TestName = FString::Printf(TEXT("%s_%d"), *BaseName, UniqueNumber);
		UniqueNumber += 1;
	} while (!IsUniqueRefVarName(Graph, *TestName));
	return TestName;
}

FTimerHandle UBPNode_GameActionSegmentBase::SkeletalRecompileChildrenHandle;

void UBPNode_GameActionSegmentBase::RecompileSkeletalBlueprintDelayed()
{
	if (UBlueprint* Blueprint = GetBlueprint())
	{
		GEditor->GetTimerManager()->SetTimer(SkeletalRecompileChildrenHandle, FTimerDelegate::CreateWeakLambda(Blueprint, [Blueprint]()
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		}), MIN_flt, false);
	}
}

void UBPNode_GameActionSegmentBase::BuildOptionPins(TArray<FOptionalPinFromProperty>& ShowPinOptions, UClass* Class)
{
	struct FGameActionSegmentOptionalPinManager : public FOptionalPinManager
	{
		using Super = FOptionalPinManager;

		void GetRecordDefaults(FProperty* TestProperty, FOptionalPinFromProperty& Record) const override
		{
			Super::GetRecordDefaults(TestProperty, Record);

			Record.bShowPin = false;
		}

		bool CanTreatPropertyAsOptional(FProperty* TestProperty) const override
		{
			return TestProperty->HasAnyPropertyFlags(CPF_ExposeOnSpawn);
		}
	};

	FGameActionSegmentOptionalPinManager GameActionSegmentOptionalPinManager;
	GameActionSegmentOptionalPinManager.RebuildPropertyList(ShowPinOptions, Class);
	GameActionSegmentOptionalPinManager.CreateVisiblePins(ShowPinOptions, Class, EGPD_Input, this);
}

void UBPNode_GameActionSegment::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		TArray<UClass*> DerivedClasses;
		GetDerivedClasses(UGameActionSegment::StaticClass(), DerivedClasses);
		if (DerivedClasses.Num() == 0)
		{
			DerivedClasses.Add(UGameActionSegment::StaticClass());
		}
		for (UClass* DerivedClass : DerivedClasses)
		{
			if (DerivedClass == nullptr || DerivedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
			{
				continue;
			}
			
			UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
			check(NodeSpawner != nullptr);

			NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda([=](UEdGraphNode* NewNode, bool bIsTemplateNode) mutable
			{
				UBPNode_GameActionSegmentBase* GameActionSegmentNode = CastChecked<UBPNode_GameActionSegmentBase>(NewNode);
				UGameActionSegment* GameActionSegment = NewObject<UGameActionSegment>(GameActionSegmentNode, DerivedClass, NAME_None, RF_Transactional);
				GameActionSegmentNode->GameActionSegment = GameActionSegment;

				if (bIsTemplateNode == false)
				{
					UGameActionBlueprint* GameActionBlueprint = NewNode->GetTypedOuter<UGameActionBlueprint>();
					GameActionSegmentNode->GetGameActionSequence()->SetOwnerCharacter(GameActionBlueprint->GetOwnerType());
				}
			});
			ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
		}
	}
}

void UBPNode_GameActionSegment::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	if (UGameActionSegment* Segment = Cast<UGameActionSegment>(GameActionSegment))
	{
		if (Segment->GameActionSequence == nullptr)
		{
			CompilerContext.MessageLog.Error(TEXT("@@ 必须存在动画"), this);
		}
	}
}

UGameActionSequence* UBPNode_GameActionSegment::GetGameActionSequence() const
{
	if (UGameActionSegment* Segment = Cast<UGameActionSegment>(GameActionSegment))
	{
		return Segment->GameActionSequence;
	}
	return nullptr;
}

void UBPNode_GameActionSegmentGeneric::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		TArray<UClass*> DerivedClasses;
		GetDerivedClasses(UGameActionSegmentGeneric::StaticClass(), DerivedClasses);
		for (UClass* DerivedClass : DerivedClasses)
		{
			if (DerivedClass == nullptr || DerivedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
			{
				continue;
			}
			
			UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
			check(NodeSpawner != nullptr);

			NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda([=](UEdGraphNode* NewNode, bool bIsTemplateNode) mutable
			{
				UBPNode_GameActionSegmentBase* GameActionSegmentNode = CastChecked<UBPNode_GameActionSegmentBase>(NewNode);
				UGameActionSegmentBase* GameActionSegment = NewObject<UGameActionSegmentBase>(GameActionSegmentNode, DerivedClass, NAME_None, RF_Transactional);
				GameActionSegmentNode->GameActionSegment = GameActionSegment;
			});
			ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
		}
	}
}

#undef LOCTEXT_NAMESPACE
