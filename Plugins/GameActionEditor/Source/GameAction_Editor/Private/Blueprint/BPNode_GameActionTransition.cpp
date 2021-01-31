// Fill out your copyright notice in the Description page of Project Settings.


#include "Blueprint/BPNode_GameActionTransition.h"
#include <Kismet2/BlueprintEditorUtils.h>
#include <EdGraphUtilities.h>
#include <EdGraphSchema_K2.h>
#include <KismetNodes/SGraphNodeK2Default.h>
#include <ConnectionDrawingPolicy.h>
#include <KismetCompiler.h>
#include <K2Node_CallFunction.h>
#include <K2Node_FunctionEntry.h>
#include <K2Node_FunctionResult.h>
#include <BlueprintActionDatabaseRegistrar.h>
#include <BlueprintNodeSpawner.h>
#include <K2Node_IfThenElse.h>
#include <K2Node_Knot.h>
#include <K2Node_DynamicCast.h>
#include <Widgets/Layout/SConstraintCanvas.h>
#include <IDocumentation.h>
#include <SKismetLinearExpression.h>
#include <Widgets/Images/SImage.h>
#include <Widgets/Layout/SBox.h>
#include <Widgets/SToolTip.h>

#include "Blueprint/BPNode_GameActionEntry.h"
#include "Blueprint/BPNode_GameActionSegment.h"
#include "GameAction/GameActionSegment.h"

#define LOCTEXT_NAMESPACE "BPNode_GameActionTransition"

TSharedPtr<SGraphNode> FGameActionGraphNodeFactory::CreateNode(UEdGraphNode* InNode) const
{
	if (InNode->GetGraph()->IsA<UGameActionTransitionGraph>())
	{
		if (InNode->IsA<UK2Node_FunctionEntry>() || InNode->IsA<UK2Node_FunctionResult>())
		{
			class SGameActionTransitionFunctionNode : public SGraphNodeK2Default
			{
				using Super = SGraphNodeK2Default;
			public:
				void Construct(const FArguments& InArgs, UK2Node* InNode)
				{
					Super::Construct(InArgs, InNode);
				}
				void UpdateGraphNode() override
				{
					SetVisibility(EVisibility::Hidden);
				}
			};
			return SNew(SGameActionTransitionFunctionNode, CastChecked<UK2Node>(InNode));
		}
	}
	return nullptr;
}

FText UBPNode_GameActionTransitionResult::GetTooltipText() const
{
	return LOCTEXT("TransitionResultTooltip", "This expression is evaluated to determine if the state transition can be taken");
}

FText UBPNode_GameActionTransitionResult::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Result", "Result");
}

void UBPNode_GameActionTransitionResult::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	using namespace GameActionTransitionResultUtils;
	UEdGraphPin* Exec = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec,  UEdGraphSchema_K2::PN_Execute);
	Exec->bHidden = true;
	UEdGraphPin* IsServerPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Boolean, IsServerPinName);
	IsServerPin->bHidden = true;
	UEdGraphPin* AutonomousThenPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, AutonomousThenPinName);
	AutonomousThenPin->bHidden = true;
	UEdGraphPin* ServerThenPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, ServerThenPinName);
	ServerThenPin->bHidden = true;
	UEdGraphPin* AutonomousOutPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, AutonomousOutPinName);
	AutonomousOutPin->bHidden = true;
	UEdGraphPin* ServerOutPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, ServerOutPinName);
	ServerOutPin->bHidden = true;

	UEdGraphPin* AutonomousPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Boolean, AutonomousPinName);
	AutonomousPin->PinFriendlyName = LOCTEXT("主控端结果", "主控端结果");
	AutonomousPin->DefaultValue = TEXT("true");
	UEdGraphPin* ServerPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Boolean, ServerPinName);
	ServerPin->PinFriendlyName = LOCTEXT("服务器结果", "服务器结果");
	ServerPin->PinToolTip = LOCTEXT("服务器结果提示", "若是远程主控端导致的跳转会使用服务器结果进行校验").ToString();
	ServerPin->DefaultValue = TEXT("true");
}

TSharedPtr<SGraphNode> UBPNode_GameActionTransitionResult::CreateVisualWidget()
{
	class SGameActionTransitionResultNode : public SGraphNodeK2Default
	{
		using Super = SGraphNodeK2Default;
	public:
		void Construct(const FArguments& InArgs, UK2Node* InNode)
		{
			Super::Construct(InArgs, InNode);
		}
		void CreatePinWidgets() override
		{
			for (UEdGraphPin* CurPin : GraphNode->Pins)
			{
				if (!ensure(CurPin->GetOuter() == GraphNode))
				{
					continue;
				}
				if (CurPin->bHidden)
				{
					continue;
				}
				CreateStandardPinWidget(CurPin);
			}
		}
	};
	return SNew(SGameActionTransitionResultNode, this);
}

void UBPNode_GameActionTransitionResult::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	using namespace GameActionTransitionResultUtils;

	UK2Node_IfThenElse* IfThenElseNode = CompilerContext.SpawnIntermediateNode<UK2Node_IfThenElse>(this, SourceGraph);
	IfThenElseNode->AllocateDefaultPins();

	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *IfThenElseNode->GetExecPin());
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(AutonomousThenPinName, EGPD_Output), *IfThenElseNode->GetElsePin());
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(ServerThenPinName, EGPD_Output), *IfThenElseNode->GetThenPin());
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(IsServerPinName, EGPD_Input), *IfThenElseNode->GetConditionPin());

	{
		UK2Node_Knot* Knot = CompilerContext.SpawnIntermediateNode<UK2Node_Knot>(this, SourceGraph);
		Knot->AllocateDefaultPins();
		CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(AutonomousPinName, EGPD_Input), *Knot->GetInputPin());
		CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(AutonomousOutPinName, EGPD_Output), *Knot->GetOutputPin());
	}

	{
		UK2Node_Knot* Knot = CompilerContext.SpawnIntermediateNode<UK2Node_Knot>(this, SourceGraph);
		Knot->AllocateDefaultPins();
		CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(ServerPinName, EGPD_Input), *Knot->GetInputPin());
		CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(ServerOutPinName, EGPD_Output), *Knot->GetOutputPin());
	}
}

class SGameActionTransitionNodeBase : public SGraphNodeK2Default
{
	using Super = SGraphNodeK2Default;
public:
	void Construct(const FArguments& InArgs, UBPNode_GameActionTransitionBase* InNode)
	{
		Super::Construct(InArgs, InNode);
	}
	void UpdateGraphNode() override
	{
		InputPins.Empty();
		OutputPins.Empty();

		RightNodeBox.Reset();
		LeftNodeBox.Reset();

		const FSlateBrush* IndexBrush = FEditorStyle::GetBrush(TEXT("BTEditor.Graph.BTNode.Index"));

		this->ContentScale.Bind(this, &SGraphNode::GetContentScale);
		this->GetOrAddSlot( ENodeZone::Center )
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SAssignNew(TransitionOverlay, SOverlay)
				+ SOverlay::Slot()	
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(SOverlay)
					+SOverlay::Slot()
					[
						SNew(SImage)
						.Image( FEditorStyle::GetBrush("Graph.TransitionNode.ColorSpill") )
						.ColorAndOpacity_Lambda([=]
						{
							return IsHovered() ? UBPNode_GameActionTransitionBase::HoverColor : UBPNode_GameActionTransitionBase::BaseColor;
						})
					]
					+SOverlay::Slot()
					[
						SNew(SImage)
						.Image(FEditorStyle::GetBrush("Graph.TransitionNode.Icon"))
					]
					+SOverlay::Slot()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Top)
					[
						SNew(SOverlay)
						.RenderTransform(FSlateRenderTransform(FVector2D(14.f, -14.f)))
						+SOverlay::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Fill)
						[
							// Add a dummy box here to make sure the widget doesnt get smaller than the brush
							SNew(SBox)
							.WidthOverride(IndexBrush->ImageSize.X)
							.HeightOverride(IndexBrush->ImageSize.Y)
						]
						+SOverlay::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Fill)
						[
							SNew(SBorder)
							.BorderImage(IndexBrush)
							.BorderBackgroundColor(FSlateColor(FColor::Black))
							.VAlign(VAlign_Center)
							.HAlign(HAlign_Center)
							.ToolTipText(LOCTEXT("条件执行的优先级", "条件执行的优先级"))
							[
								SNew(STextBlock)
								.Text_Lambda([this]
								{
									UBPNode_GameActionTransitionBase* TransitionNode = CastChecked<UBPNode_GameActionTransitionBase>(GraphNode);
									if (TransitionNode->FindPinChecked(UEdGraphSchema_K2::PN_Then)->LinkedTo.Num() > 0)
									{
										const UEdGraphNode* End = TransitionNode->GetToPin()->GetOwningNode();

										TArray<UEdGraphNode*, TInlineAllocator<4>> AllEndNodes;
										const UEdGraphNode* Start = TransitionNode->GetFromPin()->GetOwningNode();
										for (UEdGraphPin* Pin : Start->Pins)
										{
											if (Pin->Direction == EEdGraphPinDirection::EGPD_Output)
											{
												for (UEdGraphPin* LinkTo : Pin->LinkedTo)
												{
													if (UBPNode_GameActionTransitionBase* OtherTransitionNode = Cast<UBPNode_GameActionTransitionBase>(LinkTo->GetOwningNode()))
													{
														if (TransitionNode->IsSameType(OtherTransitionNode))
														{
															AllEndNodes.Add(OtherTransitionNode->GetToPin()->GetOwningNode());
														}
													}
												}
											}
										}
										AllEndNodes.Sort([](const UEdGraphNode& LHS, const UEdGraphNode& RHS) { return LHS.NodePosY < RHS.NodePosY; });

										return FText::AsNumber(AllEndNodes.IndexOfByKey(End));
									}
									return FText::AsNumber(INDEX_NONE);
								})
								.Font(FEditorStyle::GetFontStyle("BTEditor.Graph.BTNode.IndexText"))
							]
						]
					]
				]
			];
	}
	bool RequiresSecondPassLayout() const override { return true; }
	void PerformSecondPassLayout(const TMap< UObject*, TSharedRef<SNode> >& NodeToWidgetLookup) const override
	{
		UBPNode_GameActionTransitionBase* TransitionNode = CastChecked<UBPNode_GameActionTransitionBase>(GraphNode);

		const UEdGraphNode* Start = TransitionNode->GetFromPin()->GetOwningNode();
		const UEdGraphNode* End = TransitionNode->GetToPin()->GetOwningNode();
		const TSharedRef<SNode>* FromWidget = NodeToWidgetLookup.Find(Start);
		const TSharedRef<SNode>* ToWidget = NodeToWidgetLookup.Find(End);
		if (FromWidget && ToWidget)
		{
			const FGeometry StartGeom = FGeometry(FVector2D(Start->NodePosX, Start->NodePosY), FVector2D::ZeroVector, (*FromWidget)->GetDesiredSize(), 1.0f);
			const FGeometry EndGeom = FGeometry(FVector2D(End->NodePosX, End->NodePosY), FVector2D::ZeroVector, (*ToWidget)->GetDesiredSize(), 1.0f);
			PositionBetweenTwoNodesWithOffset(StartGeom, EndGeom, 0, 1);
		}
	}
	TSharedPtr<SToolTip> GetComplexTooltip() override
	{
		return SNew(SToolTip)
			[
				GenerateRichTooltip()
			];
	}
protected:
	void PositionBetweenTwoNodesWithOffset(const FGeometry& StartGeom, const FGeometry& EndGeom, int32 NodeIndex, int32 MaxNodes) const
	{
		// Get a reasonable seed point (halfway between the boxes)
		const FVector2D StartCenter = FGeometryHelper::CenterOf(StartGeom);
		const FVector2D EndCenter = FGeometryHelper::CenterOf(EndGeom);
		const FVector2D SeedPoint = (StartCenter + EndCenter) * 0.5f;

		// Find the (approximate) closest points between the two boxes
		const FVector2D StartAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(StartGeom, SeedPoint);
		const FVector2D EndAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(EndGeom, SeedPoint);

		// Position ourselves halfway along the connecting line between the nodes, elevated away perpendicular to the direction of the line
		const float Height = 30.0f;

		const FVector2D DesiredNodeSize = GetDesiredSize();

		FVector2D DeltaPos(EndAnchorPoint - StartAnchorPoint);

		if (DeltaPos.IsNearlyZero())
		{
			DeltaPos = FVector2D(10.0f, 0.0f);
		}

		const FVector2D Normal = FVector2D(DeltaPos.Y, -DeltaPos.X).GetSafeNormal();

		const FVector2D NewCenter = StartAnchorPoint + (0.5f * DeltaPos) + (Height * Normal);

		FVector2D DeltaNormal = DeltaPos.GetSafeNormal();

		// Calculate node offset in the case of multiple transitions between the same two nodes
		// MultiNodeOffset: the offset where 0 is the centre of the transition, -1 is 1 <size of node>
		// towards the PrevStateNode and +1 is 1 <size of node> towards the NextStateNode.

		const float MutliNodeSpace = 0.2f; // Space between multiple transition nodes (in units of <size of node> )
		const float MultiNodeStep = (1.f + MutliNodeSpace); //Step between node centres (Size of node + size of node spacer)

		const float MultiNodeStart = -((MaxNodes - 1) * MultiNodeStep) / 2.f;
		const float MultiNodeOffset = MultiNodeStart + (NodeIndex * MultiNodeStep);

		// Now we need to adjust the new center by the node size, zoom factor and multi node offset
		const FVector2D NewCorner = NewCenter - (0.5f * DesiredNodeSize) + (DeltaNormal * MultiNodeOffset * DesiredNodeSize.Size());

		GraphNode->NodePosX = NewCorner.X;
		GraphNode->NodePosY = NewCorner.Y;
	}
	TSharedRef<SWidget> GenerateRichTooltip()
	{
		UBPNode_GameActionTransitionBase* TransNode = CastChecked<UBPNode_GameActionTransitionBase>(GraphNode);
		if (TransNode->BoundGraph == NULL)
		{
			return SNew(STextBlock).Text(LOCTEXT("NoAnimGraphBoundToNodeMessage", "Error: No graph"));
		}

		// Find the expression hooked up to the can execute pin of the transition node
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

		TSharedRef<SVerticalBox> Widget = SNew(SVerticalBox);

		const FText TooltipDesc = GetPreviewCornerText();

		// Transition rule linearized
		Widget->AddSlot()
			.AutoHeight()
			.Padding(2.0f)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), TEXT("Graph.TransitionNode.TooltipName"))
			.Text(TooltipDesc)
			];

		if (UGameActionTransitionGraph* TransGraph = Cast<UGameActionTransitionGraph>(TransNode->BoundGraph))
		{
			if (UBPNode_GameActionTransitionResult* ResultNode = TransGraph->ResultNode)
			{
				Widget->AddSlot()
					.AutoHeight()
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), TEXT("Graph.TransitionNode.TooltipRule"))
						.Text(LOCTEXT("AutonomousTransitionRule_ToolTip", "主端跳转规则"))
					];

				Widget->AddSlot()
					.AutoHeight()
					.Padding(2.0f)
					[
						SNew(SKismetLinearExpression, ResultNode->FindPin(GameActionTransitionResultUtils::AutonomousPinName))
					];

				Widget->AddSlot()
					.AutoHeight()
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), TEXT("Graph.TransitionNode.TooltipRule"))
						.Text(LOCTEXT("ServerTransitionRule_ToolTip", "服务器跳转规则"))
					];

				Widget->AddSlot()
					.AutoHeight()
					.Padding(2.0f)
					[
						SNew(SKismetLinearExpression, ResultNode->FindPin(GameActionTransitionResultUtils::ServerPinName))
					];
			}
		}

		return Widget;
	}
	virtual FText GetPreviewCornerText() const { return FText::GetEmpty(); }

	TSharedPtr<SOverlay> TransitionOverlay;
};

void UBPNode_GameActionTransitionBase::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, GameActionNodeUtils::GameActionExecPinCategory, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, GameActionNodeUtils::GameActionExecPinCategory, UEdGraphSchema_K2::PN_Then);
}

void UBPNode_GameActionTransitionBase::PostPlacedNewNode()
{
	const FName TransitionName = GetTransitionName();
	UBlueprint* Blueprint = GetBlueprint();
	BoundGraph = CastChecked<UGameActionTransitionGraph>(FBlueprintEditorUtils::CreateNewGraph(GetBlueprint(), TransitionName, UGameActionTransitionGraph::StaticClass(), UEdGraphSchema_GameActionTransition::StaticClass()));
	BoundGraph->TransitionNode = this;

	Blueprint->Modify();
	Blueprint->FunctionGraphs.Add(BoundGraph);

	using namespace GameActionTransitionResultUtils;
	const FEdGraphPinType BooleanPinType(UEdGraphSchema_K2::PC_Boolean, NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
	
	FEdGraphTerminalType SegmentPinTerminalType;
	SegmentPinTerminalType.bTerminalIsConst = true;
	const FEdGraphPinType SegmentPinType(UEdGraphSchema_K2::PC_Object, NAME_None, UGameActionSegmentBase::StaticClass(), EPinContainerType::None, false, SegmentPinTerminalType);
	
	FGraphNodeCreator<UBPNode_GameActionTransitionResult> ResultTransitionNodeCreator(*BoundGraph);
	UBPNode_GameActionTransitionResult* ResultTransitionNode = ResultTransitionNodeCreator.CreateNode();
	ResultTransitionNodeCreator.Finalize();
	BoundGraph->ResultNode = ResultTransitionNode;
	
	FGraphNodeCreator<UK2Node_FunctionEntry> EntryNodeCreator(*BoundGraph);
	UK2Node_FunctionEntry* EntryNode = EntryNodeCreator.CreateNode();
	EntryNode->CustomGeneratedFunctionName = NAME_None;
	EntryNode->AddExtraFlags(FUNC_Private | FUNC_Const);
	EntryNode->MetaData.Category = LOCTEXT("跳转条件", "跳转条件");
	EntryNodeCreator.Finalize();
	EntryNode->CreateUserDefinedPin(SegmentPinName, SegmentPinType, EGPD_Output, false);
	EntryNode->CreateUserDefinedPin(IsServerPinName, BooleanPinType, EGPD_Output, false);
	EntryNode->CustomGeneratedFunctionName = TransitionName;
	BoundGraph->EntryNode = EntryNode;
	
	FGraphNodeCreator<UK2Node_FunctionResult> AutonomousResultNodeCreator(*BoundGraph);
	UK2Node_FunctionResult* AutonomousResultNode = AutonomousResultNodeCreator.CreateNode();
	AutonomousResultNodeCreator.Finalize();

	FGraphNodeCreator<UK2Node_FunctionResult> ServerResultNodeCreator(*BoundGraph);
	UK2Node_FunctionResult* ServerResultNode = ServerResultNodeCreator.CreateNode();
	ServerResultNodeCreator.Finalize();
	
	AutonomousResultNode->CreateUserDefinedPin(UEdGraphSchema_K2::PN_ReturnValue, BooleanPinType, EGPD_Input, false);
	ServerResultNode->CreateUserDefinedPin(UEdGraphSchema_K2::PN_ReturnValue, BooleanPinType, EGPD_Input, false);

	EntryNode->FindPinChecked(UEdGraphSchema_K2::PN_Then)->MakeLinkTo(ResultTransitionNode->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
	ResultTransitionNode->FindPinChecked(AutonomousThenPinName, EGPD_Output)->MakeLinkTo(AutonomousResultNode->GetExecPin());
	ResultTransitionNode->FindPinChecked(ServerThenPinName, EGPD_Output)->MakeLinkTo(ServerResultNode->GetExecPin());
	ResultTransitionNode->FindPinChecked(AutonomousOutPinName, EGPD_Output)->MakeLinkTo(AutonomousResultNode->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue));
	ResultTransitionNode->FindPinChecked(ServerOutPinName, EGPD_Output)->MakeLinkTo(ServerResultNode->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue));
	ResultTransitionNode->FindPinChecked(IsServerPinName, EGPD_Input)->MakeLinkTo(EntryNode->FindPinChecked(IsServerPinName));
	
	UEdGraph* ParentGraph = GetGraph();
	ParentGraph->Modify();
	ParentGraph->SubGraphs.Add(BoundGraph);
}

void UBPNode_GameActionTransitionBase::DestroyNode()
{
	for (UEdGraphNode* Node : TArray<UEdGraphNode*>(BoundGraph->Nodes))
	{
		Node->DestroyNode();
	}

	UEdGraph* ParentGraph = GetGraph();
	ParentGraph->Modify();
	ParentGraph->SubGraphs.Remove(BoundGraph);
	GetBlueprint()->FunctionGraphs.Remove(BoundGraph);
	BoundGraph->Rename(nullptr, GetTransientPackage());
	
	Super::DestroyNode();
}

void UBPNode_GameActionTransitionBase::PinConnectionListChanged(UEdGraphPin* Pin)
{
	if (Pin->LinkedTo.Num() == 0)
	{
		Modify();

		if (UEdGraph* ParentGraph = GetGraph())
		{
			ParentGraph->Modify();
		}

		DestroyNode();
	}
}

UObject* UBPNode_GameActionTransitionBase::GetJumpTargetForDoubleClick() const
{
	return BoundGraph;
}

void UBPNode_GameActionTransitionBase::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);
}

void UBPNode_GameActionTransitionBase::UpdateBoundGraphName()
{
	BoundGraph->EntryNode->CustomGeneratedFunctionName = GetTransitionName();
}

const FLinearColor UBPNode_GameActionTransitionBase::HoverColor(0.724f, 0.256f, 0.0f, 1.0f);
const FLinearColor UBPNode_GameActionTransitionBase::BaseColor(0.9f, 0.9f, 0.9f, 1.0f);

TSharedPtr<SGraphNode> UBPNode_GameActionTransition::CreateVisualWidget()
{
	// SGraphNodeAnimTransition
	class SGameActionTransitionNode : public SGameActionTransitionNodeBase
	{
		using Super = SGameActionTransitionNodeBase;
	public:
		void Construct(const FArguments& InArgs, UBPNode_GameActionTransition* InNode)
		{
			Super::Construct(InArgs, InNode);
			TransitionNode = InNode;
		}
		void UpdateGraphNode() override
		{
			Super::UpdateGraphNode();

			TransitionOverlay->AddSlot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(0.f)
				.HeightOverride(0.f)
				[
					SNew(SConstraintCanvas)
					.Visibility_Lambda([=]
					{
						return TransitionNode->TransitionType == EGameActionTransitionType::Event ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					+ SConstraintCanvas::Slot()
					.AutoSize(true)
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("Graph.StateNode.Body"))
						.Padding(2.f)
						.BorderBackgroundColor(FLinearColor(0.2f, 0.2f, 0.08f))
						[
							SNew(STextBlock)
							.Text_Lambda([=]
							{
								return FText::FromName(TransitionNode->EventName);
							})
						]
					]
					+ SConstraintCanvas::Slot()
					.AutoSize(true)
					.Offset(FMargin(-14.f, -16.f, 0.f, 0.f))
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("Graph.StateNode.Body"))
						.Padding(2.f)
						.BorderBackgroundColor(FLinearColor(0.2f, 0.08f, 0.08f))
						[
							SNew(STextBlock)
							.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
							.Text(LOCTEXT("事件名", "事件名"))
						]
					]
				]
			];
		}
		FText GetPreviewCornerText() const override
		{
			return TransitionNode->TransitionType == EGameActionTransitionType::Event ? LOCTEXT("事件跳转逻辑", "事件跳转逻辑") : LOCTEXT("Tick跳转逻辑", "Tick跳转逻辑");
		}

		UBPNode_GameActionTransition* TransitionNode;
	};
	return SNew(SGameActionTransitionNode, this);
}

void UBPNode_GameActionTransition::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	
	if (Pin->LinkedTo.Num() != 0)
	{
		UBPNode_GameActionSegmentBase* LinkedSegementNode = CastChecked<UBPNode_GameActionSegmentBase>(Pin->LinkedTo[0]->GetOwningNode());
		if (Pin->Direction == EGPD_Input)
		{
			FromNode = LinkedSegementNode;
		}
		else
		{
			ToNode = LinkedSegementNode;
		}
		if (FindPinChecked(UEdGraphSchema_K2::PN_Execute)->LinkedTo.Num() > 0 && FindPinChecked(UEdGraphSchema_K2::PN_Then)->LinkedTo.Num() > 0)
		{
			UpdateBoundGraphName();
		}
	}
}

FName UBPNode_GameActionTransition::GetTransitionName() const
{
	check(FromNode && ToNode);
	return *FString::Printf(TEXT("%s__%s"), *FromNode->GetRefVarName().ToString(), *ToNode->GetRefVarName().ToString());
}

UObject* UBPNode_GameActionTransition::GetJumpTargetForDoubleClick() const
{
	return BoundGraph;
}

bool UBPNode_GameActionTransition::IsSameType(UBPNode_GameActionTransitionBase* OtherTransitionNode) const
{
	if (UBPNode_GameActionTransition* Transition = Cast<UBPNode_GameActionTransition>(OtherTransitionNode))
	{
		return TransitionType == Transition->TransitionType;
	}
	return false;
}

void UBPNode_GameActionEntryTransition::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();
}

TSharedPtr<SGraphNode> UBPNode_GameActionEntryTransition::CreateVisualWidget()
{
	class SGameActionEntryTransitionNode : public SGameActionTransitionNodeBase
	{
		using Super = SGameActionTransitionNodeBase;
	public:
		void Construct(const FArguments& InArgs, UBPNode_GameActionEntryTransition* InNode)
		{
			Super::Construct(InArgs, InNode);
		}
		FText GetPreviewCornerText() const override
		{
			return LOCTEXT("行为入口逻辑", "行为入口逻辑");
		}
	};
	return SNew(SGameActionEntryTransitionNode, this);
}

FName UBPNode_GameActionEntryTransition::GetTransitionName() const
{
	check(EntryNode && ToNode);

	FStructProperty* Entry = EntryNode->EntryTransitionProperty.Get();
	const FString EntryName = Entry ? Entry->GetDisplayNameText().ToString() : TEXT("Invalid");
	return *FString::Printf(TEXT("%s__%s"), *EntryName, *ToNode->GetRefVarName().ToString());
}

UBPNode_GetOwningSegment::UBPNode_GetOwningSegment()
{
	OwningType = UGameActionSegmentBase::StaticClass();
}

void UBPNode_GetOwningSegment::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, OwningType, UEdGraphSchema_K2::PN_ReturnValue);
}

FText UBPNode_GetOwningSegment::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Get Owning Segment", "Get Owning Segment");
}

FText UBPNode_GetOwningSegment::GetCompactNodeTitle() const
{
	return LOCTEXT("Segment", "Segment");
}

FSlateIcon UBPNode_GetOwningSegment::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = GetNodeTitleColor();
	static FSlateIcon Icon("EditorStyle", "Kismet.AllClasses.FunctionIcon");
	return Icon;
}

bool UBPNode_GetOwningSegment::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
	return TargetGraph->IsA<UGameActionTransitionGraph>();
}

FText UBPNode_GetOwningSegment::GetMenuCategory() const
{
	return LOCTEXT("Game Action", "Game Action");
}

void UBPNode_GetOwningSegment::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda([](UEdGraphNode* NewNode, bool bIsTemplateNode)
		{
			if (bIsTemplateNode == false)
			{
				UBPNode_GetOwningSegment* GetOwningSegment = CastChecked<UBPNode_GetOwningSegment>(NewNode);
				GetOwningSegment->UpdateOwningType();
			}
		});
		
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

void UBPNode_GetOwningSegment::PostPasteNode()
{
	Super::PostPasteNode();

	UpdateOwningType();
}

void UBPNode_GetOwningSegment::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UGameActionTransitionGraph* TransitionGraph = CastChecked<UGameActionTransitionGraph>(GetGraph());
	UEdGraphPin* ParameterPin = TransitionGraph->EntryNode->FindPinChecked(GameActionTransitionResultUtils::SegmentPinName);

	UK2Node_DynamicCast* CastToInterfaceNode = CompilerContext.SpawnIntermediateNode<UK2Node_DynamicCast>(this, SourceGraph);
	CastToInterfaceNode->TargetType = OwningType;
	CastToInterfaceNode->SetPurity(true);
	CastToInterfaceNode->AllocateDefaultPins();
	CastToInterfaceNode->GetCastSourcePin()->MakeLinkTo(ParameterPin);
	CastToInterfaceNode->NotifyPinConnectionListChanged(CastToInterfaceNode->GetCastSourcePin());
	
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue), *CastToInterfaceNode->GetCastResultPin());
}

void UBPNode_GetOwningSegment::UpdateOwningType()
{
	UBPNode_GameActionTransitionBase* TransitionNode = GetTypedOuter<UBPNode_GameActionTransitionBase>();
	UBPNode_GameActionSegmentBase* SegmentNode = Cast<UBPNode_GameActionSegmentBase>(TransitionNode->GetFromPin()->GetOwningNode());
	if (SegmentNode->GameActionSegment)
	{
		OwningType = SegmentNode->GameActionSegment->GetClass();
	}
	else
	{
		OwningType = UGameActionSegmentBase::StaticClass();
	}
}

#undef LOCTEXT_NAMESPACE
