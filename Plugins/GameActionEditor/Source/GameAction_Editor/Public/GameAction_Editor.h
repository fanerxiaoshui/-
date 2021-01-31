// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Templates/SubclassOf.h"
#include "Framework/Commands/Commands.h"
#include "Editor/GameActionEditorStyle.h"

// 自定义PossessableActorData继承该类型向GameAction_Editor注册
class GAMEACTION_EDITOR_API FCustomPossessableActorDataFactory
{
public:
	FCustomPossessableActorDataFactory();
	
	UStruct* DataType;
	virtual void SetDataValue(const AActor* Actor, struct FGameActionPossessableActorData& ActorData) const;
};

class FGameAction_EditorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	void StartupModule() override;
	void ShutdownModule() override;

	static FGameAction_EditorModule& GetChecked() { return FModuleManager::GetModuleChecked<FGameAction_EditorModule>(TEXT("GameAction_Editor")); }
	static FGameAction_EditorModule* GetPtr() { return FModuleManager::GetModulePtr<FGameAction_EditorModule>(TEXT("GameAction_Editor")); }
	void RegisterPossessableData(const TSubclassOf<AActor>& Type, const TSharedRef<FCustomPossessableActorDataFactory>& Factory);
	void UnregisterPossessableData(const TSubclassOf<AActor>& Type);
	const FCustomPossessableActorDataFactory& GetPossessableDataFactory(const TSubclassOf<AActor>& Type) const;

	TSubclassOf<class UGameActionSegmentBase> DefaultSegment;
private:
	TSharedPtr<class FGameActionBlueprint_AssetTypeActions> GameActionBlueprint_AssetTypeActions;
	TSharedPtr<class FGameActionScene_AssetTypeActions> GameActionScene_AssetTypeActions;
	TSharedPtr<struct FGameActionGraphNodeFactory> GameActionGraphNodeFactory;

	FDelegateHandle GameActionKeyEventTrackEditorHandle;
	FDelegateHandle GameActionStateEventTrackEditorHandle;
	FDelegateHandle GameActionTimeTestingTrackEditorHandle;
	FDelegateHandle GameActionSpawnTrackEditorHandle;
	FDelegateHandle GameActionAnimationTrackEditorHandle;
	FDelegateHandle GameActionTrackEditorHackHandle;
	FDelegateHandle ContentBrowserExtenderDelegateHandle;

	friend class FGameActionEditorStyle;
	FGameActionEditorStyle GameActionEditorStyle;

	TMap<TSubclassOf<AActor>, TSharedRef<FCustomPossessableActorDataFactory>> PossessableDataFactories;
	FCustomPossessableActorDataFactory DefaultActorDataFactory;
};

class FGameActionEditorCommands : public TCommands<FGameActionEditorCommands>
{
	using Super = TCommands<FGameActionEditorCommands>;
public:
	FGameActionEditorCommands();

	void RegisterCommands() override;

	// File-ish commands
	TSharedPtr< FUICommandInfo > EnableSimulation;
};
