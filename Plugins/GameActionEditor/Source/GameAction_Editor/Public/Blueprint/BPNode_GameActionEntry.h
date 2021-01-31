// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "K2Node_Event.h"
#include "BPNode_GameActionEntry.generated.h"

/**
 * 
 */
UCLASS()
class GAMEACTION_EDITOR_API UBPNode_GameActionEntry : public UK2Node_Event
{
	GENERATED_BODY()
public:
	UBPNode_GameActionEntry();
	
	bool CanUserDeleteNode() const override { return EntryTransitionProperty.Get() == nullptr; }
	bool CanDuplicateNode() const override { return false; }
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FText GetTooltipText() const override;
	FText GetMenuCategory() const override;
	void AllocateDefaultPins() override;
	void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	TSharedPtr<SGraphNode> CreateVisualWidget() override;

	void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;
	bool IsActionFilteredOut(class FBlueprintActionFilter const& Filter) override;

	UPROPERTY()
	TFieldPath<FStructProperty> EntryTransitionProperty;
};
