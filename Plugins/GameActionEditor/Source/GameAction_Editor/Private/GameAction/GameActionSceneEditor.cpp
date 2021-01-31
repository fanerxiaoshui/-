// Fill out your copyright notice in the Description page of Project Settings.


#include "GameAction/GameActionSceneEditor.h"
#include <AssetTypeCategories.h>
#include <Widgets/Docking/SDockTab.h>
#include <SCommonEditorViewportToolbarBase.h>
#include <PropertyEditorModule.h>
#include <EngineUtils.h>
#include <DragAndDrop/AssetDragDropOp.h>
#include <ActorFactories/ActorFactory.h>
#include <GameFramework/Character.h>
#include <ClassViewerFilter.h>
#include <ClassViewerModule.h>
#include <EditorModeManager.h>
#include <EditorModes.h>
#include <Engine/Selection.h>
#include <Kismet2/SClassPickerDialog.h>
#include <IDetailCustomization.h>
#include <DetailLayoutBuilder.h>
#include <DetailCategoryBuilder.h>
#include <IPropertyRowGenerator.h>
#include <IDetailTreeNode.h>
#include <ContentBrowserModule.h>
#include <IContentBrowserSingleton.h>
#include <SceneOutlinerPublicTypes.h>
#include <SceneOutlinerModule.h>
#include <SceneOutlinerDelegates.h>
#include <ThumbnailRendering/SceneThumbnailInfo.h>
#include <ToolMenus.h>
#include <ScopedTransaction.h>
#include <ContentStreaming.h>

#include "Blueprint/GameActionBlueprint.h"
#include "GameAction/GameActionInstance.h"

#define LOCTEXT_NAMESPACE "GameActionScene"

UGameActionSceneFactory::UGameActionSceneFactory()
{
	SupportedClass = UGameActionScene::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UGameActionSceneFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(OwnerType);
	UGameActionScene* GameActionScene = NewObject<UGameActionScene>(InParent, InClass, InName, Flags);
	ACharacter* OwnerTemplate = NewObject<ACharacter>(GameActionScene, OwnerType, UGameActionInstanceBase::GameActionOwnerName, RF_Transactional);
	GameActionScene->OwnerTemplate = OwnerTemplate;
	OwnerTemplate->SetActorLocation(FVector(0.f, 0.f, OwnerTemplate->GetDefaultHalfHeight()));
	return GameActionScene;
}

bool UGameActionSceneFactory::ConfigureProperties()
{
	class FGameActionBlueprintOwnerTypeFilter : public IClassViewerFilter
	{
	public:
		bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
		{
			return !InClass->HasAllClassFlags(CLASS_Abstract | CLASS_Deprecated) && InClass->IsChildOf<ACharacter>();
		}
		bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
		{
			return !InUnloadedClassData->HasAllClassFlags(CLASS_Abstract | CLASS_Deprecated) && InUnloadedClassData->IsChildOf(ACharacter::StaticClass());
		}
	};

	OwnerType = nullptr;

	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
	FClassViewerInitializationOptions Options;

	Options.Mode = EClassViewerMode::ClassPicker;
	Options.ClassFilter = MakeShareable(new FGameActionBlueprintOwnerTypeFilter);
	Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::Dynamic;
	Options.InitiallySelectedClass = ACharacter::StaticClass();

	UClass* ChosenClass = nullptr;
	const bool bPressedOk = SClassPickerDialog::PickClass(LOCTEXT("选择游戏行为所有者类型", "选择游戏行为所有者类型"), Options, ChosenClass, ACharacter::StaticClass());

	if (bPressedOk)
	{
		OwnerType = ChosenClass ? ChosenClass : ACharacter::StaticClass();
	}
	return bPressedOk;
}

FGameActionScene_AssetTypeActions::FGameActionScene_AssetTypeActions()
{

}

FText FGameActionScene_AssetTypeActions::GetName() const
{
	return LOCTEXT("游戏行为情景", "游戏行为情景");
}

UClass* FGameActionScene_AssetTypeActions::GetSupportedClass() const
{
	return UGameActionScene::StaticClass();
}

FColor FGameActionScene_AssetTypeActions::GetTypeColor() const
{
	return FColor::Purple;
}

uint32 FGameActionScene_AssetTypeActions::GetCategories()
{
	return EAssetTypeCategories::Gameplay;
}

void FGameActionScene_AssetTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
	for (UObject* Object : InObjects)
	{
		if (UGameActionScene* Asset = Cast<UGameActionScene>(Object))
		{
			TSharedRef<FGameActionSceneEditor> EditorToolkit = MakeShareable(new FGameActionSceneEditor());
			EditorToolkit->InitGameActionSceneEditor(Mode, EditWithinLevelEditor, Asset);
		}
	}
}

FPreviewGameActionScene::FPreviewGameActionScene(ConstructionValues CVS, UGameActionScene* InGameActionSceneAsset)
	: Super(CVS), GameActionSceneAsset(InGameActionSceneAsset)
{
	check(InGameActionSceneAsset);

	UWorld* World = GetWorld();
	{
		AActor* OwnerTemplate = InGameActionSceneAsset->OwnerTemplate;
		FActorSpawnParameters ActorSpawnParameters;
		ActorSpawnParameters.Name = TEXT("Owner");
		ActorSpawnParameters.Template = OwnerTemplate;
		ActorSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ActorSpawnParameters.bHideFromSceneOutliner = true;
		ActionOwner = World->SpawnActor<ACharacter>(OwnerTemplate->GetClass(), ActorSpawnParameters);
		ActionOwner->SetActorLabel(TEXT("Owner"));
	}

	for (const FGameActionSceneActorData& TemplateData : InGameActionSceneAsset->TemplateDatas)
	{
		if (AActor* TemplateActor = TemplateData.Template)
		{
			FActorSpawnParameters ActorSpawnParameters;
			ActorSpawnParameters.Name = TemplateActor->GetFName();
			ActorSpawnParameters.Template = TemplateActor;
			ActorSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* PreviewActor = World->SpawnActor<AActor>(TemplateActor->GetClass(), TemplateData.GetSpawnTransform(), ActorSpawnParameters);
			TemplateData.PreviewActor = PreviewActor;
			PreviewActor->SetActorLabel(TemplateActor->GetActorLabel());
		}
	}
	
	World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* Actor)
	{
		if (UGameActionScene* GameActionScene = GameActionSceneAsset.Get())
		{
			if (Actor == nullptr || Actor->HasAnyFlags(RF_Transient))
			{
				return;
			}
			if (Actor->GetWorld() != GetWorld() || Actor->GetName() == TEXT("Owner"))
			{
				return;
			}
		
			GameActionScene->Modify();
			FGameActionSceneActorData& ActorData = GameActionScene->TemplateDatas.AddDefaulted_GetRef();
			ActorData.PreviewActor = Actor;
		}
	}));
	DestroyActorHandle = GEngine->OnLevelActorDeleted().AddLambda([this](AActor* Actor)
	{
		if (UGameActionScene* GameActionScene = GameActionSceneAsset.Get())
		{
			const int32 Idx = GameActionScene->TemplateDatas.IndexOfByPredicate([&](const FGameActionSceneActorData& E) { return E.PreviewActor == Actor; });
			if (Idx != INDEX_NONE)
			{
				GameActionScene->Modify();
				GameActionScene->TemplateDatas.RemoveAt(Idx);
			}
		}
	});
	OnObjectsReplacedHandle = GEditor->OnObjectsReplaced().AddLambda([this](const UEditorEngine::ReplacementObjectMap& ObjectMap)
	{
		if (UGameActionScene* GameActionScene = GameActionSceneAsset.Get())
		{
			for (const TPair<UObject*, UObject*>& Pair : ObjectMap)
			{
				// 处理蓝图类的编译
				if (AActor* OldActor = Cast<AActor>(Pair.Key))
				{
					AActor* NewActor = Cast<AActor>(Pair.Value);
					if (ensure(NewActor))
					{
						GameActionScene->TemplateDatas.RemoveAll([&](const FGameActionSceneActorData& E) { return E.PreviewActor == NewActor; });
						if (FGameActionSceneActorData* ActorData = GameActionScene->TemplateDatas.FindByPredicate([&](const FGameActionSceneActorData& E) { return E.PreviewActor == OldActor; }))
						{
							ActorData->PreviewActor = NewActor;
							ActorData->Template = NewActor;
						}
					}
				}
			}
		}
	});
	OnActorLabelChangedHandle = FCoreDelegates::OnActorLabelChanged.AddLambda([=](AActor* Actor)
	{
		if (Actor && Actor->GetWorld() == GetWorld())
		{
			if (UGameActionScene* GameActionScene = GameActionSceneAsset.Get())
			{
				GameActionScene->Modify();
			}
		}
	});
}

FPreviewGameActionScene::~FPreviewGameActionScene()
{
	GEngine->OnLevelActorDeleted().Remove(DestroyActorHandle);
	GEditor->OnObjectsReplaced().Remove(OnObjectsReplacedHandle);
	FCoreDelegates::OnActorLabelChanged.Remove(OnActorLabelChangedHandle);
}

void FPreviewGameActionScene::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	GetWorld()->Tick(LEVELTICK_All, DeltaTime);
}

void FPreviewGameActionScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);

	Collector.AddReferencedObject(ActionOwner);
}

namespace GameActionSceneEditorUtils
{
	const FName DetailsTabId = TEXT("GameActionSceneEditorDetailsTab");
	const FName ViewportTabId = TEXT("GameActionSceneEditorViewportTab");
	const FName BrowserTabId = TEXT("GameActionSceneEditorBrowserTab");
	const FName SceneOutlinerId = TEXT("GameActionSceneEditorSceneOutliner");
}

void FPreviewGameActionScene::SelectActor(AActor* Actor) const
{
	if (EditorViewportClient.IsValid())
	{
		USelection* Selection = EditorViewportClient.Pin()->GetModeTools()->GetSelectedActors();
		Selection->DeselectAll();
		Selection->Select(Actor);
		GEditor->SelectNone(false, false);
		GEditor->SelectActor(Actor, true, true, false, true);
	}
}

FGameActionSceneEditor::FGameActionSceneEditor()
{
	SelectObjectEventHandle = USelection::SelectObjectEvent.AddLambda([=](UObject* Object)
	{
		if (DetailsWidget.IsValid() && Object && Object->GetWorld() == PreviewScene->GetWorld())
		{
			DetailsWidget->SetObject(Object);
		}
	});
}

FGameActionSceneEditor::~FGameActionSceneEditor()
{
	USelection::SelectObjectEvent.Remove(SelectObjectEventHandle);
	SceneOutliner::FSceneOutlinerDelegates::Get().SelectionChanged.Remove(SceneOutlinerSelectChangeHandle);
}

FLinearColor FGameActionSceneEditor::GetWorldCentricTabColorScale() const
{
	return FColor::Purple;
}

FName FGameActionSceneEditor::GetToolkitFName() const
{
	return FName("GameActionSceneEditor");
}

FText FGameActionSceneEditor::GetBaseToolkitName() const
{
	return LOCTEXT("GameActionSceneEditorAppLabel", "Game Action Scene Editor");
}

FString FGameActionSceneEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Game Action Scene").ToString();
}

void FGameActionSceneEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("GameActionSceneEditorWorkspaceMenu", "Stand Template Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	Super::RegisterTabSpawners(InTabManager);

	using namespace GameActionSceneEditorUtils;
	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateLambda([=](const FSpawnTabArgs& Args)
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bUpdatesFromSelection = true;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.NotifyHook = this;
		DetailsViewArgs.bShowActorLabel = true;
		DetailsViewArgs.bCustomNameAreaLocation = true;
		DetailsWidget = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
		DetailsWidget->OnFinishedChangingProperties().AddLambda([=](const FPropertyChangedEvent&)
		{
			PreviewScene->GameActionSceneAsset->Modify();
		});

		class FGameActionSceneActorDetails : public IDetailCustomization
		{
		public:
			FGameActionSceneActorDetails(UGameActionScene* GameActionSceneAsset)
				:GameActionSceneAsset(GameActionSceneAsset)
			{}
			TWeakObjectPtr<UGameActionScene> GameActionSceneAsset;
			TSharedPtr<class IPropertyRowGenerator> PropertyRowGenerator;
			void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
			{
				UGameActionScene* GameActionScene = GameActionSceneAsset.Get();
				check(GameActionScene);
				
				const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = DetailBuilder.GetSelectedObjects();
				if (SelectedObjects.Num() != 1)
				{
					return;
				}

				AActor* Actor = Cast<AActor>(SelectedObjects[0]);
				const int32 DataIdx = GameActionScene->TemplateDatas.IndexOfByPredicate([&](const FGameActionSceneActorData& E) { return E.PreviewActor == Actor; });
				if (DataIdx != INDEX_NONE)
				{
					IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(TEXT("ActorData"), LOCTEXT("行为实体配置", "行为实体配置"), ECategoryPriority::Important);

					const TSharedPtr<IPropertyHandle> DataHandle = [&]()->TSharedPtr<IPropertyHandle>
					{
						FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
						FPropertyRowGeneratorArgs Args;
						Args.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
						PropertyRowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(Args);
						PropertyRowGenerator->SetObjects({ GameActionScene });
						const TArray<TSharedRef<IDetailTreeNode>>& RootTreeNodes = PropertyRowGenerator->GetRootTreeNodes();
						for (const TSharedRef<IDetailTreeNode>& RootTreeNode : RootTreeNodes)
						{
							TArray<TSharedRef<IDetailTreeNode>> Children;
							RootTreeNode->GetChildren(Children);
							for (const TSharedRef<IDetailTreeNode>& Child : Children)
							{
								const FName NodeName = Child->GetNodeName();
								if (NodeName == GET_MEMBER_NAME_CHECKED(UGameActionScene, TemplateDatas))
								{
									return Child->CreatePropertyHandle()->AsArray()->GetElement(DataIdx);
								}
							}
						}
						return nullptr;
					}();
					if (DataHandle.IsValid())
					{
						uint32 ChildNum;
						DataHandle->GetNumChildren(ChildNum);
						for (uint32 Idx = 0; Idx < ChildNum; ++Idx)
						{
							IDetailPropertyRow& DetailPropertyRow = CategoryBuilder.AddProperty(DataHandle->GetChildHandle(Idx));
							DetailPropertyRow.OverrideResetToDefault(FResetToDefaultOverride::Hide());
						}
					}
				}
			}
		};
		DetailsWidget->RegisterInstancedCustomPropertyLayout(AActor::StaticClass(), FOnGetDetailCustomizationInstance::CreateLambda([this]{ return MakeShareable(new FGameActionSceneActorDetails(PreviewScene->GameActionSceneAsset.Get())); }));

		return SNew(SDockTab).TabRole(ETabRole::PanelTab)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SVerticalBox)
						.Visibility_Lambda([=]
						{
							const TArray<TWeakObjectPtr<AActor>>& SelectedActors = DetailsWidget->GetSelectedActors();
							if (SelectedActors.Num() == 1)
							{
								return SelectedActors[0].Get() != PreviewScene->ActionOwner ? EVisibility::Visible : EVisibility::Collapsed;
							}
							return EVisibility::Collapsed;
						})
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(10.f, 2.f)
						[
							SNew(STextBlock)
								.Text(LOCTEXT("Possessable变量名", "Possessable变量名"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							DetailsWidget->GetNameAreaWidget().ToSharedRef()
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						DetailsWidget.ToSharedRef()
					]
				];
	}))
	.SetDisplayName(LOCTEXT("GameActionSceneEditorDetailsTab", "Details"))
	.SetGroup(WorkspaceMenuCategoryRef);

	InTabManager->RegisterTabSpawner(ViewportTabId, FOnSpawnTab::CreateLambda([=](const FSpawnTabArgs& Args)
	{
		TSharedRef<SDockTab> SpawnedTab =
			SNew(SDockTab)
			.Label(LOCTEXT("Viewport_TabTitle", "Viewport"))
			[
				Viewport.ToSharedRef()
			];

		return SpawnedTab;
	}))
	.SetDisplayName(LOCTEXT("GameActionSceneEditorViewportTab", "Viewport"))
	.SetGroup(WorkspaceMenuCategoryRef);

	InTabManager->RegisterTabSpawner(BrowserTabId, FOnSpawnTab::CreateLambda([=](const FSpawnTabArgs& Args)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		FContentBrowserConfig Config;
		Config.bCanShowClasses = false;
		Config.bCanShowRealTimeThumbnails = false;
		Config.InitialAssetViewType = EAssetViewType::Tile;
		Config.ThumbnailScale = 0.2f;
		Config.bCanShowDevelopersFolder = true;
		Config.bCanShowFolders = true;
		Config.bUseSourcesView = true;
		Config.bExpandSourcesView = true;
		Config.bCanShowFilters = true;
		Config.bUsePathPicker = true;
		Config.bShowBottomToolbar = true;
		Config.bCanShowLockButton = false;
		
		TSharedRef<SDockTab> SpawnedTab =
			SNew(SDockTab)
			.Label(LOCTEXT("Browser_TabTitle", "Browser"))
			[
				ContentBrowserModule.Get().CreateContentBrowser(TEXT("GameActionSceneEditorBrowser"), nullptr, &Config)
			];
		return SpawnedTab;
	}))
	.SetDisplayName(LOCTEXT("GameActionSceneEditorBrowserTab", "Browser"))
	.SetGroup(WorkspaceMenuCategoryRef);


	InTabManager->RegisterTabSpawner(SceneOutlinerId, FOnSpawnTab::CreateLambda([=](const FSpawnTabArgs& Args)
	{
		SceneOutliner::FInitializationOptions InitOptions;
		InitOptions.bShowTransient = false;
		InitOptions.Mode = ESceneOutlinerMode::ActorBrowsing;
		InitOptions.SpecifiedWorldToDisplay = PreviewScene->GetWorld();
		InitOptions.CustomDelete.BindLambda([](const TArray<TWeakObjectPtr<AActor>>& Actors)
		{
			FScopedTransaction ScopedTransaction(LOCTEXT("Delete Actors", "Delete Actors"));
			for (const TWeakObjectPtr<AActor>& Actor : Actors)
			{
				if (Actor.IsValid())
				{
					Actor->Destroy();
				}
			}
		});
		SceneOutlinerSelectChangeHandle = SceneOutliner::FSceneOutlinerDelegates::Get().SelectionChanged.AddLambda([=]
		{
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				if (It && It->GetWorld() == PreviewScene->GetWorld())
				{
					if (AActor* Actor = Cast<AActor>(*It))
					{
						PreviewScene->SelectActor(Actor);
					}
				}
			}
		});

		FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::Get().LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");
		TSharedRef<ISceneOutliner> SceneOutlinerRef = SceneOutlinerModule.CreateSceneOutliner(InitOptions, FOnActorPicked());

		return SNew( SDockTab )
			.Icon( FEditorStyle::GetBrush( "LevelEditor.Tabs.Outliner" ) )
			.Label(LOCTEXT("SceneOutlinerTabTitle", "Scene Outliner"))
			[
				SNew(SBorder)
				.Padding(4)
				.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
				[
					SceneOutlinerRef
				]
			];
	}))
	.SetDisplayName(LOCTEXT("GameActionSceneEditorSceneOutlinerTab", "Scene Outliner"))
	.SetGroup(WorkspaceMenuCategoryRef);
}

void FGameActionSceneEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	Super::UnregisterTabSpawners(InTabManager);
	using namespace GameActionSceneEditorUtils;
	InTabManager->UnregisterTabSpawner(DetailsTabId);
	InTabManager->UnregisterTabSpawner(ViewportTabId);
}

void FGameActionSceneEditor::SaveAsset_Execute()
{
	SyncTempateToAsset();
	Super::SaveAsset_Execute();
}

bool FGameActionSceneEditor::OnRequestClose()
{
	SyncTempateToAsset();
	return Super::OnRequestClose();
}

void FGameActionSceneEditor::SyncTempateToAsset()
{
	if (UGameActionScene* GameActionScene = PreviewScene->GameActionSceneAsset.Get())
	{
		if (UPackage* Package = GameActionScene->GetTypedOuter<UPackage>())
		{
			if (Package->IsDirty() == false)
			{
				return;
			}
		}
		
		ACharacter* ActionOwner = PreviewScene->ActionOwner;
		GameActionScene->OwnerTemplate = ::DuplicateObject(ActionOwner, GameActionScene, ActionOwner->GetFName());

		for (int32 Idx = 0; Idx < GameActionScene->TemplateDatas.Num();)
		{
			FGameActionSceneActorData& ActorData = GameActionScene->TemplateDatas[Idx];
			AActor* PreviewActor = ActorData.PreviewActor;
			ActorData.SpawnTransform = PreviewActor->GetActorTransform();
			if (ensure(PreviewActor) && PreviewActor->HasAnyFlags(RF_Transient) == false)
			{
				ActorData.Template = ::DuplicateObject(PreviewActor, GameActionScene, PreviewActor->GetFName());
				++Idx;
			}
			else
			{
				GameActionScene->TemplateDatas.RemoveAt(Idx);
			}
		}
	}
}

void FGameActionSceneEditor::InitGameActionSceneEditor(const EToolkitMode::Type InMode, const TSharedPtr<class IToolkitHost>& InToolkitHost, UGameActionScene* InAsset)
{
	PreviewScene = MakeShareable(new FPreviewGameActionScene(FPreviewGameActionScene::ConstructionValues(), InAsset));
	Viewport = SNew(SGameActionSceneViewport, PreviewScene.ToSharedRef());

	using namespace GameActionSceneEditorUtils;
	TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Layout_GameActionSceneEditor_V1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(FGameActionSceneEditor::GetToolbarTabId(), ETabState::OpenedTab)
				->SetHideTabWell(true)
				->SetSizeCoefficient(0.1f)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Horizontal)
				->SetSizeCoefficient(0.9f)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.8f)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(ViewportTabId, ETabState::OpenedTab)
						->SetHideTabWell(true)
						->SetSizeCoefficient(0.7f)
					)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(BrowserTabId, ETabState::OpenedTab)
						->SetHideTabWell(true)
						->SetSizeCoefficient(0.3f)
					)
				)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.2f)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.3f)
						->SetHideTabWell(true)
						->AddTab(SceneOutlinerId, ETabState::OpenedTab)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.7f)
						->SetHideTabWell(true)
						->AddTab(DetailsTabId, ETabState::OpenedTab)
					)
				)
			)
		);

	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
	ToolbarExtender->AddToolBarExtension(TEXT("Asset"), EExtensionHook::After, GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& ToolbarBuilder)
	{
		ToolbarBuilder.BeginSection("GameActionScene");
		ToolbarBuilder.AddToolBarButton(FUIAction(
			FExecuteAction::CreateLambda([this]
		{
			DetailsWidget->SetObject(PreviewScene->GameActionSceneAsset.Get());
		}),
		FCanExecuteAction::CreateLambda([this]
		{
			return (DetailsWidget->GetSelectedObjects().Num() > 0 && DetailsWidget->GetSelectedObjects()[0] == PreviewScene->GameActionSceneAsset) == false;
		})), TEXT("资源设置"), LOCTEXT("资源设置", "资源设置"), LOCTEXT("资源设置提示", "设置游戏行为情景配置"));
		ToolbarBuilder.EndSection();
	}));
	AddToolbarExtender(ToolbarExtender);
	
	Super::InitAssetEditor(InMode, InToolkitHost, FName("GameActionSceneEditorIdentifier"), Layout, true, true, InAsset);
}

FGameActionSceneViewportClient::FGameActionSceneViewportClient(const TSharedRef<SGameActionSceneViewport>& InViewport, const TSharedRef<FPreviewGameActionScene>& InPreviewScene)
	: Super(nullptr, &InPreviewScene.Get(), InViewport), PreviewScene(InPreviewScene)
{
	SetRealtime(true);

	// Hide grid, we don't need this.
	DrawHelper.bDrawGrid = false;
	DrawHelper.bDrawPivot = false;
	DrawHelper.AxesLineThickness = 5;
	DrawHelper.PivotSize = 5;

	//Initiate view
	SetViewLocation(FVector(75, 75, 75));
	SetViewRotation(FVector(-75, -75, -75).Rotation());

	EngineShowFlags.SetScreenPercentage(true);

	// Set the Default type to Ortho and the XZ Plane
	ELevelViewportType NewViewportType = LVT_Perspective;
	SetViewportType(NewViewportType);

	// View Modes in Persp and Ortho
	SetViewModes(VMI_Lit, VMI_Lit);

	GetModeTools()->SetDefaultMode(FBuiltinEditorModes::EM_Default);
	// SetWidgetMode(FWidget::EWidgetMode::WM_Translate);
}

bool FGameActionSceneViewportClient::InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale)
{
	if (PreviewScene->GameActionSceneAsset.IsValid() == false)
	{
		return false;
	}
	
	if (Super::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale) == false)
	{
		if (CurrentAxis != EAxisList::None)
		{
			for (FSelectionIterator SelectedActorIt(*GetModeTools()->GetSelectedActors()); SelectedActorIt; ++SelectedActorIt)
			{
				if (AActor* InActor = CastChecked<AActor>(*SelectedActorIt))
				{
					if (InActor == PreviewScene->ActionOwner)
					{
						continue;
					}
					PreviewScene->GameActionSceneAsset->Modify();
					
					const bool bIsAltPressed = IsAltPressed();
					const bool bIsShiftPressed = IsShiftPressed();
					const bool bIsCtrlPressed = IsCtrlPressed();
					InActor->EditorApplyRotation(Rot, bIsAltPressed, bIsShiftPressed, bIsCtrlPressed);
					InActor->EditorApplyTranslation(Drag, bIsAltPressed, bIsShiftPressed, bIsCtrlPressed);

					ModeTools->PivotLocation += Drag;
					ModeTools->SnappedLocation += Drag;
				}
			}
			return true;
		}
	}
	return false;
}

void FGameActionSceneViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	Super::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
	if (HitProxy && HitProxy->IsA(HActor::StaticGetType()))
	{
		HActor* ActorHitProxy = (HActor*)HitProxy;
		AActor* ConsideredActor = ActorHitProxy->Actor;
		if (ConsideredActor)
		{
			while (ConsideredActor->IsChildActor())
			{
				ConsideredActor = ConsideredActor->GetParentActor();
			}

			if (ConsideredActor->IsSelectable())
			{
				PreviewScene->SelectActor(ConsideredActor);
			}
		}
	}
}

bool FGameActionSceneViewportClient::InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event, float AmountDepressed, bool bGamepad)
{
	if (Key == EKeys::Delete)
	{
		FScopedTransaction ScopedTransaction(LOCTEXT("Delete Actors", "Delete Actors"));
		for (FSelectionIterator SelectedActorIt(*GetModeTools()->GetSelectedActors()); SelectedActorIt; ++SelectedActorIt)
		{
			if (AActor* Actor = CastChecked<AActor>(*SelectedActorIt))
			{
				Actor->Destroy();
			}
		}
		return true;
	}
	return Super::InputKey(InViewport, ControllerId, Key, Event, AmountDepressed, bGamepad);
}

void SGameActionSceneViewport::Construct(const FArguments& InArgs, const TSharedPtr<FPreviewGameActionScene>& InPreviewScene)
{
	PreviewScene = InPreviewScene;
	Super::Construct(SEditorViewport::FArguments());
}

void SGameActionSceneViewport::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	static bool bDragEnterReentranceGuard = false;
	if (!bDragEnterReentranceGuard)
	{
		bDragEnterReentranceGuard = true;

		if (AActor* Actor = PreviewActor.Get())
		{
			Actor->Destroy();
			PreviewActor.Reset();
		}
		if (ULevel* CurrentLevel = GetWorld()->GetCurrentLevel())
		{
			const TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
			if (Operation.IsValid() && Operation->IsOfType<FAssetDragDropOp>())
			{
				const TSharedPtr<FAssetDragDropOp> AssetDragDropOp = StaticCastSharedPtr<FAssetDragDropOp>(Operation);
				const TArray<FAssetData>& Assets = AssetDragDropOp->GetAssets();
				if (Assets.Num() == 1)
				{
					FAssetData AssetData = Assets[0];
					FIntPoint ViewportOrigin, ViewportSize;
					ActionViewportClient->GetViewportDimensions(ViewportOrigin, ViewportSize);

					CachedOnDropLocalMousePos = MyGeometry.AbsoluteToLocal(DragDropEvent.GetScreenSpacePosition()) * MyGeometry.Scale;
					CachedOnDropLocalMousePos.X -= ViewportOrigin.X;
					CachedOnDropLocalMousePos.Y -= ViewportOrigin.Y;

					UActorFactory* ActorFactory = AssetDragDropOp->GetActorFactory();
					Operation->SetDecoratorVisibility(false);
					AActor* ActorTemplate = [&]
					{
						AActor* Template = nullptr;
						for (UActorFactory* ActorFactory : GEditor->ActorFactories)
						{
							FText UnusedErrorMessage;
							if (ActorFactory->CanCreateActorFrom(AssetData, UnusedErrorMessage))
							{
								Template = ActorFactory->GetDefaultActor(AssetData);
								if (Template)
								{
									break;
								}
							}
						}
						return Template;
					}();
					if (ActorTemplate)
					{
						FActorSpawnParameters ActorSpawnParameters;
						ActorSpawnParameters.Template = ActorTemplate;
						PreviewActor = GetWorld()->SpawnActor<AActor>(ActorTemplate->GetClass(), ActorSpawnParameters);
					}
				}
			}
		}
		bDragEnterReentranceGuard = false;
	}
}

void SGameActionSceneViewport::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	if (AActor* Actor = PreviewActor.Get())
	{
		Actor->Destroy();
		PreviewActor.Reset();
	}
	if (DragDropEvent.GetOperation().IsValid())
	{
		DragDropEvent.GetOperation()->SetDecoratorVisibility(true);
	}
}

FReply SGameActionSceneViewport::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (AActor* Actor = PreviewActor.Get())
	{
		return FReply::Handled();
	}
	return Super::OnDragOver(MyGeometry, DragDropEvent);
}

FReply SGameActionSceneViewport::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (AActor* Actor = PreviewActor.Get())
	{
		PreviewActor.Reset();
		return FReply::Handled();
	}
	return Super::OnDrop(MyGeometry, DragDropEvent);
}

TSharedRef<class SEditorViewport> SGameActionSceneViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SGameActionSceneViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}

TSharedRef<FEditorViewportClient> SGameActionSceneViewport::MakeEditorViewportClient()
{
	ActionViewportClient = MakeShareable(new FGameActionSceneViewportClient(SharedThis(this), PreviewScene.ToSharedRef()));
	PreviewScene->EditorViewportClient = ActionViewportClient;
	return ActionViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SGameActionSceneViewport::MakeViewportToolbar()
{
	class SGameActionSceneEditorViewportToolbar : public SCommonEditorViewportToolbarBase
	{
		using Super = SCommonEditorViewportToolbarBase;
	public:
		void Construct(const FArguments& InArgs, TSharedPtr<class ICommonEditorViewportToolbarInfoProvider> InInfoProvider)
		{
			Super::Construct(InArgs, InInfoProvider);
		}
	};
	return SNew(SGameActionSceneEditorViewportToolbar, SharedThis(this));
}

void UGameActionSceneThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	if (UGameActionScene* Asset = Cast<UGameActionScene>(Object))
	{
		if (ThumbnailScene == nullptr)
		{
			ThumbnailScene = new FAdvancedPreviewScene(FPreviewScene::ConstructionValues());
		}

		if (Asset->OwnerTemplate == nullptr)
		{
			return;
		}

		TArray<AActor*> SpawnedPreviewActors;
		{
			UWorld* World = ThumbnailScene->GetWorld();
			{
				ACharacter* OwnerTemplate = Asset->OwnerTemplate;
				FActorSpawnParameters ActorSpawnParameters;
				ActorSpawnParameters.Template = OwnerTemplate;
				ACharacter* Owner = World->SpawnActor<ACharacter>(OwnerTemplate->GetClass(), ActorSpawnParameters);
				SpawnedPreviewActors.Add(Owner);
			}

			for (const FGameActionSceneActorData& TemplateData : Asset->TemplateDatas)
			{
				if (AActor* TemplateActor = TemplateData.Template)
				{
					FActorSpawnParameters ActorSpawnParameters;
					ActorSpawnParameters.Template = TemplateActor;
					AActor* PreviewActor = World->SpawnActor<AActor>(TemplateActor->GetClass(), TemplateData.GetSpawnTransform(), ActorSpawnParameters);
					SpawnedPreviewActors.Add(PreviewActor);
				}
			}
		}
		
		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(RenderTarget, ThumbnailScene->GetScene(), FEngineShowFlags(ESFIM_Game))
			.SetWorldTimes(FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime(), FApp::GetCurrentTime() - GStartTime)
			.SetResolveScene(true));

		FIntRect ViewRect(
			FMath::Max<int32>(X, 0),
			FMath::Max<int32>(Y, 0),
			FMath::Max<int32>(X + Width, 0),
			FMath::Max<int32>(Y + Height, 0));

		const float FOVDegrees = 30.f;
		const float HalfFOVRadians = FMath::DegreesToRadians<float>(FOVDegrees) * 0.5f;
		const float NearPlane = 1.0f;
		FMatrix ProjectionMatrix = FReversedZPerspectiveMatrix(
			HalfFOVRadians,
			1.0f,
			1.0f,
			NearPlane
		);

		const USceneThumbnailInfo* SceneThumbnailInfo = GetDefault<USceneThumbnailInfo>();
		float OrbitPitch = SceneThumbnailInfo->OrbitPitch;
		float OrbitYaw = SceneThumbnailInfo->OrbitYaw;
		float OrbitZoom = SceneThumbnailInfo->OrbitZoom;

		FSphere SphereBounds = FSphere(FVector(0.f, 0.f, 100.f), 100.f);
		for (AActor* Actor : SpawnedPreviewActors)
		{
			if (Actor)
			{
				Actor->ForEachComponent<UPrimitiveComponent>(true, [&](UPrimitiveComponent* PrimitiveComponent)
				{
					if (PrimitiveComponent->IsVisible())
					{
						PrimitiveComponent->UpdateBounds();
						SphereBounds += PrimitiveComponent->Bounds.GetSphere();
					}
				});
			}
		}
		FVector Origin = -SphereBounds.Center;
		const float HalfMeshSize = SphereBounds.W * 1.f;
		const float TargetDistance = HalfMeshSize / FMath::Tan(HalfFOVRadians);
		OrbitZoom += TargetDistance;

		// Ensure a minimum camera distance to prevent problems with really small objects
		const float MinCameraDistance = 48;
		OrbitZoom = FMath::Max<float>(MinCameraDistance, OrbitZoom);

		const FRotator RotationOffsetToViewCenter(0.f, 90.f, 0.f);
		FMatrix ViewRotationMatrix = FRotationMatrix(FRotator(0, OrbitYaw, 0)) *
			FRotationMatrix(FRotator(0, 0, OrbitPitch)) *
			FTranslationMatrix(FVector(0, OrbitZoom, 0)) *
			FInverseRotationMatrix(RotationOffsetToViewCenter);

		ViewRotationMatrix = ViewRotationMatrix * FMatrix(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));

		Origin -= ViewRotationMatrix.InverseTransformPosition(FVector::ZeroVector);
		ViewRotationMatrix = ViewRotationMatrix.RemoveTranslation();

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.ViewFamily = &ViewFamily;
		ViewInitOptions.SetViewRectangle(ViewRect);
		ViewInitOptions.ViewOrigin = -Origin;
		ViewInitOptions.ViewRotationMatrix = ViewRotationMatrix;
		ViewInitOptions.ProjectionMatrix = ProjectionMatrix;
		ViewInitOptions.BackgroundColor = FLinearColor::Black;

		FSceneView* NewView = new FSceneView(ViewInitOptions);

		ViewFamily.Views.Add(NewView);

		NewView->StartFinalPostprocessSettings(ViewInitOptions.ViewOrigin);
		NewView->EndFinalPostprocessSettings(ViewInitOptions);

		IStreamingManager::Get().AddViewInformation(Origin, Width, Width / FMath::Tan(FOVDegrees));

		RenderViewFamily(Canvas, &ViewFamily);

		for (AActor* Actor : SpawnedPreviewActors)
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}
	}
}

void UGameActionSceneThumbnailRenderer::BeginDestroy()
{
	if (ThumbnailScene != nullptr)
	{
		delete ThumbnailScene;
		ThumbnailScene = nullptr;
	}

	Super::BeginDestroy();
}

#undef LOCTEXT_NAMESPACE
