// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_GameAction.generated.h"

/**
 * 
 */
UCLASS()
class GAMEACTION_EDITOR_API UEdGraphSchema_GameAction : public UEdGraphSchema_K2
{
	GENERATED_BODY()
public:
	const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;
	bool CreateAutomaticConversionNodeAndConnections(UEdGraphPin* A, UEdGraphPin* B) const override;
	bool TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const override;
	class FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj) const override;
	void OnPinConnectionDoubleCicked(UEdGraphPin* PinA, UEdGraphPin* PinB, const FVector2D& GraphPosition) const override;
};
