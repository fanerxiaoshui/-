// Fill out your copyright notice in the Description page of Project Settings.


#include "Blueprint/EdGraphSchema_GameAction.h"
#include <ToolMenu.h>
#include <ToolMenuSection.h>
#include <GraphEditorActions.h>
#include <Framework/MultiBox/MultiBoxBuilder.h>
#include <Framework/Commands/GenericCommands.h>
#include <Kismet2/BlueprintEditorUtils.h>
#include <Kismet2/KismetEditorUtilities.h>
#include <EdGraphNode_Comment.h>
#include <EdGraph/EdGraphSchema.h>
#include <EdGraphSchema_K2_Actions.h>
#include <BlueprintConnectionDrawingPolicy.h>
#include <Rendering/DrawElements.h>

#include "GameAction/GameActionSegment.h"
#include "Blueprint/BPNode_GameActionSegment.h"
#include "Blueprint/BPNode_GameActionTransition.h"
#include "Blueprint/BPNode_GameActionEntry.h"

#define LOCTEXT_NAMESPACE "EdGraphSchema_GameAction"

const FPinConnectionResponse UEdGraphSchema_GameAction::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	if (A && B && A->PinType.PinSubCategory == GameActionNodeUtils::GameActionExecPinCategory && B->PinType.PinSubCategory == GameActionNodeUtils::GameActionExecPinCategory)
	{
		const UEdGraphPin* InputPin = A->Direction == EEdGraphPinDirection::EGPD_Input ? A : B;
		const UEdGraphPin* OutputPin = A->Direction == EEdGraphPinDirection::EGPD_Output ? A : B;
		const UEdGraphNode* InputNode = InputPin->GetOwningNode();
		const UEdGraphNode* OutputNode = OutputPin->GetOwningNode();
		if (InputNode == OutputNode)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT(""));
		}
		if ((OutputNode->IsA<UBPNode_GameActionSegmentBase>() || OutputNode->IsA<UBPNode_GameActionEntry>()) && InputNode->IsA<UBPNode_GameActionSegmentBase>())
		{
			if (OutputPin->LinkedTo.ContainsByPredicate([&](UEdGraphPin* E) { return E->GetOwningNode()->FindPinChecked(PN_Then)->LinkedTo[0]->GetOwningNode() == InputNode; }))
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("已经存在行为节点间跳转逻辑，不可再添加", "已经存在行为节点间跳转逻辑，不可再添加"));
			}
			else
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE, LOCTEXT("添加行为节点间跳转逻辑", "添加行为节点间跳转逻辑"));
			}
		}
		else if (OutputNode->IsA<UBPNode_GameActionSegmentBase>() && InputNode->IsA<UBPNode_GameActionEntry>())
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("不可连接起始事件节点", "不可连接起始事件节点"));
		}
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, TEXT(""));
	}
	return Super::CanCreateConnection(A, B);
}

bool UEdGraphSchema_GameAction::CreateAutomaticConversionNodeAndConnections(UEdGraphPin* A, UEdGraphPin* B) const
{
	if (A && B && A->PinType.PinSubCategory == GameActionNodeUtils::GameActionExecPinCategory && B->PinType.PinSubCategory == GameActionNodeUtils::GameActionExecPinCategory)
	{
		if (ensureAlways(A->Direction != B->Direction) == false)
		{
			return false;
		}
		UEdGraphPin* InputPin = A->Direction == EEdGraphPinDirection::EGPD_Input ? A : B;
		UEdGraphPin* OutputPin = A->Direction == EEdGraphPinDirection::EGPD_Output ? A : B;
		UEdGraphNode* InputNode = InputPin->GetOwningNode();
		UEdGraphNode* OutputNode = OutputPin->GetOwningNode();
		
		const FVector2D NodePos{ (InputNode->NodePosX + OutputNode->NodePosX) / 2.f,  (InputNode->NodePosY + OutputNode->NodePosY) / 2.f };
		if (OutputNode->IsA<UBPNode_GameActionSegmentBase>() && InputNode->IsA<UBPNode_GameActionSegmentBase>())
		{
			UBPNode_GameActionTransition* ConversionNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UBPNode_GameActionTransition>(InputPin->GetOwningNode()->GetGraph(), NodePos, EK2NewNodeFlags::None, [&](UBPNode_GameActionTransition* TransitionNode)
			{
				TransitionNode->FromNode = CastChecked<UBPNode_GameActionSegmentBase>(OutputPin->GetOwningNode());
				TransitionNode->ToNode = CastChecked<UBPNode_GameActionSegmentBase>(InputPin->GetOwningNode());
			});
			AutowireConversionNode(InputPin, OutputPin, ConversionNode);
			return true;
		}
		else if (OutputNode->IsA<UBPNode_GameActionEntry>() && InputNode->IsA<UBPNode_GameActionSegmentBase>())
		{
			UBPNode_GameActionEntryTransition* ConversionNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UBPNode_GameActionEntryTransition>(InputPin->GetOwningNode()->GetGraph(), NodePos, EK2NewNodeFlags::None, [&](UBPNode_GameActionEntryTransition* TransitionEntryNode)
			{
				TransitionEntryNode->EntryNode = CastChecked<UBPNode_GameActionEntry>(OutputPin->GetOwningNode());
				TransitionEntryNode->ToNode = CastChecked<UBPNode_GameActionSegmentBase>(InputPin->GetOwningNode());
			});
			AutowireConversionNode(InputPin, OutputPin, ConversionNode);
			return true;
		}
	}
	return Super::CreateAutomaticConversionNodeAndConnections(A, B);
}

bool UEdGraphSchema_GameAction::TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const
{
	// BPNode_GameAction节点连接时自动调整连接引脚
	if (A && B && A->PinType.PinSubCategory == GameActionNodeUtils::GameActionExecPinCategory && B->PinType.PinSubCategory == GameActionNodeUtils::GameActionExecPinCategory)
	{
		UEdGraphNode* NodeA = A->GetOwningNode();
		UEdGraphNode* NodeB = B->GetOwningNode();
		if (NodeA != NodeB)
		{
			if (NodeA->IsA<UBPNode_GameActionSegmentBase>() && NodeB->IsA<UBPNode_GameActionSegmentBase>())
			{
				UEdGraphPin* OutputPin = A->Direction == EEdGraphPinDirection::EGPD_Output ? A : NodeA->FindPinChecked(PN_Then);
				UEdGraphPin* InputPin = B->Direction == EEdGraphPinDirection::EGPD_Input ? B : NodeB->FindPinChecked(PN_Execute);
				return Super::TryCreateConnection(OutputPin, InputPin);
			}
			else if (NodeA->IsA<UBPNode_GameActionEntry>())
			{
				UEdGraphPin* InputPin = B->Direction == EEdGraphPinDirection::EGPD_Input ? B : NodeB->FindPinChecked(PN_Execute);
				return Super::TryCreateConnection(A, InputPin);
			}
		}
	}
	return Super::TryCreateConnection(A, B);
}

class FConnectionDrawingPolicy* UEdGraphSchema_GameAction::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj) const
{
	//FConnectionParams 中bUserFlag1代表绘制双向箭头
	class FConnectionDrawingPolicy_GameAction : public FKismetConnectionDrawingPolicy
	{
		using Super = FKismetConnectionDrawingPolicy;
	protected:
		UEdGraph* GraphObj;
		TMap<UEdGraphNode*, int32> NodeWidgetMap;

		uint8 bInGameActionDraw : 1;
	public:
		FConnectionDrawingPolicy_GameAction(int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
			:Super(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements, InGraphObj), GraphObj(InGraphObj), bInGameActionDraw(false)
		{
			
		}

		void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params) override
		{
			if ((OutputPin && OutputPin->PinType.PinSubCategory == GameActionNodeUtils::GameActionExecPinCategory) || (InputPin && InputPin->PinType.PinSubCategory == GameActionNodeUtils::GameActionExecPinCategory))
			{
				Params.AssociatedPin1 = OutputPin;
				Params.AssociatedPin2 = InputPin;
				Params.WireThickness = 1.5f;

				Params.WireColor = UBPNode_GameActionTransition::BaseColor;
				Params.bUserFlag2 = true;

				const bool bDeemphasizeUnhoveredPins = HoveredPins.Num() > 0;
				if (bDeemphasizeUnhoveredPins)
				{
					ApplyHoverDeemphasis(OutputPin, InputPin, /*inout*/ Params.WireThickness, /*inout*/ Params.WireColor);
				}
			}
			else
			{
				Params.bUserFlag2 = false;
				Super::DetermineWiringStyle(OutputPin, InputPin, Params);
			}
		}
		void Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes) override
		{
			// Build an acceleration structure to quickly find geometry for the nodes
			NodeWidgetMap.Empty();
			for (int32 NodeIndex = 0; NodeIndex < ArrangedNodes.Num(); ++NodeIndex)
			{
				FArrangedWidget& CurWidget = ArrangedNodes[NodeIndex];
				TSharedRef<SGraphNode> ChildNode = StaticCastSharedRef<SGraphNode>(CurWidget.Widget);
				NodeWidgetMap.Add(ChildNode->GetNodeObj(), NodeIndex);
			}

			// Now draw
			Super::Draw(InPinGeometries, ArrangedNodes);
		}
		void DrawSplineWithArrow(const FGeometry& StartGeom, const FGeometry& EndGeom, const FConnectionParams& Params) override
		{
			if (Params.bUserFlag2)
			{
				// Get a reasonable seed point (halfway between the boxes)
				const FVector2D StartCenter = FGeometryHelper::CenterOf(StartGeom);
				const FVector2D EndCenter = FGeometryHelper::CenterOf(EndGeom);
				const FVector2D SeedPoint = (StartCenter + EndCenter) * 0.5f;

				// Find the (approximate) closest points between the two boxes
				const FVector2D StartAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(StartGeom, SeedPoint);
				const FVector2D EndAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(EndGeom, SeedPoint);

				DrawSplineWithArrow(StartAnchorPoint, EndAnchorPoint, Params);
			}
			else
			{
				Super::DrawSplineWithArrow(StartGeom, EndGeom, Params);
			}
		}
		void DrawSplineWithArrow(const FVector2D& StartAnchorPoint, const FVector2D& EndAnchorPoint, const FConnectionParams& Params) override
		{
			if (Params.bUserFlag2)
			{
				FGuardValue_Bitfield(bInGameActionDraw, bInGameActionDraw);
				bInGameActionDraw = true;
				Internal_DrawLineWithArrow(StartAnchorPoint, EndAnchorPoint, Params);

				if (Params.bUserFlag1)
				{
					Internal_DrawLineWithArrow(EndAnchorPoint, StartAnchorPoint, Params);
				}
			}
			else
			{
				Super::DrawSplineWithArrow(StartAnchorPoint, EndAnchorPoint, Params);
			}
		}
		void DrawPreviewConnector(const FGeometry& PinGeometry, const FVector2D& StartPoint, const FVector2D& EndPoint, UEdGraphPin* Pin) override
		{
			if (Pin && Pin->PinType.PinSubCategory == GameActionNodeUtils::GameActionExecPinCategory)
			{
				FConnectionParams Params;
				DetermineWiringStyle(Pin, nullptr, /*inout*/ Params);

				const FVector2D SeedPoint = EndPoint;
				const FVector2D AdjustedStartPoint = FGeometryHelper::FindClosestPointOnGeom(PinGeometry, SeedPoint);

				DrawSplineWithArrow(AdjustedStartPoint, EndPoint, Params);
			}
			else
			{
				Super::DrawPreviewConnector(PinGeometry, StartPoint, EndPoint, Pin);
			}
		}
		FVector2D ComputeSplineTangent(const FVector2D& Start, const FVector2D& End) const override
		{
			if (bInGameActionDraw)
			{
				const FVector2D Delta = End - Start;
				const FVector2D NormDelta = Delta.GetSafeNormal();
				return NormDelta;
			}
			return Super::ComputeSplineTangent(Start, End);
		}
		void DetermineLinkGeometry(FArrangedChildren& ArrangedNodes, TSharedRef<SWidget>& OutputPinWidget, UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FArrangedWidget*& StartWidgetGeometry, FArrangedWidget*& EndWidgetGeometry) override
		{
			if ((OutputPin && OutputPin->PinType.PinSubCategory == GameActionNodeUtils::GameActionExecPinCategory) || (InputPin && InputPin->PinType.PinSubCategory == GameActionNodeUtils::GameActionExecPinCategory))
			{
				if (UBPNode_GameActionTransitionBase* TransNode = Cast<UBPNode_GameActionTransitionBase>(InputPin->GetOwningNode()))
				{
					UEdGraphNode* PrevNode = TransNode->GetFromPin()->GetOwningNode();
					UEdGraphNode* NextNode = TransNode->GetToPin()->GetOwningNode();
					if ((PrevNode != nullptr) && (NextNode != nullptr))
					{
						int32* PrevNodeIndex = NodeWidgetMap.Find(PrevNode);
						int32* NextNodeIndex = NodeWidgetMap.Find(NextNode);
						if ((PrevNodeIndex != nullptr) && (NextNodeIndex != nullptr))
						{
							StartWidgetGeometry = &(ArrangedNodes[*PrevNodeIndex]);
							EndWidgetGeometry = &(ArrangedNodes[*NextNodeIndex]);
						}
					}
				}
				else
				{
					StartWidgetGeometry = PinGeometries->Find(OutputPinWidget);

					if (TSharedPtr<SGraphPin>* pTargetWidget = PinToPinWidgetMap.Find(InputPin))
					{
						TSharedRef<SGraphPin> InputWidget = (*pTargetWidget).ToSharedRef();
						EndWidgetGeometry = PinGeometries->Find(InputWidget);
					}
				}
			}
			else
			{
				Super::DetermineLinkGeometry(ArrangedNodes, OutputPinWidget, OutputPin, InputPin, StartWidgetGeometry, EndWidgetGeometry);
			}
		}
	protected:
		void Internal_DrawLineWithArrow(const FVector2D& StartAnchorPoint, const FVector2D& EndAnchorPoint, const FConnectionParams& Params)
		{
			static const FSlateBrush* CachedArrowImage = FEditorStyle::GetBrush(TEXT("Graph.Arrow"));
			TGuardValue<const FSlateBrush*> ArrowImageGurad(ArrowImage, CachedArrowImage);
			TGuardValue<FVector2D> ArrowRadiusGurad(ArrowRadius, ArrowImage->ImageSize * ZoomFactor * 0.5f);

			const float LineSeparationAmount = 4.5f;

			const FVector2D DeltaPos = EndAnchorPoint - StartAnchorPoint;
			const FVector2D UnitDelta = DeltaPos.GetSafeNormal();
			const FVector2D Normal = FVector2D(DeltaPos.Y, -DeltaPos.X).GetSafeNormal();

			// Come up with the final start/end points
			const FVector2D DirectionBias = Normal * LineSeparationAmount;
			const FVector2D LengthBias = ArrowRadius.X * UnitDelta;
			const FVector2D StartPoint = StartAnchorPoint + DirectionBias + LengthBias;
			const FVector2D EndPoint = EndAnchorPoint + DirectionBias - LengthBias;

			// Draw a line/spline
			DrawConnection(WireLayerID, StartPoint, EndPoint, Params);

			// Draw the arrow
			const FVector2D ArrowDrawPos = EndPoint - ArrowRadius;
			const float AngleInRadians = FMath::Atan2(DeltaPos.Y, DeltaPos.X);

			FSlateDrawElement::MakeRotatedBox(
				DrawElementsList,
				ArrowLayerID,
				FPaintGeometry(ArrowDrawPos, ArrowImage->ImageSize * ZoomFactor, ZoomFactor),
				ArrowImage,
				ESlateDrawEffect::None,
				AngleInRadians,
				TOptional<FVector2D>(),
				FSlateDrawElement::RelativeToElement,
				Params.WireColor
			);
		}
	};
	return new FConnectionDrawingPolicy_GameAction(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
}

void UEdGraphSchema_GameAction::OnPinConnectionDoubleCicked(UEdGraphPin* PinA, UEdGraphPin* PinB, const FVector2D& GraphPosition) const
{
	const bool bIsGameActionExecPin = PinA && PinB && PinA->PinType.PinSubCategory == GameActionNodeUtils::GameActionExecPinCategory && PinB->PinType.PinSubCategory == GameActionNodeUtils::GameActionExecPinCategory;
	if (bIsGameActionExecPin == false)
	{
		Super::OnPinConnectionDoubleCicked(PinA, PinB, GraphPosition);
	}
}

#undef LOCTEXT_NAMESPACE
