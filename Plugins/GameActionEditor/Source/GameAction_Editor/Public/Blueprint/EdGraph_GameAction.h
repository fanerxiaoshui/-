// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph_GameAction.generated.h"

/**
 * 
 */
UCLASS()
class GAMEACTION_EDITOR_API UEdGraph_GameAction : public UEdGraph
{
	GENERATED_BODY()
public:
    UEdGraph_GameAction();

	TArray<FStructProperty*> GetNoEntryNodeProperties() const;
};
