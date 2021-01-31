// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

/**
 * 
 */
class GAMEACTION_EDITOR_API FGameActionSegmentNodeDetails : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShareable(new FGameActionSegmentNodeDetails()); }

    void CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder) override;
};
