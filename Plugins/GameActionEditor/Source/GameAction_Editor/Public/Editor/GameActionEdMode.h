// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"

class FGameActionEditScene;

/**
 * 
 */
class GAMEACTION_EDITOR_API IGameActionEdMode : public FEdMode
{
public:
	TWeakPtr<FGameActionEditScene> EditScene;
};

class GAMEACTION_EDITOR_API FGameActionDefaultEdMode : public IGameActionEdMode
{
public:
	static const FName ModeId;

	bool UsesPropertyWidgets() const override { return true; }
	UObject* GetItemToTryDisplayingWidgetsFor(FTransform& OutWidgetToWorld) const override;
};
