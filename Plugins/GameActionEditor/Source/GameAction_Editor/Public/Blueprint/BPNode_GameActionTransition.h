// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <K2Node_Tunnel.h>
#include <EdGraph/EdGraph.h>
#include <EdGraphSchema_K2.h>
#include <EdGraphUtilities.h>
#include "BPNode_GameActionTransition.generated.h"

/**
 * 
 */
class UGameActionSegmentBase;
class UBPNode_GameActionSegmentBase;

namespace GameActionTransitionResultUtils
{
	const FName SegmentPinName = TEXT("Segment");
	const FName IsServerPinName = TEXT("IsServerPinName");
	const FName AutonomousThenPinName = TEXT("AutonomousThen");
	const FName ServerThenPinName = TEXT("ServerThen");
	const FName AutonomousOutPinName = TEXT("AutonomousResultOut");
	const FName ServerOutPinName = TEXT("ServerResultOut");
	const FName AutonomousPinName = TEXT("AutonomousResult");
	const FName ServerPinName = TEXT("ServerResult");
}

struct FGameActionGraphNodeFactory : public FGraphPanelNodeFactory
{
	TSharedPtr<class SGraphNode> CreateNode(class UEdGraphNode* InNode) const override;
};

UCLASS(MinimalAPI)
class UBPNode_GameActionTransitionResult : public UK2Node
{
	GENERATED_BODY()

public:
	// UEdGraphNode interface
	bool CanUserDeleteNode() const override { return false; }
	bool CanDuplicateNode() const override { return false; }
	FLinearColor GetNodeTitleColor() const override { return FLinearColor(1.0f, 0.65f, 0.4f, 1.0f); }
	FText GetTooltipText() const override;
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	void AllocateDefaultPins() override;
	TSharedPtr<SGraphNode> CreateVisualWidget() override;
	void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
};

UCLASS()
class UGameActionTransitionGraph : public UEdGraph
{
	GENERATED_BODY()
public:
    UGameActionTransitionGraph()
    {
		bAllowDeletion = false;
		bAllowRenaming = false;
    }
	UPROPERTY()
	class UK2Node_FunctionEntry* EntryNode = nullptr;

	UPROPERTY()
	UBPNode_GameActionTransitionResult* ResultNode = nullptr;

	UPROPERTY()
	class UBPNode_GameActionTransitionBase* TransitionNode = nullptr;
};

UCLASS()
class GAMEACTION_EDITOR_API UEdGraphSchema_GameActionTransition : public UEdGraphSchema_K2
{
	GENERATED_BODY()
public:
    EGraphType GetGraphType(const UEdGraph* TestEdGraph) const override { return GT_Function; }
};

UENUM()
enum class EGameActionTransitionType : uint8
{
	Tick UMETA(DisplayName = "Tick判断"),
	Event UMETA(DisplayName = "事件触发")
};

UCLASS()
class GAMEACTION_EDITOR_API UBPNode_GameActionTransitionBase : public UK2Node
{
    GENERATED_BODY()
public:
    bool CanUserDeleteNode() const override { return true; }
    bool CanDuplicateNode() const override { return false; }
    void AllocateDefaultPins() override;
    void PostPlacedNewNode() override;
    void DestroyNode() override;
    void PinConnectionListChanged(UEdGraphPin* Pin) override;
    bool ShouldShowNodeProperties() const override { return true; }

    UObject* GetJumpTargetForDoubleClick() const override;
    void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

    UPROPERTY()
    UGameActionTransitionGraph* BoundGraph = nullptr;

    UEdGraphPin* GetFromPin() const { return FindPinChecked(UEdGraphSchema_K2::PN_Execute)->LinkedTo[0]; }
    UEdGraphPin* GetToPin() const { return FindPinChecked(UEdGraphSchema_K2::PN_Then)->LinkedTo[0]; }
	virtual FName GetTransitionName() const { return NAME_None; }
	void UpdateBoundGraphName();
public:
    const static FLinearColor HoverColor;
	const static FLinearColor BaseColor;

	virtual bool IsSameType(UBPNode_GameActionTransitionBase* OtherTransitionNode) const { return GetClass() == OtherTransitionNode->GetClass(); }
};

UCLASS()
class GAMEACTION_EDITOR_API UBPNode_GameActionTransition : public UBPNode_GameActionTransitionBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "配置", meta = (DisplayName = "跳转条件类型"))
    EGameActionTransitionType TransitionType;
    UPROPERTY(EditAnywhere, Category = "配置", meta = (DisplayName = "事件名", EditCondition = "TransitionType == EGameActionTransitionType::Event"))
	FName EventName;
	
	UPROPERTY()
	UBPNode_GameActionSegmentBase* FromNode = nullptr;
	UPROPERTY()
	UBPNode_GameActionSegmentBase* ToNode = nullptr;

	TSharedPtr<SGraphNode> CreateVisualWidget() override;
	void PinConnectionListChanged(UEdGraphPin* Pin) override;
	FName GetTransitionName() const override;
	UObject* GetJumpTargetForDoubleClick() const override;

	bool IsSameType(UBPNode_GameActionTransitionBase* OtherTransitionNode) const override;
};

UCLASS()
class GAMEACTION_EDITOR_API UBPNode_GameActionEntryTransition : public UBPNode_GameActionTransitionBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	class UBPNode_GameActionEntry* EntryNode = nullptr;
	UPROPERTY()
	UBPNode_GameActionSegmentBase* ToNode = nullptr;

	void PostPlacedNewNode() override;
	TSharedPtr<SGraphNode> CreateVisualWidget() override;
	FName GetTransitionName() const override;
};

UCLASS()
class GAMEACTION_EDITOR_API UBPNode_GetOwningSegment : public UK2Node
{
	GENERATED_BODY()
public:
	UBPNode_GetOwningSegment();
	
	void AllocateDefaultPins() override;
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	bool ShouldDrawCompact() const override { return true; }
	FText GetCompactNodeTitle() const override;
	FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	bool IsNodePure() const override { return true; }
	bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;
	FText GetMenuCategory() const override;
	void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	void PostPasteNode() override;
	void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	
	UPROPERTY()
	TSubclassOf<UGameActionSegmentBase> OwningType;
private:
	void UpdateOwningType();
};
