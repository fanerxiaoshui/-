// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameAction_Editor.h"
#include <AssetToolsModule.h>
#include <IAssetTools.h>
#include <KismetCompiler.h>
#include <ISequencerModule.h>
#include <SequencerChannelInterface.h>
#include <MovieSceneClipboard.h>
#include <PropertyEditorModule.h>
#include <EdGraphUtilities.h>
#include <EditorModeRegistry.h>
#include <ContentBrowserModule.h>

#include "Editor/GameActionEdMode.h"
#include "Blueprint/BPNode_GameActionTransition.h"
#include "Blueprint/GameActionBlueprint.h"
#include "Blueprint/GameActionSegmentNodeDetails.h"
#include "GameAction/GameActionFactory.h"
#include "GameAction/GameActionCompilerContext.h"
#include "GameAction/GameActionSceneEditor.h"
#include "GameAction/GameActionDetailCustomization.h"
#include "GameAction/GameActionType.h"
#include "Sequencer/GameActionEventTrackEditor.h"
#include "Sequence/GameActionEventTrack.h"
#include "Sequencer/GameActionAnimationTrackEditor.h"
#include "Sequencer/GameActionTimeTestingTrackEditor.h"
#include "Sequencer/GameActionSpawnTrackEditor.h"
#include "Sequencer/GameActionTrackEditorHack.h"

#define LOCTEXT_NAMESPACE "FGameAction_EditorModule"

namespace MovieSceneClipboard
{
	template<> inline FName GetKeyTypeName<FGameActionKeyEventValue>()
	{
		return "GameActionKeyEvent";
	}
	
	template<> inline FName GetKeyTypeName<FGameActionStateEventInnerKeyValue>()
	{
		return "GameActionStateEventInnerKeyValue";
	}
}

FCustomPossessableActorDataFactory::FCustomPossessableActorDataFactory()
	: DataType(StaticStruct<FGameActionPossessableActorData>())
{}

void FCustomPossessableActorDataFactory::SetDataValue(const AActor* Actor, FGameActionPossessableActorData& ActorData) const
{
	if (USceneComponent* RootComponent = Actor->GetRootComponent())
	{
		ActorData.RelativeLocation = RootComponent->GetRelativeLocation();
		ActorData.RelativeRotation = RootComponent->GetRelativeRotation();
	}
}

void FGameAction_EditorModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FKismetCompilerContext::RegisterCompilerForBP(UGameActionBlueprint::StaticClass(), [](UBlueprint* BP, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions)
	{ 
		return MakeShareable(new FGameActionCompilerContext(CastChecked<UGameActionBlueprint>(BP), InMessageLog, InCompileOptions)); 
	});

	if (GIsEditor == false)
	{
		return;
	}
	
	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
	{
		GameActionBlueprint_AssetTypeActions = MakeShareable(new FGameActionBlueprint_AssetTypeActions());
		AssetTools.RegisterAssetTypeActions(GameActionBlueprint_AssetTypeActions.ToSharedRef());

		GameActionScene_AssetTypeActions = MakeShareable(new FGameActionScene_AssetTypeActions());
		AssetTools.RegisterAssetTypeActions(GameActionScene_AssetTypeActions.ToSharedRef());
	}

	ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
	{
		SequencerModule.RegisterChannelInterface<FGameActionKeyEventChannel>();
		SequencerModule.RegisterChannelInterface<FGameActionStateEventInnerKeyChannel>();
		GameActionKeyEventTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FGameActionKeyEventTrackEditor::CreateTrackEditor));
		GameActionStateEventTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FGameActionStateEventTrackEditor::CreateTrackEditor));
		GameActionTimeTestingTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FGameActionTimeTestingTrackEditor::CreateTrackEditor));
		GameActionSpawnTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FGameActionSpawnTrackEditor::CreateTrackEditor));
		GameActionAnimationTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FGameActionAnimationTrackEditor::CreateTrackEditor));
		GameActionTrackEditorHackHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FGameActionTrackEditorHack::CreateTrackEditor));
	}

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	{
		PropertyModule.RegisterCustomPropertyTypeLayout(TEXT("GameActionKeyEventValue"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FGameActionKeyEventValueCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(TEXT("GameActionStateEvent"), FOnGetDetailCustomizationInstance::CreateStatic(&FGameActionStateEventDetails::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(TEXT("GameActionInstanceBase"), FOnGetDetailCustomizationInstance::CreateStatic(&FGameActionInstanceDetails::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(TEXT("BPNode_GameActionTransitionBase"), FOnGetDetailCustomizationInstance::CreateStatic(&FGameActionTransitionNodeBaseDetails::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(TEXT("BPNode_GameActionTransition"), FOnGetDetailCustomizationInstance::CreateStatic(&FGameActionTransitionNodeDetails::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(TEXT("BPNode_GameActionSegmentBase"), FOnGetDetailCustomizationInstance::CreateStatic(&FGameActionSegmentNodeDetails::MakeInstance));
	}

	GameActionGraphNodeFactory = MakeShareable(new FGameActionGraphNodeFactory());
	FEdGraphUtilities::RegisterVisualNodeFactory(GameActionGraphNodeFactory);

	FEditorModeRegistry& ModeRegistry = FEditorModeRegistry::Get();
	{
		ModeRegistry.RegisterMode<FGameActionDefaultEdMode>(FGameActionDefaultEdMode::ModeId, LOCTEXT("GameActionDefaultEdMode", "GameActionDefaultEdMode"), FSlateIcon(), false);
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	{
		TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
		CBMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateLambda([](const TArray<FAssetData>& SelectedAssets)
		{
			TSharedRef<FExtender> Extender(new FExtender());
			if (SelectedAssets.Num() == 1)
			{
				if (UGameActionBlueprint* Blueprint = Cast<UGameActionBlueprint>(SelectedAssets[0].GetAsset()))
				{
					Extender->AddMenuExtension(
					TEXT("GetAssetActions"),
					EExtensionHook::After,
					nullptr,
					FMenuExtensionDelegate::CreateLambda([=](FMenuBuilder& MenuBuilder)
						{
							MenuBuilder.AddMenuEntry(
								LOCTEXT("RetargetGameActionScene_Menu", "重定向行为情景"),
								LOCTEXT("RetargetGameActionScene_Tooltip", "重定向行为情景"),
								FSlateIcon(), 
								FUIAction(FExecuteAction::CreateLambda([=]()
								{
									FScopedTransaction ScopedTransaction(LOCTEXT("重定向行为情景", "重定向行为情景"));
									Blueprint->Modify();
									if (FGameActionBlueprint_AssetTypeActions::RetargetGameActionScene(Blueprint))
									{
										Blueprint->Status = EBlueprintStatus::BS_Dirty;
										Blueprint->GetOutermost()->MarkPackageDirty();
									}
								})));
						}));
				}
			}
			return Extender;
		}));
		ContentBrowserExtenderDelegateHandle = CBMenuExtenderDelegates.Last().GetHandle();
	}

	UThumbnailManager& ThumbnailManager = UThumbnailManager::Get();
	{
		ThumbnailManager.RegisterCustomRenderer(UGameActionScene::StaticClass(), UGameActionSceneThumbnailRenderer::StaticClass());
		ThumbnailManager.RegisterCustomRenderer(UGameActionBlueprint::StaticClass(), UGameActionBlueprintThumbnailRenderer::StaticClass());
	}
	
	FGameActionEditorCommands::Register();
}

void FGameAction_EditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	if (FAssetToolsModule::IsModuleLoaded())
	{
		IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
		AssetTools.UnregisterAssetTypeActions(GameActionBlueprint_AssetTypeActions.ToSharedRef());
		AssetTools.UnregisterAssetTypeActions(GameActionScene_AssetTypeActions.ToSharedRef());
	}

	if (ISequencerModule* SequencerModule = FModuleManager::LoadModulePtr<ISequencerModule>("Sequencer"))
	{
		SequencerModule->UnregisterSequenceEditor(GameActionKeyEventTrackEditorHandle);
		SequencerModule->UnregisterSequenceEditor(GameActionStateEventTrackEditorHandle);
		SequencerModule->UnregisterSequenceEditor(GameActionTimeTestingTrackEditorHandle);
		SequencerModule->UnregisterSequenceEditor(GameActionSpawnTrackEditorHandle);
		SequencerModule->UnregisterSequenceEditor(GameActionAnimationTrackEditorHandle);
		SequencerModule->UnregisterSequenceEditor(GameActionTrackEditorHackHandle);
	}

	if (FPropertyEditorModule* PropertyModule = FModuleManager::LoadModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		PropertyModule->UnregisterCustomPropertyTypeLayout(TEXT("GameActionKeyEventValue"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("GameActionStateEvent"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("GameActionInstanceBase"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("BPNode_GameActionTransitionBase"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("BPNode_GameActionTransition"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("BPNode_GameActionSegmentBase"));
	}

	FEdGraphUtilities::UnregisterVisualNodeFactory(GameActionGraphNodeFactory);

	FEditorModeRegistry& ModeRegistry = FEditorModeRegistry::Get();
	{
		ModeRegistry.UnregisterMode(FGameActionDefaultEdMode::ModeId);
	}

	if (FModuleManager::Get().IsModuleLoaded("ContentBrowser"))
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
		CBMenuExtenderDelegates.RemoveAll([this](const FContentBrowserMenuExtender_SelectedAssets& Delegate) { return Delegate.GetHandle() == ContentBrowserExtenderDelegateHandle; });
	}

	if (UObjectInitialized())
	{
		UThumbnailManager::Get().UnregisterCustomRenderer(UGameActionScene::StaticClass());
		UThumbnailManager::Get().UnregisterCustomRenderer(UGameActionBlueprint::StaticClass());
	}
	
	FGameActionEditorCommands::Unregister();
}

void FGameAction_EditorModule::RegisterPossessableData(const TSubclassOf<AActor>& Type, const TSharedRef<FCustomPossessableActorDataFactory>& Factory)
{
	PossessableDataFactories.Add(Type, Factory);
}

void FGameAction_EditorModule::UnregisterPossessableData(const TSubclassOf<AActor>& Type)
{
	PossessableDataFactories.Remove(Type);
}

const FCustomPossessableActorDataFactory& FGameAction_EditorModule::GetPossessableDataFactory(const TSubclassOf<AActor>& Type) const
{
	check(Type);
	for (UClass* Class = Type; Class != UObject::StaticClass(); Class = Class->GetSuperClass())
	{
		if (const TSharedRef<FCustomPossessableActorDataFactory>* Factory = PossessableDataFactories.Find(Class))
		{
			return Factory->Get();
		}
	}
	return DefaultActorDataFactory;
}

FGameActionEditorCommands::FGameActionEditorCommands()
	: Super(TEXT("GameActionEditor"), LOCTEXT("GameActionEditor", "Game Action Editor"), NAME_None, TEXT("GameActionEditorStyle"))
{}

void FGameActionEditorCommands::RegisterCommands()
{
	UI_COMMAND(EnableSimulation, "Simulation", "Enables the simulation of the blueprint and ticking", EUserInterfaceActionType::ToggleButton, FInputChord());
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FGameAction_EditorModule, GameAction_Editor)