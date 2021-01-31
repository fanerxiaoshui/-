// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "BPNode_SequenceTimeTestingNode.generated.h"

class UGameActionTimeTestingSection;
class UBPNode_GameActionSegmentBase;

/**
 * 
 */
UCLASS()
class GAMEACTION_EDITOR_API UBPNode_SequenceTimeTestingNode : public UK2Node
{
	GENERATED_BODY()
public:
	UBPNode_SequenceTimeTestingNode();

	bool CanDuplicateNode() const override { return false; }
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	void OnRenameNode(const FString& NewName) override;
	void AllocateDefaultPins() override;
	void PostPlacedNewNode() override;
	void DestroyNode() override;
	bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;
	void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	bool IsNodePure() const override { return true; }
	FText GetMenuCategory() const override;
	void PinConnectionListChanged(UEdGraphPin* Pin) override;
	void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	bool IsActionFilteredOut(class FBlueprintActionFilter const& Filter) override;

	UPROPERTY()
	FString DisplayName;
	UPROPERTY()
	UGameActionTimeTestingSection* TestingSection = nullptr;
	UPROPERTY()
	UBPNode_GameActionSegmentBase* GameActionSegmentNode = nullptr;
};
