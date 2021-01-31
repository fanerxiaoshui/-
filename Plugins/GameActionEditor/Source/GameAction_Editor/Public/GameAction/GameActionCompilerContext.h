// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <KismetCompiler.h>

class UBPNode_GameActionTransitionBase;
class UGameActionBlueprint;
class UEdGraph_GameAction;
class UBPNode_GameActionSegmentBase;
class UBPNode_GameActionEntry;

/**
 * 
 */
class FActionNodeRootSeacher
{
	TSet<UEdGraphNode*> Visited;
	void SearchImpl(UEdGraphNode* Node);
public:
	FActionNodeRootSeacher() = default;
	FActionNodeRootSeacher(UEdGraph_GameAction* GameActionGraph);

	TArray<UBPNode_GameActionSegmentBase*> ActionNodes;
	TArray<UBPNode_GameActionTransitionBase*> TransitionNodes;
	TArray<UBPNode_GameActionEntry*> Entries;
	void Search(UEdGraph_GameAction* GameActionGraph);
};

class GAMEACTION_EDITOR_API FGameActionCompilerContext : public FKismetCompilerContext
{
	using Super = FKismetCompilerContext;
public:
    FGameActionCompilerContext(UGameActionBlueprint* SourceSketch, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompilerOptions);

    void CreateClassVariablesFromBlueprint() override;
	void CreateFunctionList() override;
	void OnPostCDOCompiled() override;

	FActionNodeRootSeacher ActionNodeRootSeacher;

	struct FTransitionData
	{
		struct FTickData
		{
			FName Condition;
			FName SegmentName;
			int32 Order;
		};
		TArray<FTickData> TickDatas;

		struct FEventData
		{
			FName Event;
			FName Condition;
			FName SegmentName;
			int32 Order;
		};
		TArray<FEventData> EventDatas;
	};
	TMap<FName, FTransitionData> ActionTransitionDatas;
};
