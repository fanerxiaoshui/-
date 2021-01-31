// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <BlueprintEditor.h>

class UGameActionBlueprint;
class FGameActionSequencer;
class FGameActionEditScene;
class FGameActionSimulateScene;

/**
 * 
 */
class GAMEACTION_EDITOR_API FGameActionEditor : public FBlueprintEditor, public IToolkitHost
{
    using Super = FBlueprintEditor;
public:
    static void OpenEditor(const EToolkitMode::Type InMode, const TSharedPtr<class IToolkitHost>& InToolkitHost, UGameActionBlueprint* GameActionBlueprint);

    FGameActionEditor();
    ~FGameActionEditor();

    TSharedPtr<FGameActionEditScene> EditScene;
	TSharedPtr<FGameActionSequencer> GameActionSequencer;

protected:
	TWeakObjectPtr<UGameActionBlueprint> GameActionBlueprint;
    void InitGameActionEditor(const EToolkitMode::Type InMode, const TSharedPtr<class IToolkitHost>& InToolkitHost, UGameActionBlueprint* GameActionBlueprint);
    void RegisterApplicationModes(const TArray<UBlueprint*>& InBlueprints, bool bShouldOpenInDefaultsMode, bool bNewlyCreated) override;
    void CreateDefaultCommands() override;

    FGraphAppearanceInfo GetGraphAppearance(class UEdGraph* InGraph) const override;
    void OnSelectedNodesChangedImpl(const TSet<class UObject*>& NewSelection) override;
    void DeleteSelectedNodes() override;
    void OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent) override;
    bool IsEditable(UEdGraph* InGraph) const override;
    bool IsInspectEditable() const;
    void OnGraphEditorFocused(const TSharedRef<class SGraphEditor>& InGraphEditor) override;

    FDelegateHandle SelectObjectEventHandle;
    FDelegateHandle BlueprintCompileHandle;
public:
	TWeakPtr<SWidgetSwitcher> ViewportWidgetSwitcher;
    TWeakPtr<class SGameActionEditViewport> EditViewport;
	TWeakPtr<class SGameActionSimulationViewport> SimulationViewport;
	uint8 bEnableSimulateCharacterCamera : 1;
	TSharedPtr<FGameActionSimulateScene> SimulateScene;
	bool IsSimulation() const { return SimulateScene.IsValid(); }

protected:
    TSharedRef<SWidget> GetParentWidget() override { return GetToolkitHost()->GetParentWidget(); }
    void BringToFront() override { GetToolkitHost()->BringToFront(); }
    TSharedRef<SDockTabStack> GetTabSpot(const EToolkitTabSpot::Type TabSpot) override { return GetToolkitHost()->GetTabSpot(TabSpot); }
    TSharedPtr<FTabManager> GetTabManager() const override { return GetToolkitHost()->GetTabManager(); }
    void OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit) override { GetToolkitHost()->OnToolkitHostingStarted(Toolkit); }
    void OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit) override { GetToolkitHost()->OnToolkitHostingFinished(Toolkit); }
    UWorld* GetWorld() const override;

private:
    struct FGameActionDebugger : public FTickableGameObject
    {
        FGameActionDebugger(FGameActionEditor& GameActionEditor)
            :GameActionEditor(GameActionEditor)
        {}

        bool IsTickableInEditor() const override { return true; }
        TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(GameActionDebugger, STATGROUP_Tickables); }
        void Tick(float DeltaTime) override;

        FGameActionEditor& GameActionEditor;
    	TWeakObjectPtr<class UBPNode_GameActionSegmentBase> PreActicedNode;
    };
    FGameActionDebugger GameActionDebugger;
};
