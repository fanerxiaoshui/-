// Fill out your copyright notice in the Description page of Project Settings.


#include "GameAction/GameActionFactory.h"
#include <AssetTypeCategories.h>
#include <Kismet2/SClassPickerDialog.h>
#include <Kismet2/KismetEditorUtilities.h>
#include <Kismet2/BlueprintEditorUtils.h>
#include <GameFramework/Character.h>
#include <Widgets/SCompoundWidget.h>
#include <IContentBrowserSingleton.h>
#include <ContentBrowserModule.h>
#include <ClassViewerModule.h>
#include <ClassViewerFilter.h>
#include <Misc/MessageDialog.h>
#include <EdGraphSchema_K2.h>
#include <AdvancedPreviewScene.h>
#include <ThumbnailRendering/SceneThumbnailInfo.h>
#include <Widgets/Input/SCheckBox.h>
#include <Widgets/Input/SButton.h>
#include <ContentStreaming.h>
#include <Widgets/Layout/SBox.h>
#include <EditorStyleSet.h>
#include <Widgets/Text/STextBlock.h>

#include "GameAction_Editor.h"
#include "Editor/GameActionEditor.h"
#include "Blueprint/GameActionBlueprint.h"
#include "Blueprint/GameActionGeneratedClass.h"
#include "Blueprint/BPNode_GameActionSegment.h"
#include "Blueprint/BPNode_GameActionEntry.h"
#include "Blueprint/EdGraph_GameAction.h"
#include "Blueprint/EdGraphSchema_GameAction.h"
#include "Factories/AnimBlueprintFactory.h"
#include "GameAction/GameActionComponent.h"
#include "GameAction/GameActionInstance.h"
#include "GameAction/GameActionSegment.h"
#include "Sequence/GameActionSequence.h"
#include "Sequence/GameActionSequencePlayer.h"
#include "Widgets/Layout/SUniformGridPanel.h"

#define LOCTEXT_NAMESPACE "GameActionBlueprint"

namespace GameActionAssetUtils
{
	FAssetPickerConfig GetGameActionScenePickerConfig(const TSubclassOf<UGameActionInstanceBase>& ParentClass)
	{
		FAssetPickerConfig AssetPickerConfig;
		AssetPickerConfig.Filter.ClassNames.Add(UGameActionScene::StaticClass()->GetFName());
		AssetPickerConfig.OnShouldFilterAsset.BindLambda([=](const FAssetData& AssetData)
		{
			if (ParentClass)
			{
				if (UGameActionScene* GameActionScene = Cast<UGameActionScene>(AssetData.GetAsset()))
				{
					for (TFieldIterator<FObjectProperty> It(ParentClass); It; ++It)
					{
						if (It->GetBoolMetaData(UGameActionBlueprint::MD_GameActionPossessableReference))
						{
							if (GameActionScene->TemplateDatas.ContainsByPredicate([&](const FGameActionSceneActorData& E)
							{
								if (E.bAsPossessable && E.Template)
								{
									return E.Template->GetFName() == It->GetFName();
								}
								return false;
							}) == false)
							{
								return true;
							}
						}
					}
				}
			}
			return false;
		});
		AssetPickerConfig.SelectionMode = ESelectionMode::Single;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::Column;
		return AssetPickerConfig;
	}
}

UGameActionBlueprintFactory::UGameActionBlueprintFactory()
	: bCreateAbstract(false)
{
	SupportedClass = UGameActionBlueprint::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

FString UGameActionBlueprintFactory::GetDefaultNewAssetName() const
{
	return TEXT("NewGameActionBlueprint");
}

UObject* UGameActionBlueprintFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	check(ParentClass);
	UGameActionBlueprint* GameActionBlueprint = CastChecked<UGameActionBlueprint>(FKismetEditorUtilities::CreateBlueprint(ParentClass, InParent, Name, EBlueprintType::BPTYPE_Normal, UGameActionBlueprint::StaticClass(), UGameActionGeneratedClass::StaticClass(), CallingContext));
	FBlueprintEditorUtils::RemoveGraphs(GameActionBlueprint, GameActionBlueprint->UbergraphPages);
	GameActionBlueprint->GameActionScene = GameActionScene;

	UEdGraph* EventGraph = FBlueprintEditorUtils::CreateNewGraph(GameActionBlueprint, TEXT("EventGraph"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddUbergraphPage(GameActionBlueprint, EventGraph);
	GameActionBlueprint->LastEditedDocuments.Add(EventGraph);
	{
		int32 NextNodePosY = 0;

		FKismetEditorUtilities::AddDefaultEventNode(GameActionBlueprint, EventGraph, GET_FUNCTION_NAME_CHECKED(UGameActionInstanceBase, ReceiveWhenActived), UGameActionInstanceBase::StaticClass(), NextNodePosY);
		FKismetEditorUtilities::AddDefaultEventNode(GameActionBlueprint, EventGraph, GET_FUNCTION_NAME_CHECKED(UGameActionInstanceBase, ReceiveWhenDeactived), UGameActionInstanceBase::StaticClass(), NextNodePosY);
		FKismetEditorUtilities::AddDefaultEventNode(GameActionBlueprint, EventGraph, GET_FUNCTION_NAME_CHECKED(UGameActionInstanceBase, ReceiveTick), UGameActionInstanceBase::StaticClass(), NextNodePosY);
	}

	if (UGameActionBlueprint* ParentBlueprint = Cast<UGameActionBlueprint>(ParentClass->ClassGeneratedBy))
	{
		// 行为蓝图子类不生成行为图表
		if (ParentBlueprint->IsRootBlueprint() || ParentBlueprint->FindRootBlueprint() != nullptr)
		{
			return GameActionBlueprint;
		}
	}

	if (bCreateAbstract)
	{
		// 行为蓝图虚类不生成行为图表
		GameActionBlueprint->bGenerateAbstractClass = true;
		return GameActionBlueprint;
	}
	
	UEdGraph_GameAction* GameActionGraph = CastChecked<UEdGraph_GameAction>(FBlueprintEditorUtils::CreateNewGraph(GameActionBlueprint, TEXT("GameActionGraph"), UEdGraph_GameAction::StaticClass(), UEdGraphSchema_GameAction::StaticClass()));
	GameActionBlueprint->GameActionGraph = GameActionGraph;
	FBlueprintEditorUtils::AddUbergraphPage(GameActionBlueprint, GameActionGraph);
	GameActionBlueprint->LastEditedDocuments.Add(GameActionGraph);
	{
		int32 NextNodePosY = 0;
		for (TFieldIterator<FStructProperty> It(ParentClass); It; ++It)
		{
			if (It->Struct->IsChildOf(StaticStruct<FGameActionEntry>()))
			{
				FGraphNodeCreator<UBPNode_GameActionEntry> GameActionEntryCreator(*GameActionGraph);
				UBPNode_GameActionEntry* GameActionNodeEntry = GameActionEntryCreator.CreateNode();
				GameActionNodeEntry->CustomFunctionName = *FString::Printf(TEXT("__%s_ActionEntry"), *It->GetDisplayNameText().ToString());
				GameActionNodeEntry->NodePosX = 0;
				GameActionNodeEntry->NodePosY = NextNodePosY;
				GameActionNodeEntry->EntryTransitionProperty = *It;
				GameActionEntryCreator.Finalize();

				FGraphNodeCreator<UBPNode_GameActionSegment> GameActionNodeCreator(*GameActionGraph);
				UBPNode_GameActionSegment* GameActionSegmentNode = GameActionNodeCreator.CreateNode();
				TSubclassOf<class UGameActionSegmentBase> DefaultSegment = FGameAction_EditorModule::GetChecked().DefaultSegment;
				GameActionSegmentNode->GameActionSegment = NewObject<UGameActionSegmentBase>(GameActionSegmentNode, DefaultSegment ? *DefaultSegment : UGameActionSegment::StaticClass(), GET_MEMBER_NAME_CHECKED(UBPNode_GameActionSegment, GameActionSegment), RF_Transactional);
				GameActionSegmentNode->GetGameActionSequence()->SetOwnerCharacter(GameActionBlueprint->GetOwnerType());
				GameActionSegmentNode->NodePosX = 300;
				GameActionSegmentNode->NodePosY = NextNodePosY;
				GameActionNodeCreator.Finalize();

				const UEdGraphSchema_GameAction* GraphSchema_GameAction = GetDefault<UEdGraphSchema_GameAction>();
				GraphSchema_GameAction->CreateAutomaticConversionNodeAndConnections(GameActionNodeEntry->FindPinChecked(UEdGraphSchema_K2::PN_Then), GameActionSegmentNode->GetExecPin());

				NextNodePosY += 200.f;
			}
		}
	}
	
	FKismetEditorUtilities::CompileBlueprint(GameActionBlueprint);

	return GameActionBlueprint;
}

UObject* UGameActionBlueprintFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return FactoryCreateNew(InClass, InParent, InName, Flags, Context, Warn, NAME_None);
}

bool UGameActionBlueprintFactory::ConfigureProperties()
{
	class SGameActionBlueprintCreateDialog : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS( SGameActionBlueprintCreateDialog ){}

		SLATE_END_ARGS()

		/** Constructs this widget with InArgs */
		void Construct( const FArguments& InArgs )
		{
			bOkClicked = false;
			ParentClass = UGameActionInstanceBase::StaticClass();

			ChildSlot
			[
				SNew(SBorder)
				.Visibility(EVisibility::Visible)
				.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
				[
					SNew(SBox)
					.Visibility(EVisibility::Visible)
					.WidthOverride(500.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.FillHeight(1)
						[
							SNew(SBorder)
							.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
							.Content()
							[
								SAssignNew(ParentClassContainer, SVerticalBox)
							]
						]
						+ SVerticalBox::Slot()
						.FillHeight(1)
						.Padding(0.0f, 10.0f, 0.0f, 0.0f)
						[
							SNew(SBorder)
							.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
							.Content()
							[
								SAssignNew(GameActionSceneContainer, SVerticalBox)
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 10.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SCheckBox)
								.IsChecked_Lambda([this]
								{
									return GameActionBlueprintFactory->bCreateAbstract ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								})
								.OnCheckStateChanged_Lambda([this](ECheckBoxState CheckBoxState)
								{
									GameActionBlueprintFactory->bCreateAbstract = CheckBoxState == ECheckBoxState::Checked ? true : false;
								})
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(STextBlock)
								.Text(LOCTEXT("创建为虚类型", "创建为虚类型"))
							]
						]

						// Ok/Cancel buttons
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Bottom)
						.Padding(8)
						[
							SNew(SUniformGridPanel)
							.SlotPadding(FEditorStyle::GetMargin("StandardDialog.SlotPadding"))
							.MinDesiredSlotWidth(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
							.MinDesiredSlotHeight(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
							+ SUniformGridPanel::Slot(0,0)
							[
								SNew(SButton)
								.HAlign(HAlign_Center)
								.ContentPadding( FEditorStyle::GetMargin("StandardDialog.ContentPadding") )
								.OnClicked(this, &SGameActionBlueprintCreateDialog::OkClicked)	
								.Text(LOCTEXT("CreateGameActionBlueprintOk", "OK"))
							]
							+ SUniformGridPanel::Slot(1,0)
							[
								SNew(SButton)
								.HAlign(HAlign_Center)
								.ContentPadding( FEditorStyle::GetMargin("StandardDialog.ContentPadding") )
								.OnClicked(this, &SGameActionBlueprintCreateDialog::CancelClicked)
								.Text(LOCTEXT("CreateGameActionBlueprintCancel", "Cancel"))
							]
						]
					]
				]
			];

			MakeParentClassPicker();
			MakeGameActionScenePicker();
		}
	
		/** Sets properties for the supplied GameActionBlueprintFactory */
		bool ConfigureProperties(TWeakObjectPtr<UGameActionBlueprintFactory> InGameActionBlueprintFactory)
		{
			GameActionBlueprintFactory = InGameActionBlueprintFactory;

			TSharedRef<SWindow> Window = SNew(SWindow)
			.Title( LOCTEXT("CreateGameActionBlueprintOptions", "创建游戏行为蓝图") )
			.ClientSize(FVector2D(400, 700))
			.SupportsMinimize(false) .SupportsMaximize(false)
			[
				AsShared()
			];

			PickerWindow = Window;

			GEditor->EditorAddModalWindow(Window);
			GameActionBlueprintFactory.Reset();

			return bOkClicked;
		}

	private:
		void MakeParentClassPicker()
		{
			FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

			FClassViewerInitializationOptions Options;
			Options.Mode = EClassViewerMode::ClassPicker;
			Options.bIsBlueprintBaseOnly = false;
			Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;
			class FGameActionBlueprintParentFilter : public IClassViewerFilter
			{
			public:
				bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
				{
					return InClass->HasAnyClassFlags(CLASS_Abstract) && InClass->IsChildOf<UGameActionInstanceBase>();
				}
				bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
				{
					return InUnloadedClassData->HasAnyClassFlags(CLASS_Abstract) && InUnloadedClassData->IsChildOf(UGameActionInstanceBase::StaticClass());
				}
			};
			Options.ClassFilter = MakeShareable(new FGameActionBlueprintParentFilter());
			ParentClassContainer->ClearChildren();
			ParentClassContainer->AddSlot()
			.AutoHeight()
			[
				SNew( STextBlock )
				.Text( LOCTEXT("选择游戏行为类型", "选择游戏行为类型:") )
				.ShadowOffset( FVector2D(1.0f, 1.0f) )
			];

			ParentClassContainer->AddSlot()
			[
				ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateLambda([=](UClass* ChosenClass)
				{
					ParentClass = ChosenClass;
					MakeGameActionScenePicker();
				}))
			];
		}

		void MakeGameActionScenePicker()
		{
			FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

			FAssetPickerConfig AssetPickerConfig = GameActionAssetUtils::GetGameActionScenePickerConfig(ParentClass.Get());
			AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([=](const FAssetData& AssetData)
			{
				GameActionScene = AssetData;
			});

			GameActionSceneContainer->ClearChildren();
			GameActionSceneContainer->AddSlot()
			.AutoHeight()
			[
				SNew( STextBlock )
				.Text( LOCTEXT("选择游戏行为情景", "选择游戏行为情景:") )
				.ShadowOffset( FVector2D(1.0f, 1.0f) )
			];

			GameActionSceneContainer->AddSlot()
			[
				ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
			];
		}

		FReply OkClicked()
		{
			if (GameActionScene == nullptr)
			{
				FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("必须选择一个游戏行为情景", "必须选择一个游戏行为情景"));
				return FReply::Handled();
			}
			if (GameActionBlueprintFactory.IsValid())
			{
				GameActionBlueprintFactory->ParentClass = ParentClass.Get();
				GameActionBlueprintFactory->GameActionScene = Cast<UGameActionScene>(GameActionScene.GetAsset());
			}

			CloseDialog(true);

			return FReply::Handled();
		}

		void CloseDialog(bool bWasPicked=false)
		{
			bOkClicked = bWasPicked;
			if ( PickerWindow.IsValid() )
			{
				PickerWindow.Pin()->RequestDestroyWindow();
			}
		}

		/** Handler for when cancel is clicked */
		FReply CancelClicked()
		{
			CloseDialog();
			return FReply::Handled();
		}

		FReply OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent )
		{
			if (InKeyEvent.GetKey() == EKeys::Escape)
			{
				CloseDialog();
				return FReply::Handled();
			}
			return SWidget::OnKeyDown(MyGeometry, InKeyEvent);
		}

	private:
		TWeakObjectPtr<UGameActionBlueprintFactory> GameActionBlueprintFactory;

		TWeakPtr<SWindow> PickerWindow;

		TSharedPtr<SVerticalBox> ParentClassContainer;
		TSharedPtr<SVerticalBox> OwnerTypeContainer;
		TSharedPtr<SVerticalBox> GameActionSceneContainer;

		TWeakObjectPtr<UClass> ParentClass;
		FAssetData GameActionScene;

		bool bOkClicked;
	};

	TSharedRef<SGameActionBlueprintCreateDialog> Dialog = SNew(SGameActionBlueprintCreateDialog);
	return Dialog->ConfigureProperties(this);
}

FGameActionBlueprint_AssetTypeActions::FGameActionBlueprint_AssetTypeActions()
{

}

FText FGameActionBlueprint_AssetTypeActions::GetName() const
{
	return LOCTEXT("游戏行为蓝图", "游戏行为蓝图");
}

FText FGameActionBlueprint_AssetTypeActions::GetDisplayNameFromAssetData(const FAssetData& AssetData) const
{
	return LOCTEXT("游戏行为蓝图", "游戏行为蓝图");
}

UClass* FGameActionBlueprint_AssetTypeActions::GetSupportedClass() const
{
	return UGameActionBlueprint::StaticClass();
}

FColor FGameActionBlueprint_AssetTypeActions::GetTypeColor() const
{
	return FColor(255, 0, 255);
}

uint32 FGameActionBlueprint_AssetTypeActions::GetCategories()
{
	return EAssetTypeCategories::Gameplay;
}

void FGameActionBlueprint_AssetTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (UObject* Object : InObjects)
	{
		if (UGameActionBlueprint* GameActionBlueprint = Cast<UGameActionBlueprint>(Object))
		{
			bool bLetOpen = true;
			if (!GameActionBlueprint->SkeletonGeneratedClass || !GameActionBlueprint->GeneratedClass)
			{
				bLetOpen = EAppReturnType::Yes == FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("FailedToLoadBlueprintWithContinue", "Blueprint could not be loaded because it derives from an invalid class.  Check to make sure the parent class for this blueprint hasn't been removed! Do you want to continue (it can crash the editor)?"));
			}
			if (GameActionBlueprint->GameActionScene == nullptr)
			{
				bLetOpen = EAppReturnType::Yes == FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("GameActionScene Missing Message", "游戏行为情景丢失，是否重定向游戏行为情景？"));
				if (bLetOpen)
				{
					bLetOpen = RetargetGameActionScene(GameActionBlueprint);
				}
			}
			if (bLetOpen)
			{
				FGameActionEditor::OpenEditor(Mode, EditWithinLevelEditor, GameActionBlueprint);
			}
		}
		else
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("FailedToLoadBlueprint", "Blueprint could not be loaded because it derives from an invalid class.  Check to make sure the parent class for this blueprint hasn't been removed!"));
		}
	}
}

UFactory* FGameActionBlueprint_AssetTypeActions::GetFactoryForBlueprintType(UBlueprint* InBlueprint) const
{
	UGameActionBlueprint* GameActionBlueprint = CastChecked<UGameActionBlueprint>(InBlueprint);
	UGameActionBlueprintFactory* GameActionBlueprintFactory = NewObject<UGameActionBlueprintFactory>();
	GameActionBlueprintFactory->ParentClass = *GameActionBlueprint->GeneratedClass;
	GameActionBlueprintFactory->GameActionScene = GameActionBlueprint->GameActionScene;
	return GameActionBlueprintFactory;
}

bool FGameActionBlueprint_AssetTypeActions::RetargetGameActionScene(UGameActionBlueprint* Blueprint)
{
	class SGameActionSceneRetargetDialog : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGameActionSceneRetargetDialog) {}

		SLATE_END_ARGS()

		/** Constructs this widget with InArgs */
		void Construct(const FArguments& InArgs, UGameActionBlueprint* InBlueprint)
		{
			check(InBlueprint);
			Blueprint = InBlueprint;

			bOkClicked = false;

			FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
			FAssetPickerConfig AssetPickerConfig = GameActionAssetUtils::GetGameActionScenePickerConfig(*Blueprint->ParentClass);
			AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([=](const FAssetData& AssetData)
			{
				SelectedGameActionScene = AssetData;
			});

			ChildSlot
			[
				SNew(SBorder)
				.Visibility(EVisibility::Visible)
				.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
				[
					SNew(SBox)
					.Visibility(EVisibility::Visible)
					.WidthOverride(500.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.FillHeight(1)
						[
							SNew(SBorder)
							.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
							.Content()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(STextBlock)
									.Text(LOCTEXT("选择游戏行为情景", "选择游戏行为情景:"))
									.ShadowOffset(FVector2D(1.0f, 1.0f))
								]
								+ SVerticalBox::Slot()
								[
									ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
								]
							]
						]

						// Ok/Cancel buttons
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Bottom)
						.Padding(8)
						[
							SNew(SUniformGridPanel)
							.SlotPadding(FEditorStyle::GetMargin("StandardDialog.SlotPadding"))
							.MinDesiredSlotWidth(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
							.MinDesiredSlotHeight(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
							+ SUniformGridPanel::Slot(0,0)
							[
								SNew(SButton)
								.HAlign(HAlign_Center)
								.ContentPadding( FEditorStyle::GetMargin("StandardDialog.ContentPadding") )
								.OnClicked_Lambda([this]
								{
									Blueprint->GameActionScene = Cast<UGameActionScene>(SelectedGameActionScene.GetAsset());
									if (Blueprint->GameActionScene != nullptr)
									{
										bOkClicked = true;
										PickerWindow.Pin()->RequestDestroyWindow();
									}
									else
									{
										FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("必须选择一个游戏行为情景", "必须选择一个游戏行为情景"));
									}
									return FReply::Handled();
								})
								.Text(LOCTEXT("RetargetGameActionSceneOk", "OK"))
							]
							+ SUniformGridPanel::Slot(1,0)
							[
								SNew(SButton)
								.HAlign(HAlign_Center)
								.ContentPadding( FEditorStyle::GetMargin("StandardDialog.ContentPadding") )
								.OnClicked_Lambda([this]
								{
									bOkClicked = false;
									PickerWindow.Pin()->RequestDestroyWindow();
									return FReply::Handled();
								})
								.Text(LOCTEXT("RetargetGameActionSceneCancel", "Cancel"))
							]
						]
					]
				]
			];
		}

		bool ConfigureProperties()
		{
			TSharedRef<SWindow> Window = SNew(SWindow)
				.Title(LOCTEXT("重定向行为情景", "重定向行为情景"))
				.ClientSize(FVector2D(400, 700))
				.SupportsMinimize(false).SupportsMaximize(false)
				[
					AsShared()
				];

			PickerWindow = Window;
			GEditor->EditorAddModalWindow(Window);
			return bOkClicked;
		}

		TWeakPtr<SWindow> PickerWindow;
		UGameActionBlueprint* Blueprint = nullptr;
		FAssetData SelectedGameActionScene;
		uint8 bOkClicked : 1;
	};
	
	TSharedRef<SGameActionSceneRetargetDialog> RetargetDialog = SNew(SGameActionSceneRetargetDialog, Blueprint);
	return RetargetDialog->ConfigureProperties();
}

void UGameActionBlueprintThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	if (UGameActionBlueprint* Blueprint = Cast<UGameActionBlueprint>(Object))
	{
		UGameActionScene* GameActionScene = Blueprint->GameActionScene;
		if (ThumbnailScene == nullptr)
		{
			ThumbnailScene = new FAdvancedPreviewScene(FPreviewScene::ConstructionValues().SetEditor(false));
			ThumbnailScene->GetWorld()->bBegunPlay = true;
		}
		
		if (GameActionScene == nullptr || GameActionScene->OwnerTemplate == nullptr)
		{
			return;
		}
		UClass* GameActionInstanceClass = Blueprint->GeneratedClass;
		
		TArray<AActor*> SpawnedPreviewActors;
		{
			ACharacter* Owner = nullptr;
			UWorld* World = ThumbnailScene->GetWorld();
			{
				ACharacter* OwnerTemplate = GameActionScene->OwnerTemplate;
				FActorSpawnParameters ActorSpawnParameters;
				ActorSpawnParameters.Template = OwnerTemplate;
				Owner = World->SpawnActor<ACharacter>(OwnerTemplate->GetClass(), ActorSpawnParameters);
				SpawnedPreviewActors.Add(Owner);
			}

			UGameActionComponent* GameActionComponent = Owner->FindComponentByClass<UGameActionComponent>();
			if (GameActionComponent == nullptr)
			{
				GameActionComponent = NewObject<UGameActionComponent>(Owner, NAME_None, RF_Transient);
				GameActionComponent->RegisterComponent();
				Owner->AddOwnedComponent(GameActionComponent);
			}
			const TSubclassOf<UGameActionInstanceBase> ActionType{ GameActionInstanceClass };
			UGameActionInstanceBase* PreviewInstance = GameActionComponent->FindGameAction(ActionType);
			if (PreviewInstance == nullptr)
			{
				PreviewInstance = GameActionComponent->AddGameAction(ActionType);
			}
			PreviewInstance->bIsSimulation = true;
			
			for (const FGameActionSceneActorData& TemplateData : GameActionScene->TemplateDatas)
			{
				if (AActor* TemplateActor = TemplateData.Template)
				{
					FActorSpawnParameters ActorSpawnParameters;
					ActorSpawnParameters.Template = TemplateActor;
					AActor* PreviewActor = World->SpawnActor<AActor>(TemplateActor->GetClass(), TemplateData.GetSpawnTransform(), ActorSpawnParameters);
					SpawnedPreviewActors.Add(PreviewActor);

					const FName PossessableName = TemplateActor->GetFName();
					if (FObjectProperty* ObjectProperty = FindFProperty<FObjectProperty>(GameActionInstanceClass, PossessableName))
					{
						if (UObject** P_Possessable = ObjectProperty->ContainerPtrToValuePtr<UObject*>(PreviewInstance))
						{
							*P_Possessable = PreviewActor;
						}
					}
				}
			}

			if (PreviewInstance->DefaultEntry.Transitions.Num() > 0)
			{
				if (UGameActionSegmentBase* SegmentBase = PreviewInstance->DefaultEntry.Transitions[0].TransitionToSegment)
				{
					SegmentBase->ActiveAction();
					for (AActor* Actor : SpawnedPreviewActors)
					{
						if (Actor)
						{
							Actor->ForEachComponent<UPrimitiveComponent>(true, [&](UPrimitiveComponent* PrimitiveComponent)
							{
								PrimitiveComponent->SetLastRenderTime(World->TimeSeconds);
							});
						}
					}
					if (UGameActionSegment* Segment = Cast<UGameActionSegment>(SegmentBase))
					{
						const float TotalTime = Segment->GetSequenceTotalTime();
						const float PreviewTime = TotalTime > 1.f ? TotalTime / 2.f : TotalTime / 1.2f;
						PreviewInstance->SequencePlayer->JumpToSeconds(PreviewTime);
						World->Tick(ELevelTick::LEVELTICK_All, PreviewTime);
					}
					else
					{
						World->Tick(ELevelTick::LEVELTICK_All, 1.f);
					}
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

void UGameActionBlueprintThumbnailRenderer::BeginDestroy()
{
	if (ThumbnailScene != nullptr)
	{
		delete ThumbnailScene;
		ThumbnailScene = nullptr;
	}

	Super::BeginDestroy();
}

#undef LOCTEXT_NAMESPACE
