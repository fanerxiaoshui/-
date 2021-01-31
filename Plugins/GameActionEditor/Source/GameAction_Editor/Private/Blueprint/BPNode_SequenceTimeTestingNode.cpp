// Fill out your copyright notice in the Description page of Project Settings.


#include "Blueprint/BPNode_SequenceTimeTestingNode.h"
#include <BlueprintActionDatabaseRegistrar.h>
#include <BlueprintNodeSpawner.h>
#include <EdGraphSchema_K2.h>
#include <K2Node_CallFunction.h>
#include <MovieScene.h>
#include <ISequencer.h>
#include <KismetCompiler.h>
#include <UObject/PropertyPortFlags.h>

#include "Blueprint/BPNode_GameActionTransition.h"
#include "Blueprint/BPNode_GameActionSegment.h"
#include "Blueprint/GameActionBlueprint.h"
#include "GameAction/GameActionSegment.h"
#include "Sequence/GameActionSequence.h"
#include "Sequence/GameActionTimeTestingTrack.h"
#include "Sequencer/GameActionSequencer.h"

#define LOCTEXT_NAMESPACE "BPNode_SequenceTimeNode"

UBPNode_SequenceTimeTestingNode::UBPNode_SequenceTimeTestingNode()
{
	bCanRenameNode = true;
}

FText UBPNode_SequenceTimeTestingNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::FullTitle)
	{
		return FText::Format(LOCTEXT("SequenceTimeNodeFullTitle", "[{0}]\nIs In Sequence Range"), FText::FromString(DisplayName));
	}
	else if (TitleType == ENodeTitleType::EditableTitle)
	{
		return FText::FromString(DisplayName);
	}
	return LOCTEXT("SequenceTimeNodeTitle", "Is In Sequence Range");
}

void UBPNode_SequenceTimeTestingNode::OnRenameNode(const FString& NewName)
{
	DisplayName = NewName;
}

void UBPNode_SequenceTimeTestingNode::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, UEdGraphSchema_K2::PN_ReturnValue);
}

void UBPNode_SequenceTimeTestingNode::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();

	UGameActionTransitionGraph* ActionTransitionGraph = CastChecked<UGameActionTransitionGraph>(GetGraph());
	GameActionSegmentNode = CastChecked<UBPNode_GameActionSegmentBase>(ActionTransitionGraph->TransitionNode->GetFromPin()->GetOwningNode());
	DisplayName = ActionTransitionGraph->GetName();

	if (UGameActionSequence* GameActionSequence = GameActionSegmentNode->GetGameActionSequence())
	{
		check(GameActionSequence);

		UMovieScene* MovieScene = GameActionSequence->GetMovieScene();
		UGameActionTimeTestingTrack* TimeTestingTrack = MovieScene->FindMasterTrack<UGameActionTimeTestingTrack>();
		if (TimeTestingTrack == nullptr)
		{
			TimeTestingTrack = MovieScene->AddMasterTrack<UGameActionTimeTestingTrack>();
		}

		int32 MaxRowIndex = -1;
		for (UMovieSceneSection* ExistSection : TimeTestingTrack->GetAllSections())
		{
			if (ExistSection->GetRowIndex() > MaxRowIndex)
			{
				MaxRowIndex = ExistSection->GetRowIndex();
			}
		}
		TestingSection = NewObject<UGameActionTimeTestingSection>(TimeTestingTrack, NAME_None, RF_Transactional);
		TestingSection->SetRange(TRange<FFrameNumber>(0, MovieScene->GetTickResolution().AsFrameNumber(1.0)));
		TestingSection->SetRowIndex(MaxRowIndex + 1);
		TestingSection->TimeTestingNode = this;
		TestingSection->bIsEnable = FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue)->LinkedTo.Num() != 0;

		TimeTestingTrack->AddSection(*TestingSection);
		TimeTestingTrack->UpdateEasing();

		UGameActionBlueprint* GameActionBlueprint = GetTypedOuter<UGameActionBlueprint>();
		if (GameActionBlueprint->PreviewSequencer.IsValid())
		{
			if (ISequencer* Sequencer = GameActionBlueprint->PreviewSequencer.Pin()->PreviewSequencer.Get())
			{
				Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
			}
		}
	}
}

void UBPNode_SequenceTimeTestingNode::DestroyNode()
{
	Super::DestroyNode();

	if (TestingSection == nullptr || GameActionSegmentNode == nullptr)
	{
		return;
	}
	if (UGameActionSequence* GameActionSequence = GameActionSegmentNode->GetGameActionSequence())
	{
		check(GameActionSequence);

		UMovieScene* MovieScene = GameActionSequence->GetMovieScene();
		MovieScene->Modify();
		if (UGameActionTimeTestingTrack* TimeTestingTrack = MovieScene->FindMasterTrack<UGameActionTimeTestingTrack>())
		{
			TimeTestingTrack->RemoveSection(*TestingSection);
			if (TimeTestingTrack->GetAllSections().Num() == 0)
			{
				MovieScene->RemoveMasterTrack(*TimeTestingTrack);
			}
		}

		UGameActionBlueprint* GameActionBlueprint = GetTypedOuter<UGameActionBlueprint>();
		if (GameActionBlueprint->PreviewSequencer.IsValid())
		{
			if (ISequencer* Sequencer = GameActionBlueprint->PreviewSequencer.Pin()->PreviewSequencer.Get())
			{
				Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemRemoved);
			}
		}
	}
}

bool UBPNode_SequenceTimeTestingNode::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
	return TargetGraph->GetClass() == UGameActionTransitionGraph::StaticClass();
}

void UBPNode_SequenceTimeTestingNode::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FSlateIcon UBPNode_SequenceTimeTestingNode::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = GetNodeTitleColor();
	static FSlateIcon Icon("EditorStyle", "Kismet.AllClasses.FunctionIcon");
	return Icon;
}

FText UBPNode_SequenceTimeTestingNode::GetMenuCategory() const
{
	return LOCTEXT("GameAction", "GameAction");
}

void UBPNode_SequenceTimeTestingNode::PinConnectionListChanged(UEdGraphPin* Pin)
{
	if (TestingSection && Pin->PinName == UEdGraphSchema_K2::PN_ReturnValue)
	{
		TestingSection->bIsEnable = Pin->LinkedTo.Num() != 0;
	}
}

void UBPNode_SequenceTimeTestingNode::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	if (::IsValid(TestingSection) == false || TestingSection->GetTypedOuter<UGameActionTimeTestingTrack>()->HasSection(*TestingSection) == false)
	{
		CompilerContext.MessageLog.Error(TEXT("@@ 对应的条件跳转片段不存在"), this);
		return;
	}

	UEdGraphPin* ReturnPin = FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);

	UK2Node_CallFunction* CallIsInSequenceTimeNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallIsInSequenceTimeNode->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UGameActionSegment, IsInSequenceTime), UGameActionSegment::StaticClass());
	CallIsInSequenceTimeNode->AllocateDefaultPins();

	UBPNode_GetOwningSegment* GetOwningSegmentNode = CompilerContext.SpawnIntermediateNode<UBPNode_GetOwningSegment>(this, SourceGraph);
	GetOwningSegmentNode->OwningType = UGameActionSegment::StaticClass();
	GetOwningSegmentNode->AllocateDefaultPins();
	CallIsInSequenceTimeNode->FindPinChecked(UEdGraphSchema_K2::PN_Self)->MakeLinkTo(GetOwningSegmentNode->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue));
	
	const TRange<FFrameNumber> Range = TestingSection->GetRange();
	FString LowerValue;
	TBaseStructure<FFrameNumber>::Get()->ExportText(LowerValue, &Range.GetLowerBoundValue(), nullptr, nullptr, PPF_None, nullptr);
 	CallIsInSequenceTimeNode->FindPinChecked(TEXT("Lower"))->DefaultValue = LowerValue;
	FString UpperValue;
	TBaseStructure<FFrameNumber>::Get()->ExportText(UpperValue, &Range.GetUpperBoundValue(), nullptr, nullptr, PPF_None, nullptr);
 	CallIsInSequenceTimeNode->FindPinChecked(TEXT("Upper"))->DefaultValue = UpperValue;
	
	CompilerContext.MovePinLinksToIntermediate(*ReturnPin, *CallIsInSequenceTimeNode->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue));
}

bool UBPNode_SequenceTimeTestingNode::IsActionFilteredOut(class FBlueprintActionFilter const& Filter)
{
	for (UEdGraph* Graph : Filter.Context.Graphs)
	{
		if (UGameActionTransitionGraph* GameActionTransitionGraph = Cast<UGameActionTransitionGraph>(Graph))
		{
			if (UBPNode_GameActionTransitionBase* Transition = GameActionTransitionGraph->TransitionNode)
			{
				if (UBPNode_GameActionSegmentBase* FromGameActionSegmentNode = Cast<UBPNode_GameActionSegmentBase>(Transition->GetFromPin()->GetOwningNode()))
				{
					if (FromGameActionSegmentNode->GetGameActionSequence() != nullptr)
					{
						return false;
					}
				}
			}
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
