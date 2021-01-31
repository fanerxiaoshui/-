// Fill out your copyright notice in the Description page of Project Settings.


#include "Editor/GameActionEdMode.h"

#include "Editor/GameActionPreviewScene.h"

const FName FGameActionDefaultEdMode::ModeId = TEXT("GameActionDefaultEdMode");

UObject* FGameActionDefaultEdMode::GetItemToTryDisplayingWidgetsFor(FTransform& OutWidgetToWorld) const
{
	return EditScene.IsValid() ? EditScene.Pin()->GetItemToTryDisplayingWidgetsFor(OutWidgetToWorld) : nullptr;
}
