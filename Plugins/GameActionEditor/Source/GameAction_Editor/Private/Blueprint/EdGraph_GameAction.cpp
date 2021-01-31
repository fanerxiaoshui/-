// Fill out your copyright notice in the Description page of Project Settings.


#include "Blueprint/EdGraph_GameAction.h"
#include <Kismet2/BlueprintEditorUtils.h>

#include "Blueprint/BPNode_GameActionEntry.h"
#include "GameAction/GameActionInstance.h"

UEdGraph_GameAction::UEdGraph_GameAction()
{
	bAllowDeletion = false;
	bAllowRenaming = false;
}

TArray<FStructProperty*> UEdGraph_GameAction::GetNoEntryNodeProperties() const
{
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(this);
	check(Blueprint);
	TArray<FStructProperty*> AllEntryProperties;
	for (TFieldIterator<FStructProperty> It(Blueprint->GeneratedClass); It; ++It)
	{
		if (It->Struct->IsChildOf(StaticStruct<FGameActionEntry>()))
		{
			AllEntryProperties.Add(*It);
		}
	}
	for (UEdGraphNode* Node : Nodes)
	{
		if (UBPNode_GameActionEntry* GameActionEntry = Cast<UBPNode_GameActionEntry>(Node))
		{
			AllEntryProperties.Remove(GameActionEntry->EntryTransitionProperty.Get());
		}
	}
	return AllEntryProperties;
}
