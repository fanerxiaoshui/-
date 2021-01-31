// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <Styling/SlateStyle.h>

/**
 * 
 */
class GAMEACTION_EDITOR_API FGameActionEditorStyle : public FSlateStyleSet
{
public:
	FGameActionEditorStyle();
	~FGameActionEditorStyle();
	
	static FGameActionEditorStyle& Get();
};
