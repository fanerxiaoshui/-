// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <IDetailCustomization.h>
#include <TickableEditorObject.h>
#include <UObject/WeakObjectPtr.h>

class UGameActionSegmentBase;
class SVerticalBox;
class UGameActionInstanceBase;

/**
 * 
 */
class FGameActionInstanceDetails : public IDetailCustomization, public FTickableEditorObject
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShareable(new FGameActionInstanceDetails()); }

	void CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder) override;
private:
	void Tick(float DeltaTime) override;
	TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FGameActionInstanceDetails, STATGROUP_Tickables); }

	TWeakObjectPtr<UGameActionInstanceBase> Instance;
	TWeakObjectPtr<UGameActionSegmentBase> PreActivedSegment;
	TSharedPtr<SVerticalBox> EventList;
};

class FGameActionTransitionNodeBaseDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShareable(new FGameActionTransitionNodeBaseDetails()); }

	void CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder) override;
};

class FGameActionTransitionNodeDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShareable(new FGameActionTransitionNodeDetails()); }

	void CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder) override;
};
