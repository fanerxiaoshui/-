// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <IDetailKeyframeHandler.h>
#include <UObject/NoExportTypes.h>
#include "GameActionSequencer.generated.h"

class UGameActionSegment;
class UGameActionSequence;
class SWidget;
class ISequencer;
class UGameActionSegmentBase;
class UBPNode_GameActionSegmentBase;
class FGameActionEditScene;
class IDetailsView;
class FExtender;
class UBPNode_GameActionSegment;
class IToolkitHost;
class SWidgetSwitcher;
class SBox;

/**
 * 
 */
class GAMEACTION_EDITOR_API FGameActionSequencer : public TSharedFromThis<FGameActionSequencer>
{
public:
    FGameActionSequencer(const TSharedRef<FGameActionEditScene>& InGameActionPreviewScene, const TSharedPtr<IToolkitHost>& InToolkitHost);
    ~FGameActionSequencer();

    void OpenGameActionSequencer(UBPNode_GameActionSegment* ActionSegmentNode);
    void CloseGameActionSequencer();

    void WhenSelectedNodesChanged(const TSet<class UObject*>& NewSelection);
    void WhenDeleteNode(UBPNode_GameActionSegmentBase* GameActionSegmentNode);
	void PostBlueprintCompiled();
	void WhenPreviewObjectChanged(UObject* PreviewObject);

	TSharedRef<SWidget> GetSequencerWidget() const;
	void SetInSimulation(bool bInSimulation);

	TSharedPtr<ISequencer> PreviewSequencer;
protected:
	TWeakPtr<IToolkitHost> ToolkitHost;
    TSharedRef<FGameActionEditScene> EditScene;
    TWeakObjectPtr<UBPNode_GameActionSegment> PreviewActionSegmentNode;
	TWeakObjectPtr<UGameActionSequence> EditingActionSequence;
	TSharedPtr<SWidgetSwitcher> ActionSequencerWidget;
    TSharedPtr<SBox> SequencerContent;

	TSharedPtr<FExtender> GameActionToolbarExtender;
	
	FDelegateHandle ActorBindingDelegateHandle;
	FDelegateHandle SequencerExtenderHandle;
	FDelegateHandle SequenceEditorHandle;
	FDelegateHandle OnActorMoveHandle;
	FDelegateHandle OnObjectsReplacedHandle;
public:
	class FGameActionDetailKeyframeHandler : public IDetailKeyframeHandler
	{
	public:
		FGameActionDetailKeyframeHandler(const TSharedPtr<FGameActionSequencer>& GameActionSequencer)
			: GameActionSequencer(GameActionSequencer)
		{}

		bool IsPropertyKeyable(UClass* InObjectClass, const IPropertyHandle& InPropertyHandle) const override;
		bool IsPropertyKeyingEnabled() const override;
		bool IsPropertyAnimated(const IPropertyHandle& PropertyHandle, UObject* ParentObject) const override;
		void OnKeyPropertyClicked(const IPropertyHandle& KeyedPropertyHandle) override;
	private:
		TWeakPtr<FGameActionSequencer> GameActionSequencer;
		const TSharedPtr<ISequencer> GetSequencer() const;
	};

private:
	void SyncWidgetSectionPosition();
	void CreateGameActionCamera();

	class UGameActionSequencerWidgetSectionVisual& WidgetSectionVisual;
};

UCLASS(Transient)
class UGameActionSequencerWidgetSectionVisual : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, meta = (DisplayName = "轨道当前坐标", MakeEditWidget = true))
	FTransform Transform;

	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	DECLARE_DELEGATE_OneParam(FOnTransformChanged, const FTransform&);
	FOnTransformChanged OnTransformChanged;

	UPROPERTY()
	class UMovieSceneSection* WidgetSection = nullptr;
};
