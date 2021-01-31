// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <UObject/NoExportTypes.h>
#include <Factories/Factory.h>
#include <AssetTypeActions_Base.h>
#include <Toolkits/AssetEditorToolkit.h>
#include <SEditorViewport.h>
#include <EditorViewportClient.h>
#include <AdvancedPreviewScene.h>
#include <SCommonEditorViewportToolbarBase.h>
#include <ThumbnailRendering/DefaultSizedThumbnailRenderer.h>
#include <Templates/SubclassOf.h>
#include <Misc/NotifyHook.h>
#include "GameActionSceneEditor.generated.h"

class UGameActionScene;
class ACharacter;

/**
 * 
 */
UCLASS()
class GAMEACTION_EDITOR_API UGameActionSceneFactory : public UFactory
{
	GENERATED_BODY()
public:
	UGameActionSceneFactory();

	UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;

	UPROPERTY(Transient)
	TSubclassOf<ACharacter> OwnerType;

	bool ConfigureProperties() override;
};

class FGameActionScene_AssetTypeActions : public FAssetTypeActions_Base
{
public:
	FGameActionScene_AssetTypeActions();

	// Inherited via FAssetTypeActions_Base
	FText GetName() const override;
	UClass* GetSupportedClass() const override;
	FColor GetTypeColor() const override;
	uint32 GetCategories() override;
	void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor) override;
};

class FPreviewGameActionScene : public FAdvancedPreviewScene
{
	using Super = FAdvancedPreviewScene;
public:
	FPreviewGameActionScene(ConstructionValues CVS, UGameActionScene* InGameActionSceneAsset);
	~FPreviewGameActionScene();

	void Tick(float DeltaTime) override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	TWeakObjectPtr<UGameActionScene> GameActionSceneAsset;

	ACharacter* ActionOwner;
	TArray<AActor*> SceneInitedActors;

	TWeakPtr<FEditorViewportClient> EditorViewportClient;
	void SelectActor(AActor* Actor) const;
	
	FDelegateHandle DestroyActorHandle;
	FDelegateHandle OnObjectsReplacedHandle;
	FDelegateHandle OnActorLabelChangedHandle;
};

class GAMEACTION_EDITOR_API FGameActionSceneEditor : public FAssetEditorToolkit, public FNotifyHook
{
	using Super = FAssetEditorToolkit;
public:
	FGameActionSceneEditor();
	~FGameActionSceneEditor();

	// Inherited via FAssetEditorToolkit
	FLinearColor GetWorldCentricTabColorScale() const override;
	FName GetToolkitFName() const override;
	FText GetBaseToolkitName() const override;
	FString GetWorldCentricTabPrefix() const override;
	void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	void SaveAsset_Execute() override;
	bool OnRequestClose() override;

	void SyncTempateToAsset();
	void InitGameActionSceneEditor(const EToolkitMode::Type InMode, const TSharedPtr<class IToolkitHost>& InToolkitHost, UGameActionScene* InAsset);
private:
	TSharedPtr<IDetailsView> DetailsWidget;
	TSharedPtr<class SGameActionSceneViewport> Viewport;
	TSharedPtr<FPreviewGameActionScene> PreviewScene;
	FDelegateHandle SelectObjectEventHandle;
	FDelegateHandle SceneOutlinerSelectChangeHandle;
};

class FGameActionSceneViewportClient : public FEditorViewportClient
{
	using Super = FEditorViewportClient;
public:
	FGameActionSceneViewportClient(const TSharedRef<SGameActionSceneViewport>& InViewport, const TSharedRef<FPreviewGameActionScene>& InPreviewScene);
	
	TSharedRef<FPreviewGameActionScene> PreviewScene;
	TWeakPtr<class SGameActionSceneViewport> ViewportPtr;
public:
	bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale) override;
 	void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
 	bool InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event, float AmountDepressed, bool bGamepad) override;
};

class SGameActionSceneViewport : public SEditorViewport, public FGCObject, public ICommonEditorViewportToolbarInfoProvider
{
	using Super = SEditorViewport;
public:
	SLATE_BEGIN_ARGS(SGameActionSceneViewport) {}
	SLATE_END_ARGS()

	/** The scene for this viewport. */
	TSharedPtr<FPreviewGameActionScene> PreviewScene;

	void Construct(const FArguments& InArgs, const TSharedPtr<FPreviewGameActionScene>& InPreviewScene);

	// TODO：完成拖拽生成资源的预览
	void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

	void AddReferencedObjects(FReferenceCollector& Collector) override {}
	TSharedRef<class SEditorViewport> GetViewportWidget() override;
	TSharedPtr<FExtender> GetExtenders() const override;
	void OnFloatingButtonClicked() override {}

	TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	TSharedPtr<SWidget> MakeViewportToolbar() override;
private:
	TSharedPtr<FGameActionSceneViewportClient> ActionViewportClient;
	FVector2D CachedOnDropLocalMousePos;
	TWeakObjectPtr<AActor> PreviewActor;
};

UCLASS()
class UGameActionSceneThumbnailRenderer : public UDefaultSizedThumbnailRenderer
{
	GENERATED_BODY()
public:
	void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily) override;
	void BeginDestroy() override;
	bool AllowsRealtimeThumbnails(UObject* Object) const override { return false; }
private:
	class FAdvancedPreviewScene* ThumbnailScene = nullptr;
};
