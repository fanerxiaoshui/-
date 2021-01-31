// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <K2Node.h>
#include <K2Node_Event.h>
#include "Blueprint/EdGraphSchema_GameAction.h"
#include "BPNode_GameActionSegment.generated.h"

class UGameActionSegmentBase;
class UGameActionSequence;
class UBPNode_GameActionSegmentBase;

/**
 * 
 */
namespace GameActionNodeUtils
{
    const static FName GameActionExecPinCategory = TEXT("GameActionExecPin");
}

UCLASS()
class UEdGraphSchema_GameActionEvents : public UEdGraphSchema_GameAction
{
	GENERATED_BODY()
};

UCLASS()
class GAMEACTION_EDITOR_API UBPNode_GameActionSegmentEvent : public UK2Node_Event
{
	GENERATED_BODY()
public:
	UBPNode_GameActionSegmentEvent();
	bool CanDuplicateNode() const override { return false; }
	bool CanUserDeleteNode() const override { return true; }

	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	FText GetMenuCategory() const override;
	void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	bool IsActionFilteredOut(class FBlueprintActionFilter const& Filter) override;
	void PostPlacedNewNode() override;
	FText GetTooltipText() const override;
	bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;
	
	UPROPERTY()
	TFieldPath<FDelegateProperty> DelegateProperty;
	UPROPERTY()
	const UBPNode_GameActionSegmentBase* SegmentNode;
};

UCLASS(abstract)
class GAMEACTION_EDITOR_API UBPNode_GameActionSegmentBase : public UK2Node
{
	GENERATED_BODY()
public:
    UBPNode_GameActionSegmentBase();

    UPROPERTY(VisibleAnywhere, Instanced)
    UGameActionSegmentBase* GameActionSegment = nullptr;

    bool CanUserDeleteNode() const override { return true; }
    bool CanDuplicateNode() const override { return true; }
    FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FText GetMenuCategory() const override;
    bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;
    void AllocateDefaultPins() override;
    TSharedPtr<class INameValidatorInterface> MakeNameValidator() const override;
    void OnRenameNode(const FString& NewName) override;
    void PostPlacedNewNode() override;
    void PostPasteNode() override;
	void DestroyNode() override;
	void PinConnectionListChanged(UEdGraphPin* Pin) override;
    TSharedPtr<SGraphNode> CreateVisualWidget() override;
	bool ShouldShowNodeProperties() const override { return true; }
	UObject* GetJumpTargetForDoubleClick() const override;
	void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	
	UPROPERTY(EditAnywhere, Category = PinOptions, EditFixedSize)
	TArray<FOptionalPinFromProperty> ShowPinForProperties;
	
    UPROPERTY()
    FName RefVarName;
	FORCEINLINE FName GetRefVarName() const { return RefVarName; }
    static bool IsUniqueRefVarName(const UEdGraph* Graph, const FName& Name);
	static FString MakeUniqueRefVarName(const UEdGraph* Graph, const FString& BaseName);

	virtual UGameActionSequence* GetGameActionSequence() const { return nullptr; }
	
	UPROPERTY()
	mutable UEdGraph* EventGraph = nullptr;
	FName GetEventGraphName() const { return *FString::Printf(TEXT("%s_Events"), *GetRefVarName().ToString()); }
protected:
	static struct FTimerHandle SkeletalRecompileChildrenHandle;
	// 由于PostPlacedNewNode等操作在触发时还没更新节点列表，所以延迟一帧执行
	// 带来的好处是可以防止同一个操作刷新多次
	void RecompileSkeletalBlueprintDelayed();

	void BuildOptionPins(TArray<FOptionalPinFromProperty>& ShowPinOptions, UClass* Class);
public:
	enum class EDebugState : uint8
	{
		Deactived,
		Actived
	};
	EDebugState DebugState = EDebugState::Deactived;
};

UCLASS()
class GAMEACTION_EDITOR_API UBPNode_GameActionSegment : public UBPNode_GameActionSegmentBase
{
	GENERATED_BODY()
public:
	void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

    UGameActionSequence* GetGameActionSequence() const override;
};

UCLASS()
class GAMEACTION_EDITOR_API UBPNode_GameActionSegmentGeneric : public UBPNode_GameActionSegmentBase
{
	GENERATED_BODY()
public:
	void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
};
