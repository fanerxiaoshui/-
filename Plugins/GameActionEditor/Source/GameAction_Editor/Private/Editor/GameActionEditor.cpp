// Fill out your copyright notice in the Description page of Project Settings.


#include "Editor/GameActionEditor.h"
#include <BlueprintEditorModule.h>
#include <BlueprintEditorModes.h>
#include <BlueprintEditorTabs.h>
#include <WatchPointViewer.h>
#include <EditorModes.h>
#include <EditorViewportClient.h>
#include <EditorModeManager.h>
#include <ToolMenu.h>
#include <SEditorViewport.h>
#include <SCommonEditorViewportToolbarBase.h>
#include <SBlueprintEditorToolbar.h>
#include <EngineUtils.h>
#include <Engine/Selection.h>
#include <SKismetInspector.h>
#include <MouseDeltaTracker.h>
#include <Kismet2/KismetEditorUtilities.h>
#include <Widgets/Notifications/SNotificationList.h>
#include <Widgets/Layout/SWidgetSwitcher.h>
#include <Framework/Notifications/NotificationManager.h>
#include <MovieSceneCommonHelpers.h>
#include <Camera/CameraComponent.h>
#include <GameFramework/PlayerController.h>
#include <Camera/CameraActor.h>
#include <Widgets/Docking/SDockableTab.h>
#include <IDetailsView.h>

#include "GameAction_Editor.h"
#include "Sequencer/GameActionSequencer.h"
#include "Blueprint/GameActionBlueprint.h"
#include "Blueprint/EdGraph_GameAction.h"
#include "Blueprint/BPNode_GameActionSegment.h"
#include "GameAction/GameActionInstance.h"
#include "GameAction/GameActionSegment.h"
#include "Editor/GameActionEdMode.h"
#include "Editor/GameActionPreviewScene.h"

#define LOCTEXT_NAMESPACE "GameActionBlueprintEditor"

namespace GameActionBlueprintEditorTabs
{
	const FName ViewportTab(TEXT("Viewport"));
	const FName SequencerTab(TEXT("Sequencer"));
};

class FGameActionEditViewportClient : public FEditorViewportClient
{
	using Super = FEditorViewportClient;
public:
	FGameActionEditViewportClient(const TSharedRef<class SGameActionEditViewport>& InViewport, const TSharedRef<FGameActionEditScene>& InEditScene)
		: Super(nullptr, &InEditScene.Get(), StaticCastSharedRef<SEditorViewport>(InViewport))
		, ViewportPtr(InViewport)
		, EditScene(InEditScene)
	{
		EngineShowFlags.SetScreenPercentage(true);
		SetRealtime(true);
		SetViewportType(LVT_Perspective);
		SetViewModes(VMI_Lit, VMI_Lit);

		SetViewLocation(FVector(100, 100, 200));
		SetViewRotation(FVector(-75, -75, -75).Rotation());

		DrawHelper.bDrawGrid = false;
		DrawHelper.bDrawPivot = false;
		DrawHelper.AxesLineThickness = 5;
		DrawHelper.PivotSize = 5;

		bDrawVertices = true;
		
		FEditorModeTools* TheModeTools = GetModeTools();
		TheModeTools->SetDefaultMode(FGameActionDefaultEdMode::ModeId);
		TheModeTools->ActivateDefaultMode();
		FGameActionDefaultEdMode* GameActionDefaultEdMode = TheModeTools->GetActiveModeTyped<FGameActionDefaultEdMode>(FGameActionDefaultEdMode::ModeId);
		GameActionDefaultEdMode->EditScene = InEditScene;
	}

	TWeakPtr<class SGameActionEditViewport> ViewportPtr;
	TSharedRef<FGameActionEditScene> EditScene;
	int32 TransactionIdx = INDEX_NONE;
	TWeakObjectPtr<UCameraComponent> CachedCameraComponent;
public:
	bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale) override
	{
		if (Super::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale))
		{
			return true;
		}
		
		if (CurrentAxis != EAxisList::None)
		{
			for (FSelectionIterator SelectedActorIt(*GetModeTools()->GetSelectedActors()); SelectedActorIt; ++SelectedActorIt)
			{
				if (AActor* InActor = CastChecked<AActor>(*SelectedActorIt))
				{
					InActor->Modify();
					const bool bIsAltPressed = IsAltPressed();
					const bool bIsShiftPressed = IsShiftPressed();
					const bool bIsCtrlPressed = IsCtrlPressed();
					InActor->EditorApplyRotation(Rot, bIsAltPressed, bIsShiftPressed, bIsCtrlPressed);
					InActor->EditorApplyTranslation(Drag, bIsAltPressed, bIsShiftPressed, bIsCtrlPressed);
					InActor->PostEditMove(false);
					// GEditor->ApplyDeltaToActor(InActor, true, &Drag, &Rot, nullptr, IsAltPressed(), IsShiftPressed(), IsCtrlPressed());

					ModeTools->PivotLocation += Drag;
					ModeTools->SnappedLocation += Drag;
				}
			}
			return true;
		}
		return false;
	}
	void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override
	{
		Super::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
		if (HitProxy && HitProxy->IsA(HActor::StaticGetType()))
		{
			HActor* ActorHitProxy = (HActor*)HitProxy;
			AActor* ConsideredActor = ActorHitProxy->Actor;
			if (ConsideredActor) // It is Possessable to be clicking something during level transition if you spam click, and it might not be valid by this point
			{
				while (ConsideredActor->IsChildActor())
				{
					ConsideredActor = ConsideredActor->GetParentActor();
				}

				EditScene->SelectActor(ConsideredActor);
			}
		}
	}
	void TrackingStarted(const struct FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge) override
	{
		if (GetModeTools()->StartTracking(this, Viewport))
		{
			return;
		}
		if (bIsDraggingWidget)
		{
			if (TransactionIdx != INDEX_NONE)
			{
				GEditor->CancelTransaction(TransactionIdx);
				TransactionIdx = INDEX_NONE;
			}
			TransactionIdx = GEditor->BeginTransaction(LOCTEXT("Move Actor", "Move Actor"));
		}
	}
	void TrackingStopped() override 
	{
		if (GetModeTools()->EndTracking(this, Viewport))
		{
			return;
		}
		if (TransactionIdx != INDEX_NONE)
		{
			GEditor->EndTransaction();
			TransactionIdx = INDEX_NONE;
		}
		if (MouseDeltaTracker->HasReceivedDelta())
		{
			TArray<AActor*> MovedActors;
			for (FSelectionIterator SelectedActorIt(*GetModeTools()->GetSelectedActors()); SelectedActorIt; ++SelectedActorIt)
			{
				if (AActor* Actor = CastChecked<AActor>(*SelectedActorIt))
				{
					MovedActors.Add(Actor);
					FPropertyChangedEvent PropertyChangedEvent(FindFProperty<FProperty>(USceneComponent::StaticClass(), USceneComponent::GetRelativeLocationPropertyName()), EPropertyChangeType::ValueSet);
					FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Actor, PropertyChangedEvent);
					Actor->PostEditMove(true);
					GEditor->BroadcastEndObjectMovement(*Actor);
				}
			}
			GEditor->BroadcastActorsMoved(MovedActors);
		}
	}
	void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override
	{
		Super::Draw(View, PDI);
	}
	void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) override
	{
		Super::DrawCanvas(InViewport, View, Canvas);
	}
	void Tick(float DeltaSeconds) override
	{
		if (AActor* CameraCutsView = EditScene->CameraCutsViewActor.Get())
		{
			if (UCameraComponent* CameraComponent = MovieSceneHelpers::CameraComponentFromActor(CameraCutsView))
			{
				CachedCameraComponent = CameraComponent;
				CameraComponent->GetCameraView(0.f, ControllingActorViewInfo);
				CameraComponent->GetExtraPostProcessBlends(ControllingActorExtraPostProcessBlends, ControllingActorExtraPostProcessBlendWeights);
				ViewFOV = ControllingActorViewInfo.FOV;
				AspectRatio = ControllingActorViewInfo.AspectRatio;
				SetViewLocation(ControllingActorViewInfo.Location);
				SetViewRotation(ControllingActorViewInfo.Rotation);
				return;
			}
		}
		else if (AActor* ToViewActor = EditScene->ToViewActor.Get())
		{
			UCameraComponent* CameraComponent = MovieSceneHelpers::CameraComponentFromActor(ToViewActor);
			const bool bIsNewViewTo = CachedCameraComponent.Get() != CameraComponent;
			CachedCameraComponent = CameraComponent;
			if (CameraComponent)
			{
				bUseControllingActorViewInfo = true;
				const bool IsCameraMoved = CameraComponent->GetComponentLocation().Equals(ControllingActorViewInfo.Location) == false || CameraComponent->GetComponentRotation().Equals(ControllingActorViewInfo.Rotation) == false;
				
				CameraComponent->GetCameraView(0.f, ControllingActorViewInfo);
				CameraComponent->GetExtraPostProcessBlends(ControllingActorExtraPostProcessBlends, ControllingActorExtraPostProcessBlendWeights);
				ViewFOV = ControllingActorViewInfo.FOV;
				AspectRatio = ControllingActorViewInfo.AspectRatio;

				if (bIsNewViewTo || IsCameraMoved || ToViewActor->IsA<ACameraActor>() == false)
				{
					SetViewLocation(ControllingActorViewInfo.Location);
					SetViewRotation(ControllingActorViewInfo.Rotation);
				}
				else
				{
					Super::Tick(DeltaSeconds);
					const FVector Location = GetViewLocation();
					const FRotator Rotation = GetViewRotation();
					if (Location.Equals(ControllingActorViewInfo.Location) == false || Rotation.Equals(ControllingActorViewInfo.Rotation) == false)
					{
						ToViewActor->SetActorLocationAndRotation(Location, Rotation);
						EditScene->OnMoveViewActor.ExecuteIfBound(Location, Rotation);
					}
				}
				return;
			}
		}
		else
		{
			CachedCameraComponent.Reset();
		}
		
		if (bUseControllingActorViewInfo == true)
		{
			bUseControllingActorViewInfo = false;
			FRotator ViewRotation = GetViewRotation();
			ViewRotation.Roll = 0.f;
			SetViewRotation(ViewRotation);
			AspectRatio = 1.777777f;
			ViewFOV = FOVAngle;
		}
		
		Super::Tick(DeltaSeconds);
	}
	bool GetActiveSafeFrame(float& OutAspectRatio) const override
	{
		if (!IsOrtho())
		{
			const UCameraComponent* CameraComponent = CachedCameraComponent.Get();
			if (CameraComponent && CameraComponent->bConstrainAspectRatio)
			{
				OutAspectRatio = CameraComponent->AspectRatio;
				return true;
			}
		}

		return false;
	}

	ELevelViewportType GetViewportType() const override
	{
		ELevelViewportType EffectiveViewportType = ViewportType;
		if (bUseControllingActorViewInfo)
		{
			EffectiveViewportType = (ControllingActorViewInfo.ProjectionMode == ECameraProjectionMode::Perspective) ? LVT_Perspective : LVT_OrthoFreelook;
		}
		return EffectiveViewportType;
	}

	void OverridePostProcessSettings(FSceneView& View) override
	{
		if (UCameraComponent* CameraComponent = CachedCameraComponent.Get())
		{
			View.OverridePostProcessSettings(CameraComponent->PostProcessSettings, CameraComponent->PostProcessBlendWeight);
		}
	}
};

class SGameActionEditViewport : public SEditorViewport, public ICommonEditorViewportToolbarInfoProvider
{
	using Super = SEditorViewport;
public:
	SLATE_BEGIN_ARGS(SGameActionEditViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FGameActionEditor>& InGameActionEditor)
	{
		GameActionEditor = InGameActionEditor;
		EditScene = InGameActionEditor->EditScene;

		Super::Construct(SEditorViewport::FArguments());
	}

	TSharedRef<class SEditorViewport> GetViewportWidget() override { return SharedThis(this); }
	TSharedPtr<FExtender> GetExtenders() const override	{ return MakeShareable(new FExtender); }
	void OnFloatingButtonClicked() override {}
	TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override 
	{
		TSharedRef<FEditorViewportClient> EditorViewportClient = MakeShareable(new FGameActionEditViewportClient(SharedThis(this), EditScene.ToSharedRef()));
		EditScene->EditorViewportClient = EditorViewportClient;
		return EditorViewportClient;
	}
	TSharedPtr<SWidget> MakeViewportToolbar() override
	{
		class SGameActionEditorViewportToolbar : public SCommonEditorViewportToolbarBase
		{
			using Super = SCommonEditorViewportToolbarBase;
		public:
			void Construct(const FArguments& InArgs, TSharedPtr<class ICommonEditorViewportToolbarInfoProvider> InInfoProvider)
			{
				Super::Construct(InArgs, InInfoProvider);
			}
		};
		return SNew(SGameActionEditorViewportToolbar, SharedThis(this));
	}

	TWeakPtr<FGameActionEditor> GameActionEditor;
	TSharedPtr<FGameActionEditScene> EditScene;
};

class FGameActionSimulationViewportClient : public FEditorViewportClient
{
	using Super = FEditorViewportClient;
public:
	FGameActionSimulationViewportClient(const TSharedRef<class SGameActionSimulationViewport>& InViewport, const TSharedRef<FGameActionSimulateScene>& InSimulationScene, const TWeakPtr<FGameActionEditor>& GameActionEditor)
		: Super(nullptr, &InSimulationScene.Get(), StaticCastSharedRef<SEditorViewport>(InViewport))
		, SimulationScene(InSimulationScene)
		, GameActionEditor(GameActionEditor)
	{
		EngineShowFlags.SetScreenPercentage(true);
		SetRealtime(true);
		SetGameView(true);
		SetViewportType(LVT_Perspective);
		SetViewModes(VMI_Lit, VMI_Lit);

		UWorld* World = InSimulationScene->GetWorld();
		World->bBegunPlay = true;
		World->bShouldSimulatePhysics = true;
		
		DrawHelper.bDrawGrid = false;
		DrawHelper.bDrawPivot = false;
		DrawHelper.AxesLineThickness = 5;
		DrawHelper.PivotSize = 5;
	}

	void Tick(float DeltaSeconds) override
	{
		Super::Tick(DeltaSeconds);

		if (GameActionEditor.Pin()->bEnableSimulateCharacterCamera)
		{
			if (APlayerController* PlayerController = SimulationScene->PlayerController)
			{
				if (APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
				{
					ControllingActorViewInfo = CameraManager->ViewTarget.POV;
					ViewFOV = ControllingActorViewInfo.FOV;
					AspectRatio = ControllingActorViewInfo.AspectRatio;
					SetViewLocation(ControllingActorViewInfo.Location);
					SetViewRotation(ControllingActorViewInfo.Rotation);
				}
			}
		}
	}

	TSharedRef<FGameActionSimulateScene> SimulationScene;
	TWeakPtr<FGameActionEditor> GameActionEditor;
};

class SGameActionSimulationViewport : public SEditorViewport
{
	using Super = SEditorViewport;
public:
	SLATE_BEGIN_ARGS(SGameActionSimulationViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FGameActionEditor>& InGameActionEditor)
	{
		GameActionEditor = InGameActionEditor;

		Super::Construct(SEditorViewport::FArguments());
	}
	TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override
	{
		return MakeShareable(new FGameActionSimulationViewportClient(SharedThis(this), GameActionEditor.Pin()->SimulateScene.ToSharedRef(), GameActionEditor));
	}
	TSharedPtr<SWidget> MakeViewportToolbar() override
	{
		FMenuBuilder MenuBuilder{ false, GetCommandList() };
		MenuBuilder.AddMenuEntry(
			LOCTEXT("预览角色相机", "预览角色相机"), 
			LOCTEXT("预览角色相机", "预览角色相机"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]
				{
					TSharedPtr<FGameActionEditor> Editor = GameActionEditor.Pin();
					Editor->bEnableSimulateCharacterCamera = !Editor->bEnableSimulateCharacterCamera;
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]
				{
					return !!GameActionEditor.Pin()->bEnableSimulateCharacterCamera;
				})
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
		return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.FillWidth(1.f)
				.Padding(4.f)
				[
					MenuBuilder.MakeWidget()
				];
	}
	
	TWeakPtr<FGameActionEditor> GameActionEditor;
};

class FGameActionBlueprintEditorMode : public FBlueprintEditorApplicationMode
{
	using Super = FBlueprintEditorApplicationMode;
public:
	const static FName ModeName;
	static FText GetLocalizedMode(const FName InMode)
	{
		static TMap< FName, FText > LocModes;

		if (LocModes.Num() == 0)
		{
			LocModes.Add(ModeName, LOCTEXT("GameActionBlueprintEditorMode", "Game Action Blueprint"));
		}

		check(InMode != NAME_None);
		const FText* OutDesc = LocModes.Find(InMode);
		check(OutDesc);
		return *OutDesc;
	}

	FGameActionBlueprintEditorMode(const TSharedRef<FGameActionEditor>& InGameActionBlueprintEditor)
		: Super(InGameActionBlueprintEditor, ModeName, &FGameActionBlueprintEditorMode::GetLocalizedMode, false, false),
		GameActionEditorPtr(InGameActionBlueprintEditor)
	{

		TabLayout = FTabManager::NewLayout("Stanalone_GameActionBlueprintEditMode_Layout_v1")
			->AddArea
			(
				FTabManager::NewPrimaryArea()
				->SetOrientation(Orient_Vertical)
				->Split
				(
					// Top toolbar
					FTabManager::NewStack()
					->SetSizeCoefficient(0.186721f)
					->SetHideTabWell(true)
					->AddTab(InGameActionBlueprintEditor->GetToolbarTabId(), ETabState::OpenedTab)
				)
				->Split
				(
					// Main application area
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Horizontal)
					->Split
					(
						// Left side
						FTabManager::NewSplitter()
						->SetSizeCoefficient(0.25f)
						->SetOrientation(Orient_Vertical)
						->Split
						(
							// Left top - viewport
							FTabManager::NewStack()
							->SetSizeCoefficient(0.5f)
							->SetHideTabWell(true)
							->AddTab(GameActionBlueprintEditorTabs::ViewportTab, ETabState::OpenedTab)
						)
						->Split
						(
							//	Left bottom - preview settings
							FTabManager::NewStack()
							->SetSizeCoefficient(0.5f)
							->AddTab(FBlueprintEditorTabs::MyBlueprintID, ETabState::OpenedTab)
						)
					)
					->Split
					(
						// Middle 
						FTabManager::NewSplitter()
						->SetOrientation(Orient_Vertical)
						->SetSizeCoefficient(0.55f)
						->Split
						(
							// Middle top - document edit area
							FTabManager::NewStack()
							->SetSizeCoefficient(0.8f)
							->AddTab("Document", ETabState::ClosedTab)
						)
						->Split
						(
							// Middle bottom - compiler results & find
							FTabManager::NewStack()
							->SetSizeCoefficient(0.2f)
							->AddTab(GameActionBlueprintEditorTabs::SequencerTab, ETabState::OpenedTab)
							->AddTab(FBlueprintEditorTabs::CompilerResultsID, ETabState::ClosedTab)
							->AddTab(FBlueprintEditorTabs::FindResultsID, ETabState::ClosedTab)
						)
					)
					->Split
					(
						// Right side
						FTabManager::NewSplitter()
						->SetSizeCoefficient(0.2f)
						->SetOrientation(Orient_Vertical)
						->Split
						(
							// Right top - selection details panel & overrides
							FTabManager::NewStack()
							->SetHideTabWell(false)
							->SetSizeCoefficient(0.5f)
							->AddTab(FBlueprintEditorTabs::DetailsID, ETabState::OpenedTab)
							->SetForegroundTab(FBlueprintEditorTabs::DetailsID)
						)
// 						->Split
// 						(
// 							// Right bottom - Asset browser & advanced preview settings
// 							FTabManager::NewStack()
// 							->SetHideTabWell(false)
// 							->SetSizeCoefficient(0.5f)
// 							->AddTab(AnimationBlueprintEditorTabs::AnimBlueprintPreviewEditorTab, ETabState::OpenedTab)
// 							->AddTab(AnimationBlueprintEditorTabs::AssetBrowserTab, ETabState::OpenedTab)
// 							->AddTab(AnimationBlueprintEditorTabs::SlotNamesTab, ETabState::ClosedTab)
// 							->SetForegroundTab(AnimationBlueprintEditorTabs::AnimBlueprintPreviewEditorTab)
// 						)
					)
				)
			);

		if (UToolMenu* Toolbar = InGameActionBlueprintEditor->RegisterModeToolbarIfUnregistered(GetModeName()))
		{
			TSharedPtr<class FBlueprintEditorToolbar> ToolbarBuilder = InGameActionBlueprintEditor->GetToolbarBuilder();
			ToolbarBuilder->AddCompileToolbar(Toolbar);
			ToolbarBuilder->AddScriptingToolbar(Toolbar);
			ToolbarBuilder->AddBlueprintGlobalOptionsToolbar(Toolbar);
			
			{
				FToolMenuSection& Section = Toolbar->AddSection("GameActionViewport");
				Section.AddEntry(FToolMenuEntry::InitToolBarButton(FGameActionEditorCommands::Get().EnableSimulation));
			}
			
			ToolbarBuilder->AddDebuggingToolbar(Toolbar);
		}
	}

	// FApplicationMode interface
	void RegisterTabFactories(TSharedPtr<FTabManager> InTabManager) override
	{
		Super::RegisterTabFactories(InTabManager);

		WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("GameActionEditorWorkspaceMenu", "Game Action Editor"));

		InTabManager->RegisterTabSpawner(GameActionBlueprintEditorTabs::ViewportTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs& Args)
		{
			TSharedRef<FGameActionEditor> GameActionEditor = GameActionEditorPtr.Pin().ToSharedRef();
			TSharedRef<SDockTab> SpawnedTab =
				SNew(SDockTab)
				.Label(LOCTEXT("GameActionViewport_TabTitle", "Viewport"))
				[
					SAssignNew(GameActionEditor->ViewportWidgetSwitcher, SWidgetSwitcher)
					.WidgetIndex(0)
					+ SWidgetSwitcher::Slot()
					[
						SAssignNew(GameActionEditorPtr.Pin()->EditViewport, SGameActionEditViewport, GameActionEditor)
					]
				];

			return SpawnedTab;
		}))
		.SetDisplayName(LOCTEXT("GameActionEditorViewportTab", "Game Action Viewport"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

		InTabManager->RegisterTabSpawner(GameActionBlueprintEditorTabs::SequencerTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs& Args)
		{
			TSharedRef<SDockTab> SpawnedTab =
				SNew(SDockTab)
				.Label(LOCTEXT("GameActionSequencer_TabTitle", "Sequencer"))
				[
					GameActionEditorPtr.Pin()->GameActionSequencer->GetSequencerWidget()
				];

			return SpawnedTab;
		}))
		.SetDisplayName(LOCTEXT("GameActionEditorSequencerTab", "Game Action Sequencer"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());
	}

	void PostActivateMode() override
	{
		Super::PostActivateMode();
	}

private:
	TWeakPtr<FGameActionEditor> GameActionEditorPtr;
	// End of FApplicationMode interface
};
const FName FGameActionBlueprintEditorMode::ModeName = TEXT("GameActionBlueprintEditorMode");

void FGameActionEditor::OpenEditor(const EToolkitMode::Type InMode, const TSharedPtr<class IToolkitHost>& InToolkitHost, UGameActionBlueprint* GameActionBlueprint)
{
	TSharedRef<FGameActionEditor> Editor = MakeShareable(new FGameActionEditor());
	Editor->InitGameActionEditor(InMode, InToolkitHost, GameActionBlueprint);
}

FGameActionEditor::FGameActionEditor()
	: bEnableSimulateCharacterCamera(false)
	, GameActionDebugger(*this)
{
	SelectObjectEventHandle = USelection::SelectObjectEvent.AddLambda([=](UObject* Object)
	{
		if (Inspector.IsValid() && Object && Object->GetWorld() == EditScene->GetWorld())
		{
			Inspector->ShowDetailsForSingleObject(Object);
		}
	});
}

FGameActionEditor::~FGameActionEditor()
{
	USelection::SelectObjectEvent.Remove(SelectObjectEventHandle);
	if (UGameActionBlueprint* Blueprint = GameActionBlueprint.Get())
	{
		Blueprint->PreviewSequencer = nullptr;
		Blueprint->OnCompiled().Remove(BlueprintCompileHandle);
	}
}

void FGameActionEditor::InitGameActionEditor(const EToolkitMode::Type InMode, const TSharedPtr<class IToolkitHost>& InToolkitHost, UGameActionBlueprint* InGameActionBlueprint)
{
	GameActionBlueprint = InGameActionBlueprint;

	EditScene = MakeShareable(new FGameActionEditScene(InGameActionBlueprint));
	GameActionSequencer = MakeShareable(new FGameActionSequencer(EditScene.ToSharedRef(), SharedThis(this)));
	InGameActionBlueprint->PreviewSequencer = GameActionSequencer;

	InitBlueprintEditor(InMode, InToolkitHost, { InGameActionBlueprint }, false);
	UpdatePreviewActor(GetBlueprintObj(), true);

	WatchViewer::UpdateWatchListFromBlueprint(InGameActionBlueprint);

	FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
	BlueprintEditorModule.OnBlueprintEditorOpened().Broadcast(InGameActionBlueprint->BlueprintType);

	Inspector->GetPropertyView()->SetKeyframeHandler(MakeShareable(new FGameActionSequencer::FGameActionDetailKeyframeHandler(GameActionSequencer)));;
	struct SKismetInspectorHacker : SKismetInspector
	{
		static FIsPropertyEditingEnabled& GetIsPropertyEditingEnabledDelegate(SKismetInspector* Inspector)
		{
			return static_cast<SKismetInspectorHacker*>(Inspector)->IsPropertyEditingEnabledDelegate;
		}
	};
	SKismetInspectorHacker::GetIsPropertyEditingEnabledDelegate(Inspector.Get()).BindRaw(this, &FGameActionEditor::IsInspectEditable);

	BlueprintCompileHandle = InGameActionBlueprint->OnCompiled().AddLambda([this](UBlueprint* Blueprint)
	{
		GameActionSequencer->PostBlueprintCompiled();
	});

	// 子蓝图默认打开图表面板
	if (UGameActionBlueprint* RootBlueprint = InGameActionBlueprint->FindRootBlueprint())
	{
		OpenGraphAndBringToFront(RootBlueprint->GetGameActionGraph());
	}
}

void FGameActionEditor::RegisterApplicationModes(const TArray<UBlueprint*>& InBlueprints, bool bShouldOpenInDefaultsMode, bool bNewlyCreated)
{
	AddApplicationMode(FGameActionBlueprintEditorMode::ModeName, MakeShareable(new FGameActionBlueprintEditorMode(SharedThis(this))));
	SetCurrentMode(FGameActionBlueprintEditorMode::ModeName);
}

void FGameActionEditor::CreateDefaultCommands()
{
	Super::CreateDefaultCommands();

	const FGameActionEditorCommands& Commands = FGameActionEditorCommands::Get();
	GetToolkitCommands()->MapAction(
		Commands.EnableSimulation,
		FExecuteAction::CreateLambda([this]
		{
			if (ViewportWidgetSwitcher.IsValid())
			{
				const bool DisableSimulation = IsSimulation();

				GameActionSequencer->SetInSimulation(DisableSimulation);
				EditViewport.Pin()->GetViewportClient()->SetRealtime(DisableSimulation);
				
				if (DisableSimulation)
				{
					TSharedPtr<SWidgetSwitcher> Switcher = ViewportWidgetSwitcher.Pin();

					const FVector ViewLocation = SimulationViewport.Pin()->GetViewportClient()->GetViewLocation();
					const FRotator ViewRotation = SimulationViewport.Pin()->GetViewportClient()->GetViewRotation();
					EditViewport.Pin()->GetViewportClient()->SetViewLocation(ViewLocation);
					EditViewport.Pin()->GetViewportClient()->SetViewRotation(ViewRotation);

					Switcher->SetActiveWidgetIndex(0);
					Switcher->RemoveSlot(Switcher->GetWidget(1).ToSharedRef());
					SimulateScene.Reset();
					SimulationViewport.Reset();

					GetInspector()->ShowDetailsForSingleObject(nullptr);
				}
				else
				{
					if (GameActionBlueprint->Status == EBlueprintStatus::BS_Dirty)
					{
						FKismetEditorUtilities::CompileBlueprint(GameActionBlueprint.Get());
					}
					if (GameActionBlueprint->Status == EBlueprintStatus::BS_Error)
					{
						FNotificationInfo NotificationInfo(LOCTEXT("请修复编译报错再进入模拟状态", "请修复编译报错再进入模拟状态"));
						NotificationInfo.bUseSuccessFailIcons = true;
						NotificationInfo.ExpireDuration = 3.0f;
						NotificationInfo.bUseLargeFont = true;
						FSlateNotificationManager::Get().AddNotification(NotificationInfo)->SetCompletionState(SNotificationItem::CS_Fail);
						return;
					}
					
					TSharedPtr<SWidgetSwitcher> Switcher = ViewportWidgetSwitcher.Pin();
					SimulateScene = MakeShareable(new FGameActionSimulateScene(CastChecked<UGameActionBlueprint>(GetBlueprintObj())));
					Switcher->AddSlot(1)
						[
							SAssignNew(SimulationViewport, SGameActionSimulationViewport, SharedThis(this))
						];
					Switcher->SetActiveWidgetIndex(1);

					const FVector ViewLocation = EditViewport.Pin()->GetViewportClient()->GetViewLocation();
					const FRotator ViewRotation = EditViewport.Pin()->GetViewportClient()->GetViewRotation();
					SimulationViewport.Pin()->GetViewportClient()->SetViewLocation(ViewLocation);
					SimulationViewport.Pin()->GetViewportClient()->SetViewRotation(ViewRotation);

					GetInspector()->ShowDetailsForSingleObject(SimulateScene->GetGameActionInstance());
				}
			}
		}),
		FCanExecuteAction::CreateLambda([this]
		{
			return GameActionBlueprint->Status != EBlueprintStatus::BS_Error;
		}),
		FIsActionChecked::CreateLambda([this]
		{
			return SimulationViewport.IsValid();
		}));
}

FGraphAppearanceInfo FGameActionEditor::GetGraphAppearance(class UEdGraph* InGraph) const
{
	FGraphAppearanceInfo AppearanceInfo = Super::GetGraphAppearance(InGraph);
	if (InGraph->GetClass() == UEdGraph_GameAction::StaticClass())
	{
		AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_GameActionGraph", "游戏行为");
	}
	return AppearanceInfo;
}

void FGameActionEditor::OnSelectedNodesChangedImpl(const TSet<class UObject*>& NewSelection)
{
	if (IsSimulation())
	{
		return;
	}
	Super::OnSelectedNodesChangedImpl(NewSelection);
	EditScene->ClearDrawableObject();
	GameActionSequencer->WhenSelectedNodesChanged(NewSelection);
}

void FGameActionEditor::DeleteSelectedNodes()
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		if (UBPNode_GameActionSegmentBase* GameActionSegmentNode = Cast<UBPNode_GameActionSegmentBase>(*NodeIt))
		{
			if (GameActionSegmentNode->CanUserDeleteNode())
			{
				GameActionSequencer->WhenDeleteNode(GameActionSegmentNode);
			}
		}
	}
	Super::DeleteSelectedNodes();
}

void FGameActionEditor::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	for (const TWeakObjectPtr<UObject>& ObjectPtr : Inspector->GetPropertyView()->GetSelectedObjects())
	{
		if (UObject* Object = ObjectPtr.Get())
		{
			GameActionSequencer->WhenPreviewObjectChanged(Object);
		}
	}
}

bool FGameActionEditor::IsEditable(UEdGraph* InGraph) const
{
	return Super::IsEditable(InGraph) && IsSimulation() == false && IsGraphInCurrentBlueprint(InGraph);
}

bool FGameActionEditor::IsInspectEditable() const
{
	for (const TWeakObjectPtr<UObject>& Object : Inspector->GetSelectedObjects())
	{
		if (UK2Node* Node = Cast<UK2Node>(Object))
		{
			if (IsGraphInCurrentBlueprint(Node->GetGraph()) == false)
			{
				return false;
			}
		}
	}
	return true;
}

void FGameActionEditor::OnGraphEditorFocused(const TSharedRef<SGraphEditor>& InGraphEditor)
{
	if (IsSimulation() == false)
	{
		Super::OnGraphEditorFocused(InGraphEditor);
	}
}

UWorld* FGameActionEditor::GetWorld() const
{
	return EditScene->GetWorld();
}

void FGameActionEditor::FGameActionDebugger::Tick(float DeltaTime)
{
	if (UGameActionBlueprint* Blueprint = Cast<UGameActionBlueprint>(GameActionEditor.GetBlueprintObj()))
	{
		UGameActionInstanceBase* DebugInstance = Cast<UGameActionInstanceBase>(Blueprint->GetObjectBeingDebugged());
		if (DebugInstance == nullptr && GameActionEditor.SimulateScene.IsValid())
		{
			DebugInstance = GameActionEditor.SimulateScene->GetGameActionInstance();
		}
		if (DebugInstance && DebugInstance->ActivedSegment)
		{
			UBPNode_GameActionSegmentBase* BPNode_GameActionSegment = DebugInstance->ActivedSegment->GetBPNodeTemplate();
			if (BPNode_GameActionSegment != PreActicedNode.Get())
			{
				if (PreActicedNode.IsValid())
				{
					PreActicedNode->DebugState = UBPNode_GameActionSegmentBase::EDebugState::Deactived;
				}
				PreActicedNode = BPNode_GameActionSegment;
				BPNode_GameActionSegment->DebugState = UBPNode_GameActionSegmentBase::EDebugState::Actived;
			}
		}
		else
		{
			if (PreActicedNode.IsValid())
			{
				PreActicedNode->DebugState = UBPNode_GameActionSegmentBase::EDebugState::Deactived;
				PreActicedNode.Reset();
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
