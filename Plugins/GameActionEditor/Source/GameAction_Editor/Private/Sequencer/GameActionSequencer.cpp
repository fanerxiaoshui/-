// Fill out your copyright notice in the Description page of Project Settings.


#include "Sequencer/GameActionSequencer.h"
#include <ISequencerModule.h>
#include <ISequencer.h>
#include <ISequencerEditorObjectBinding.h>
#include <MovieScene.h>
#include <Widgets/Layout/SBox.h>
#include <Widgets/Text/STextBlock.h>
#include <Widgets/Layout/SWidgetSwitcher.h>
#include <Widgets/Layout/SConstraintCanvas.h>
#include <Styling/SlateIconFinder.h>
#include <ScopedTransaction.h>
#include <KeyPropertyParams.h>
#include <Toolkits/AssetEditorToolkit.h>
#include <Framework/MultiBox/MultiBoxExtender.h>
#include <MovieSceneSequenceEditor.h>
#include <Kismet2/KismetEditorUtilities.h>
#include <SequencerSettings.h>
#include <ClassViewerModule.h>
#include <ClassViewerFilter.h>
#include <Misc/ScopeExit.h>
#include <Kismet2/BlueprintEditorUtils.h>
#include <CineCameraActor.h>
#include <CineCameraComponent.h>
#include <MovieSceneToolHelpers.h>
#include <Animation/AnimInstance.h>
#include <Tracks/MovieSceneTransformTrack.h>
#include <Tracks/MovieScene3DTransformTrack.h>
#include <Sections/MovieScene3DTransformSection.h>
#include <Sections/MovieSceneVectorSection.h>
#include <AssetSelection.h>
#include <ActorFactories/ActorFactory.h>
#include <ContentBrowserModule.h>
#include <IContentBrowserSingleton.h>
#include <GameFramework/Character.h>
#include <Widgets/Input/SCheckBox.h>
#include <Framework/MultiBox/MultiBoxBuilder.h>
#include <EditorViewportClient.h>
#include <Framework/Application/SlateApplication.h>
#include <PropertyHandle.h>

#include "Editor/GameActionPreviewScene.h"
#include "Sequence/GameActionSequence.h"
#include "Sequence/GameActionSequencePlayer.h"
#include "Blueprint/GameActionBlueprint.h"
#include "Blueprint/BPNode_GameActionSegment.h"
#include "Blueprint/EdGraph_GameAction.h"
#include "GameAction/GameActionEvent.h"
#include "GameAction/GameActionSegment.h"
#include "GameAction/GameActionInstance.h"
#include "Sequence/GameActionDynamicSpawnTrack.h"
#include "Sequence/GameActionEventTrack.h"
#include "Sequence/GameActionSequenceCustomSpawner.h"

#define LOCTEXT_NAMESPACE "Game Action Sequencer"

FGameActionSequencer::FGameActionSequencer(const TSharedRef<FGameActionEditScene>& InGameActionPreviewScene, const TSharedPtr<IToolkitHost>& InToolkitHost)
	: ToolkitHost(InToolkitHost), EditScene(InGameActionPreviewScene), WidgetSectionVisual(*NewObject<UGameActionSequencerWidgetSectionVisual>())
{
	UGameActionBlueprint* GameActionBlueprint = InGameActionPreviewScene->GameActionBlueprint;

	SAssignNew(ActionSequencerWidget, SWidgetSwitcher)
	+ SWidgetSwitcher::Slot()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("选择行为节点进行动画编辑", "选择行为节点进行动画编辑"))
		.Justification(ETextJustify::Center)
	]
	+ SWidgetSwitcher::Slot()
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SAssignNew(SequencerContent, SBox)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNullWidget::NullWidget
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SConstraintCanvas)
			.Visibility(GameActionBlueprint->IsRootBlueprint() ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible)
			+ SConstraintCanvas::Slot()
			.Anchors(FAnchors(1.f, 0.f, 1.f, 0.f))
			.Alignment(FVector2D(1.f, 0.f))
			.Offset(FMargin(-30.f, 6.f, 0.f, 0.f))
			.AutoSize(true)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]
					{
						if (UBPNode_GameActionSegment* PreviewSegmentNode = PreviewActionSegmentNode.Get())
						{
							const TArray<FGameActionSequenceOverride>& SequenceOverrides = EditScene->GameActionBlueprint->SequenceOverrides;
							if (const FGameActionSequenceOverride* SequenceOverride = SequenceOverrides.FindByPredicate([&](const FGameActionSequenceOverride& E) { return E.NodeOverride.Get() == PreviewSegmentNode; } ))
							{
								if (SequenceOverride->bEnableOverride)
								{
									return ECheckBoxState::Checked;
								}
							}
						}
						return ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](ECheckBoxState CheckBoxState)
					{
						if (UBPNode_GameActionSegment* PreviewSegmentNode = PreviewActionSegmentNode.Get())
						{
							FScopedTransaction ScopedTransaction(LOCTEXT("覆盖父类Sequence", "覆盖父类Sequence"));

							UGameActionBlueprint* GameActionBlueprint = EditScene->GameActionBlueprint;
							check(GameActionBlueprint->IsRootBlueprint() == false);
							GameActionBlueprint->Modify();
							GameActionBlueprint->Status = EBlueprintStatus::BS_Dirty;
							TArray<FGameActionSequenceOverride>& SequenceOverrides = GameActionBlueprint->SequenceOverrides;
							if (FGameActionSequenceOverride* SequenceOverride = SequenceOverrides.FindByPredicate([&](const FGameActionSequenceOverride& E) { return E.NodeOverride.Get() == PreviewSegmentNode; }))
							{
								SequenceOverride->bEnableOverride = !SequenceOverride->bEnableOverride;
							}
							else
							{
								FGameActionSequenceOverride& Override = SequenceOverrides.AddDefaulted_GetRef();
								Override.NodeOverride = PreviewSegmentNode;
								Override.bEnableOverride = true;
								Override.SequenceOverride = DuplicateObject(PreviewSegmentNode->GetGameActionSequence(), GameActionBlueprint, NAME_None);
							}
							CloseGameActionSequencer();
							OpenGameActionSequencer(PreviewSegmentNode);
						}
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("覆盖父类Sequence", "覆盖父类Sequence"))
				]
			]
		]
	];
	
	ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
	ActorBindingDelegateHandle = SequencerModule.RegisterEditorObjectBinding(FOnCreateEditorObjectBinding::CreateLambda([=](TSharedRef<ISequencer> InSequencer)
	{
		class FGameActionSequencerActorBinding : public ISequencerEditorObjectBinding
		{
		public:
			FGameActionSequencerActorBinding(const TSharedRef<ISequencer>& InSequencer, const TSharedRef<FGameActionPreviewScene>& InGameActionPreviewScene)
				: Sequencer(InSequencer), GameActionPreviewScene(InGameActionPreviewScene)
			{}

			void BuildSequencerAddMenu(FMenuBuilder& MenuBuilder) override
			{
				MenuBuilder.AddSubMenu(
					LOCTEXT("AddReference_Label", "添加引用"),
					LOCTEXT("AddReference_ToolTip", "在当前行为序列中添加引用"),
					FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder)
					{
						SubMenuBuilder.BeginSection(TEXT("Possessable"), LOCTEXT("Possessable", "Possessable"));
						{
							bool bOwnerExist = true;
							{
								UGameActionBlueprint* Blueprint = GameActionPreviewScene->GameActionBlueprint;
								TSubclassOf<ACharacter> OwnerType = Blueprint->GetOwnerType();
								const FSlateIcon ActorIcon = FSlateIconFinder::FindIconForClass(OwnerType);
								UGameActionSequence* Sequence = Cast<UGameActionSequence>(Sequencer.Pin()->GetFocusedMovieSceneSequence());
								UMovieScene* MovieScene = Sequence->GetMovieScene();
								bOwnerExist = MovieScene->FindPossessable(Sequence->OwnerGuid) != nullptr;
								if (bOwnerExist == false)
								{
									SubMenuBuilder.AddMenuEntry(LOCTEXT("Owner", "Owner"), FText(), ActorIcon, FExecuteAction::CreateLambda([=] {
										if (Sequencer.IsValid())
										{
											if (UGameActionSequence* GameActionSequence = Cast<UGameActionSequence>(Sequencer.Pin()->GetRootMovieSceneSequence()))
											{
												const FScopedTransaction Transaction(LOCTEXT("添加Owner引用至序列", "添加Owner引用至序列"));
												GameActionSequence->Modify();
												GameActionSequence->SetOwnerCharacter(OwnerType);
												Sequencer.Pin()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
											}
										}
										FSlateApplication::Get().DismissAllMenus();
									}));
								}
							}

							TSet<FName> ExistingPossessedObjects;
							if (Sequencer.IsValid())
							{
								UMovieSceneSequence* MovieSceneSequence = Sequencer.Pin()->GetFocusedMovieSceneSequence();
								UMovieScene* MovieScene = MovieSceneSequence->GetMovieScene();
								if (MovieScene)
								{
									for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); Index++)
									{
										FMovieScenePossessable& Possessable = MovieScene->GetPossessable(Index);
										ExistingPossessedObjects.Add(*Possessable.GetName());
									}
								}
							}

							TArray<FObjectProperty*> CanCreatePossessable;
							if (const UGameActionBlueprint* GameActionBlueprint = GameActionPreviewScene->GameActionBlueprint)
							{
								if (UClass* SkeletonGeneratedClass = GameActionBlueprint->SkeletonGeneratedClass)
								{
									for (TFieldIterator<FObjectProperty> It(SkeletonGeneratedClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
									{
										FObjectProperty* ObjectProperty = *It;
										if (ObjectProperty->PropertyClass && ObjectProperty->PropertyClass->IsChildOf<AActor>() && ObjectProperty->GetBoolMetaData(UGameActionBlueprint::MD_GameActionPossessableReference))
										{
											if (ExistingPossessedObjects.Contains(ObjectProperty->GetFName()) == false)
											{
												CanCreatePossessable.Add(ObjectProperty);
											}
										}
									}
								}
							}

							for (FObjectProperty* ObjectProperty : CanCreatePossessable)
							{
								const FName PropertyName = ObjectProperty->GetFName();
								const FSlateIcon ActorIcon = FSlateIconFinder::FindIconForClass(ObjectProperty->PropertyClass);
								SubMenuBuilder.AddMenuEntry(FText::FromName(PropertyName), FText(), ActorIcon, FExecuteAction::CreateLambda([=] {
									if (Sequencer.IsValid())
									{
										if (UGameActionSequence* GameActionSequence = Cast<UGameActionSequence>(Sequencer.Pin()->GetRootMovieSceneSequence()))
										{
											const FScopedTransaction Transaction(LOCTEXT("添加Possessable引用至序列", "添加Possessable引用至序列"));
											GameActionSequence->Modify();
											const FGuid BindGuid = GameActionSequence->AddPossessableActor(PropertyName, ObjectProperty->PropertyClass);
											Sequencer.Pin()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
										}
									}
									FSlateApplication::Get().DismissAllMenus();
								}));
							}

							if (CanCreatePossessable.Num() == 0 && bOwnerExist)
							{
								SubMenuBuilder.AddWidget(SNew(STextBlock).Text(LOCTEXT("没有可供添加的Possessable引用", "没有可供添加的Possessable引用")), FText::GetEmpty());
							}
						}
						SubMenuBuilder.EndSection();

						SubMenuBuilder.BeginSection(TEXT("Spawnable"), LOCTEXT("Spawnable", "Spawnable"));
						{
							TSet<UObject*> ExistingSpawnableObjects;
							if (Sequencer.IsValid())
							{
								UMovieSceneSequence* MovieSceneSequence = Sequencer.Pin()->GetFocusedMovieSceneSequence();
								UMovieScene* MovieScene = MovieSceneSequence->GetMovieScene();
								if (MovieScene)
								{
									for (int32 Index = 0; Index < MovieScene->GetSpawnableCount(); Index++)
									{
										FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(Index);
										ExistingSpawnableObjects.Add(Spawnable.GetObjectTemplate());
									}
								}
							}

							struct FTemplateAndTrack
							{
								AActor* Template;
								UGameActionDynamicSpawnTrack* SpawnTrack;
							};
							TArray<FTemplateAndTrack> CanCreateSpawnable;
							if (const UGameActionBlueprint* GameActionBlueprint = GameActionPreviewScene->GameActionBlueprint)
							{
								UEdGraph_GameAction* GameActionGraph = GameActionBlueprint->GetGameActionGraph();
								for (UEdGraphNode* Node : GameActionGraph->Nodes)
								{
									if (UBPNode_GameActionSegment* SegmentNode = Cast<UBPNode_GameActionSegment>(Node))
									{
										UGameActionSegment* GameActionSegment = Cast<UGameActionSegment>(SegmentNode->GameActionSegment);
										if (ensure(GameActionSegment))
										{
											UMovieScene* MovieScene = GameActionSegment->GameActionSequence->GetMovieScene();
											for (int32 Idx = 0; Idx < MovieScene->GetSpawnableCount(); ++Idx)
											{
												FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(Idx);
												UGameActionDynamicSpawnTrack* SpawnTrack = MovieScene->FindTrack<UGameActionDynamicSpawnTrack>(Spawnable.GetGuid());
												if (SpawnTrack == nullptr || SpawnTrack->GetAllSections().Num() == 0)
												{
													continue;
												}
												const UGameActionDynamicSpawnSectionBase* SpawnSection = SpawnTrack->SpawnSection[0];
												if (SpawnSection->AsReference() == false)
												{
													continue;
												}

												if (ensure(Spawnable.GetObjectTemplate()) == false)
												{
													continue;
												}
												if (ExistingSpawnableObjects.Contains(Spawnable.GetObjectTemplate()) == false 
														&& CanCreateSpawnable.ContainsByPredicate([&](const FTemplateAndTrack& E){ return E.Template == Spawnable.GetObjectTemplate(); }) == false)
												{
													FTemplateAndTrack& Data = CanCreateSpawnable.AddDefaulted_GetRef();
													Data.Template = CastChecked<AActor>(Spawnable.GetObjectTemplate());
													Data.SpawnTrack = MovieScene->FindTrack<UGameActionDynamicSpawnTrack>(Spawnable.GetGuid());
												}
											}
										}
									}
								}
							}
							for (const FTemplateAndTrack& TemplateAndTrack : CanCreateSpawnable)
							{
								AActor* Spawnable = TemplateAndTrack.Template;
								UMovieSceneSection* OriginSection = TemplateAndTrack.SpawnTrack->GetAllSections()[0];
								const FName SpawnableName = Spawnable->GetFName();
								const FSlateIcon ActorIcon = FSlateIconFinder::FindIconForClass(Spawnable->GetClass());
								SubMenuBuilder.AddMenuEntry(FText::FromName(SpawnableName), FText(), ActorIcon, FExecuteAction::CreateLambda([=] {
									if (Sequencer.IsValid())
									{
										UGameActionBlueprint* Blueprint = GameActionPreviewScene->GameActionBlueprint;
										if (UGameActionSequence* GameActionSequence = Cast<UGameActionSequence>(Sequencer.Pin()->GetRootMovieSceneSequence()))
										{
											const FScopedTransaction Transaction(LOCTEXT("添加Spawnable引用至序列", "添加Spawnable引用至序列"));
											GameActionSequence->Modify();
											UMovieScene* MovieScene = GameActionSequence->GetMovieScene();
											const FGuid BindGuid = GameActionSequence->AddSpawnable(SpawnableName.ToString(), *Spawnable);
											UGameActionDynamicSpawnTrack* SpawnTrack = MovieScene->AddTrack<UGameActionDynamicSpawnTrack>(BindGuid);
											if (UGameActionSpawnByTemplateSection* SpawnByType = Cast<UGameActionSpawnByTemplateSection>(OriginSection))
											{
												UGameActionSpawnByTemplateSection* SpawnSection = NewObject<UGameActionSpawnByTemplateSection>(SpawnTrack, NAME_None, RF_Transactional);
												SpawnSection->SpawnerSettings = SpawnByType->SpawnerSettings;
												SpawnTrack->AddSection(*SpawnSection);
											}
											else if (UGameActionSpawnBySpawnerSection* SpawnBySpawner = Cast<UGameActionSpawnBySpawnerSection>(OriginSection))
											{
												UGameActionSpawnBySpawnerSection* SpawnSection = NewObject<UGameActionSpawnBySpawnerSection>(SpawnTrack, NAME_None, RF_Transactional);
												SpawnSection->CustomSpawner = SpawnBySpawner->CustomSpawner;
												SpawnTrack->AddSection(*SpawnSection);
											}
											else
											{
												ensure(false);
											}

											Sequencer.Pin()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
											Sequencer.Pin()->OnActorAddedToSequencer().Broadcast(Spawnable, BindGuid);
										}
									}
									FSlateApplication::Get().DismissAllMenus();
								}));
							}

							if (CanCreateSpawnable.Num() == 0)
							{
								SubMenuBuilder.AddWidget(SNew(STextBlock).Text(LOCTEXT("没有可供添加的Spawnable引用", "没有可供添加的Spawnable引用")), FText::GetEmpty());
							}
							SubMenuBuilder.EndSection();
						}
					}),
					false,
					FSlateIcon("LevelSequenceEditorStyle", "LevelSequenceEditor.PossessNewActor")
				);

				MenuBuilder.AddSubMenu(
					LOCTEXT("添加生成实体导轨", "生成实体"),
					LOCTEXT("添加生成实体导轨提示", "添加生成实体导轨"),
					FNewMenuDelegate::CreateLambda([=](FMenuBuilder& MenuBuilder)
					{
						MenuBuilder.AddSubMenu(
						LOCTEXT("选择生成器", "选择生成器"),
						LOCTEXT("选择生成器提示", "选择生成器"),
						FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder)
						{
							FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
							FClassViewerInitializationOptions Options;
							Options.Mode = EClassViewerMode::ClassPicker;
							Options.DisplayMode = EClassViewerDisplayMode::ListView;
							Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::Dynamic;
							Options.bIsBlueprintBaseOnly = false;
							class FSpawnByTypeClassFilter : public IClassViewerFilter
							{
							public:
								bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
								{
									return InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated) == false && InClass->IsChildOf<UGameActionSequenceCustomSpawnerBase>();
								}
								bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
								{
									return InUnloadedClassData->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated) == false && InUnloadedClassData->IsChildOf(UGameActionSequenceCustomSpawnerBase::StaticClass());
								}
							};
							Options.ClassFilter = MakeShareable(new FSpawnByTypeClassFilter());

							TSharedRef<SWidget> ClassPickerWidget = ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateLambda([=](UClass* CustomSpawnerClass)
							{
								ON_SCOPE_EXIT
								{
									FSlateApplication::Get().DismissAllMenus();
								};

								UMovieSceneSequence* MovieSceneSequence = Sequencer.Pin()->GetFocusedMovieSceneSequence();
								UMovieScene* MovieScene = MovieSceneSequence->GetMovieScene();
								MovieSceneSequence->Modify();
								UGameActionBlueprint* Blueprint = GameActionPreviewScene->GameActionBlueprint;
								UObject* Outer = Blueprint;
								AActor* PreviewInstance = CustomSpawnerClass->GetDefaultObject<UGameActionSequenceCustomSpawnerBase>()->GetPreviewInstance(Outer);
								if (ensure(PreviewInstance) == false)
								{
									return;
								}
							
 								const FScopedTransaction Transaction(LOCTEXT("从生成器创建生成实体导轨", "从生成器创建生成实体导轨"));
 								const FName ObjectName = MakeUniqueObjectName(GameActionPreviewScene->GetWorld()->PersistentLevel, CustomSpawnerClass, *CustomSpawnerClass->GetDisplayNameText().ToString());
								UGameActionSequenceCustomSpawnerBase* Spawner = NewObject<UGameActionSequenceCustomSpawnerBase>(Outer, CustomSpawnerClass, ObjectName, RF_Transactional);
 								const FGuid ObjectGuid = MovieScene->AddSpawnable(ObjectName.ToString(), *PreviewInstance);
 								UGameActionDynamicSpawnTrack* SpawnTrack = MovieScene->AddTrack<UGameActionDynamicSpawnTrack>(ObjectGuid);
								UGameActionSpawnBySpawnerSection* SpawnSection = NewObject<UGameActionSpawnBySpawnerSection>(SpawnTrack);
								SpawnSection->CustomSpawner = Spawner;
 								SpawnTrack->AddSection(*SpawnSection);

 								Sequencer.Pin()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
								Sequencer.Pin()->OnActorAddedToSequencer().Broadcast(PreviewInstance, ObjectGuid);

								if (SpawnSection->AsReference())
								{
									FKismetEditorUtilities::CompileBlueprint(Blueprint);
								}
							}));

							SubMenuBuilder.AddWidget(SNew(SBox)
														.WidthOverride(400.f)
														[
															ClassPickerWidget
														], FText::GetEmpty(), true);
						}),
						false,
						FSlateIcon());

						MenuBuilder.AddSubMenu(
						LOCTEXT("选择资源生成", "选择资源生成"),
						LOCTEXT("选择资源生成提示", "选择资源生成"),
						FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder)
						{
							FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
							FAssetPickerConfig AssetPickerConfig;
							AssetPickerConfig.SelectionMode = ESelectionMode::Single;
							AssetPickerConfig.InitialAssetViewType = EAssetViewType::Tile;
							AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateLambda([](const FAssetData& AssetData)
							{
								if (FActorFactoryAssetProxy::GetFactoryForAsset(AssetData))
								{
									return false;
								}
								return true;
							});
							AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([=](const FAssetData& AssetData)
							{
								ON_SCOPE_EXIT
								{
									FSlateApplication::Get().DismissAllMenus();
								};

								const FScopedTransaction Transaction(LOCTEXT("从资源创建生成实体导轨", "从资源创建生成实体导轨"));

								UObject* Asset = AssetData.GetAsset();
								Sequencer.Pin()->MakeNewSpawnable(*Asset, FActorFactoryAssetProxy::GetFactoryForAssetObject(Asset));
								Sequencer.Pin()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
							});
							SubMenuBuilder.AddWidget(SNew(SBox)
														.WidthOverride(400.f)
														[
															ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
														], FText::GetEmpty(), true);
						}),
						false,
						FSlateIcon());
					}),
					false,
					FSlateIcon("LevelSequenceEditorStyle", "LevelSequenceEditor.PossessNewActor")
				);
			}
			bool SupportsSequence(UMovieSceneSequence* InSequence) const override
			{
				if (UGameActionSequence* GameActionSequence = Cast<UGameActionSequence>(InSequence))
				{
					return GameActionSequence->BelongToEditScene == GameActionPreviewScene;
				}
				return false;
			}

			TWeakPtr<ISequencer> Sequencer;
			TSharedRef<FGameActionPreviewScene> GameActionPreviewScene;
		};
		return MakeShareable(new FGameActionSequencerActorBinding(InSequencer, EditScene));
	}));

	const int32 NewIndex = SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates().Add(FAssetEditorExtender::CreateLambda([=](const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> ContextSensitiveObjects)
	{
		TSharedRef<FExtender> AddTrackMenuExtender(new FExtender());
		if (ContextSensitiveObjects.Num() != 1 || ContextSensitiveObjects[0] == nullptr || ContextSensitiveObjects[0]->GetWorld() != EditScene->GetWorld())
		{
			return AddTrackMenuExtender;
		}
		AddTrackMenuExtender->AddMenuExtension(
			SequencerMenuExtensionPoints::AddTrackMenu_PropertiesSection,
			EExtensionHook::Before,
			CommandList,
			FMenuExtensionDelegate::CreateLambda([=](FMenuBuilder& AddTrackMenuBuilder)
			{
				if (ContextSensitiveObjects.Num() != 1)
				{
					return;
				}

				if (AActor* Actor = Cast<AActor>(ContextSensitiveObjects[0]))
				{
					AddTrackMenuBuilder.BeginSection("Components", LOCTEXT("ComponentsSection", "Components"));
					{
						for (UActorComponent* Component : Actor->GetComponents())
						{
							if (Component)
							{
								const FText ComponentName = FText::FromString(Component->GetName());
								AddTrackMenuBuilder.AddMenuEntry(
									ComponentName,
									FText::Format(LOCTEXT("ComponentToolTipFormat", "Add {0} component"), ComponentName),
									FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([=]()
									{
										const FScopedTransaction Transaction(LOCTEXT("AddComponent", "Add Component"));
										PreviewSequencer->GetHandleToObject(Component);
									})));
							}
						}
					}
					AddTrackMenuBuilder.EndSection();
				}
				else if (USkeletalMeshComponent* SkeletalComponent = Cast<USkeletalMeshComponent>(ContextSensitiveObjects[0]))
				{
					if (UAnimInstance* AnimInstance = SkeletalComponent->GetAnimInstance())
					{
						FText AnimInstanceLabel = LOCTEXT("AnimInstanceLabel", "Anim Instance");
						FText DetailedInstanceText = FText::Format(LOCTEXT("AnimInstanceLabelFormat", "Anim Instance '{0}'"), FText::FromString(AnimInstance->GetName()));

						AddTrackMenuBuilder.BeginSection("Anim Instance", AnimInstanceLabel);
						{
							AddTrackMenuBuilder.AddMenuEntry(
								DetailedInstanceText,
								LOCTEXT("AnimInstanceToolTip", "Add this skeletal mesh component's animation instance."),
								FSlateIcon(),
								FUIAction(FExecuteAction::CreateLambda([AnimInstance, this]
								{
									const FScopedTransaction Transaction(LOCTEXT("AddAnimInstance", "Add Anim Instance"));
									PreviewSequencer->GetHandleToObject(AnimInstance);
								}))
							);
						}
						AddTrackMenuBuilder.EndSection();
					}
				}
			}));
		return AddTrackMenuExtender;
	}));
	SequencerExtenderHandle = SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates()[NewIndex].GetHandle();

	struct FMovieSceneSequenceEditor_GameActionSequence : FMovieSceneSequenceEditor
	{
		bool CanCreateEvents(UMovieSceneSequence* InSequence) const override
		{
			return true;
		}

		UBlueprint* GetBlueprintForSequence(UMovieSceneSequence* InSequence) const override
		{
			UGameActionSequence* GameActionSequence = CastChecked<UGameActionSequence>(InSequence);
			if (UBlueprint* Blueprint = GameActionSequence->GetTypedOuter<UBlueprint>())
			{
				return Blueprint;
			}
			return nullptr;
		}

		UBlueprint* CreateBlueprintForSequence(UMovieSceneSequence* InSequence) const override
		{
			return nullptr;
		}
	};
	SequenceEditorHandle = SequencerModule.RegisterSequenceEditor(UGameActionSequence::StaticClass(), MakeUnique<FMovieSceneSequenceEditor_GameActionSequence>());

	GameActionToolbarExtender = MakeShareable(new FExtender());
	GameActionToolbarExtender->AddToolBarExtension(
		TEXT("Base Commands"),
		EExtensionHook::After,
		nullptr,
		FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& ToolBarBuilder)
		{
			ToolBarBuilder.AddToolBarButton(FUIAction(
				FExecuteAction::CreateLambda([this]
				{
					CreateGameActionCamera();
				})
			), NAME_None, LOCTEXT("创建相机", "创建相机"), LOCTEXT("创建相机", "创建相机"), FSlateIcon(TEXT("EditorStyle"), TEXT("Sequencer.CreateCamera")));
		})
	);
	
	WidgetSectionVisual.AddToRoot();

	// 处理Owner蓝图类编译后失效问题
	OnObjectsReplacedHandle = GEditor->OnObjectsReplaced().AddLambda([this](const UEditorEngine::ReplacementObjectMap& ObjectMap)
	{
		for (const TPair<UObject*, UObject*>& Pair : ObjectMap)
		{
			if (ACharacter* OldOwner = Cast<ACharacter>(Pair.Key))
			{
				if (EditScene->PreviewOwner == OldOwner)
				{
					EditScene->PreviewOwner = CastChecked<ACharacter>(Pair.Value);
					EditScene->CreatePreviewInstance();
					if (UBPNode_GameActionSegment* SegmentNode = PreviewActionSegmentNode.Get())
					{
						CloseGameActionSequencer();
						OpenGameActionSequencer(SegmentNode);
					}
					break;
				}
			}
		}
	});
}

FGameActionSequencer::~FGameActionSequencer()
{
	if (PreviewActionSegmentNode.IsValid())
	{
		CloseGameActionSequencer();
	}
	if (ISequencerModule* SequencerModule = FModuleManager::LoadModulePtr<ISequencerModule>("Sequencer"))
	{
		SequencerModule->UnRegisterEditorObjectBinding(ActorBindingDelegateHandle);
		SequencerModule->GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates().RemoveAll([=](const FAssetEditorExtender& Extender)
		{
			return SequencerExtenderHandle == Extender.GetHandle();
		});
		SequencerModule->UnRegisterTrackEditor(SequenceEditorHandle);
	}
	if (EditScene->GetDrawableObject() == &WidgetSectionVisual)
	{
		EditScene->ClearDrawableObject();
	}
	WidgetSectionVisual.RemoveFromRoot();
	GEditor->OnObjectsReplaced().Remove(OnObjectsReplacedHandle);
}

#include "Sequencer/Private/SequencerSelection.h"
namespace SequencerHack
{
	namespace dirty_hacks
	{
		template <class T>
		using remove_cvref_t = typename std::remove_cv_t<std::remove_reference_t<T>>;

		template <class T>
		struct type_identity { using type = T; };
		template <class T>
		using type_identity_t = typename type_identity<T>::type;

		namespace impl
		{

			template <class T>
			struct complete
			{
				template <class U>
				static auto test(U*) -> decltype(sizeof(U), std::true_type{});
				static auto test(...)->std::false_type;
				using type = decltype(test(static_cast<T*>(nullptr)));
			};

		} // namespace impl

		template <class T>
		struct is_complete : impl::complete<T>::type {};

	} // namespace dirty_hacks

	/** Yet another concat implementation. */
#define YA_CAT_IMPL(x, y) x##y
/** Yet another concat. */
#define YA_CAT(x, y) YA_CAT_IMPL(x, y)
/**
 * Init private class member hijacker.
 * @param class_ Class name.
 * @param member Private member to hijack.
 * @param __VA_ARGS__ Member type.
 * @remark All HIJACKERs should appear before any HIJACK.
 */
#define HIJACKER(class_, member, ...) \
    namespace dirty_hacks { namespace hijack { \
    template <class> struct tag_##member; \
    inline auto get(tag_##member<class_>) -> type_identity_t<__VA_ARGS__> class_::*; \
    template <> struct tag_##member<class_> { \
        tag_##member(tag_##member*) {} \
        template <class T, class = std::enable_if_t< \
            !is_complete<tag_##member<T>>::value && std::is_base_of<class_, T>::value>> \
        tag_##member(tag_##member<T>*) {} \
    }; \
    template <type_identity_t<__VA_ARGS__> class_::* Ptr> struct YA_CAT(hijack_##member##_, __LINE__) { \
        friend auto get(tag_##member<class_>) -> type_identity_t<__VA_ARGS__> class_::* { return Ptr; } \
    }; \
    template struct YA_CAT(hijack_##member##_, __LINE__)<&class_::member>; \
    }}
 /**
  * Hijack private class member.
  * @param ptr Pointer to class instance.
  * @param member Private member to hijack.
  * @remark All HIJACKs should appear after any HIJACKER.
  */
#define HIJACK(ptr, member) \
    ((ptr)->*dirty_hacks::hijack::get( \
        static_cast<std::add_pointer_t<dirty_hacks::hijack::tag_##member< \
            dirty_hacks::remove_cvref_t<decltype(*ptr)>>>>(nullptr)))

	HIJACKER(FSequencerSelection, SelectedKeys, TSet<FSequencerSelectedKey>);
	HIJACKER(FSequencerSelection, OnOutlinerNodeSelectionChanged, FSequencerSelection::FOnSelectionChanged);
	const TSet<FSequencerSelectedKey>& HackSelectedKeys(ISequencer* Sequencer)
	{
		const FSequencerSelection& SequencerSelection = Sequencer->GetSelection();
		return HIJACK(&SequencerSelection, SelectedKeys);
	}

	FSequencerSelection::FOnSelectionChanged& HackOnOutlinerNodeSelectionChanged(ISequencer* Sequencer)
	{
		FSequencerSelection& SequencerSelection = Sequencer->GetSelection();
		return HIJACK(&SequencerSelection, OnOutlinerNodeSelectionChanged);
	}
}

class FGameActionEditorSpawnRegister : public FGameActionSpawnRegister
{
	using Super = FGameActionSpawnRegister;
	
	TSharedRef<FGameActionEditScene> EditScene;
public:
	FGameActionEditorSpawnRegister(const TSharedRef<FGameActionEditScene>& EditScene)
		: EditScene(EditScene)
	{
		
	}
	
	bool CanSpawnObject(UClass* InClass) const override { return true; }
	UObject* SpawnObject(FMovieSceneSpawnable& Spawnable, FMovieSceneSequenceIDRef TemplateID, IMovieScenePlayer& Player) override
	{
		const bool RuntimeState = FGameActionPlayerContext::CurrentSpawnerSettings ? true : false;
		if (RuntimeState)
		{
			return Super::SpawnObject(Spawnable, TemplateID, Player);
		}
		return Spawnable.GetObjectTemplate();
	}
	void DestroySpawnedObject(UObject& Object) override
	{
		const bool RuntimeState = FGameActionPlayerContext::CurrentSpawnerSettings ? true : false;
		if (RuntimeState)
		{
			Super::DestroySpawnedObject(Object);
		}
		else
		{
			AActor* Actor = CastChecked<AActor>(&Object);
			SpawnOwnershipMap.Remove(Actor);
			Actor->Destroy();
		}
	}
	TValueOrError<FNewSpawnable, FText> CreateNewSpawnableType(UObject& SourceObject, UMovieScene& OwnerMovieScene, UActorFactory* ActorFactory) override
	{
		FNewSpawnable NewSpawnable(nullptr, FName::NameToDisplayString(SourceObject.GetName(), false));
		UObject* Outer = OwnerMovieScene.GetTypedOuter<UGameActionBlueprint>();
		check(Outer);

		const FName TemplateName = MakeUniqueObjectName(Outer, UObject::StaticClass(), SourceObject.GetFName());

		FText ErrorText;

		// Deal with creating a spawnable from an instance of an actor
		if (AActor* Actor = Cast<AActor>(&SourceObject))
		{
			// If the source actor is not transactional, temporarily add the flag to ensure that the duplicated object is created with the transactional flag.
			// This is necessary for the creation of the object to exist in the transaction buffer for multi-user workflows
			const bool bWasTransactional = Actor->HasAnyFlags(RF_Transactional);
			if (!bWasTransactional)
			{
				Actor->SetFlags(RF_Transactional);
			}

			AActor* SpawnedActor = Cast<AActor>(StaticDuplicateObject(Actor, Outer, TemplateName, RF_AllFlags));
			SpawnedActor->bIsEditorPreviewActor = false;
			NewSpawnable.ObjectTemplate = SpawnedActor;
			NewSpawnable.Name = Actor->GetActorLabel();

			if (!bWasTransactional)
			{
				Actor->ClearFlags(RF_Transactional);
			}
		}

		// If it's a blueprint, we need some special handling
		else if (UBlueprint* SourceBlueprint = Cast<UBlueprint>(&SourceObject))
		{
			if (!OwnerMovieScene.GetClass()->IsChildOf(SourceBlueprint->GeneratedClass->ClassWithin))
			{
				ErrorText = FText::Format(LOCTEXT("ClassWithin", "Unable to add spawnable for class of type '{0}' since it has a required outer class '{1}'."), FText::FromString(SourceObject.GetName()), FText::FromString(SourceBlueprint->GeneratedClass->ClassWithin->GetName()));
				return MakeError(ErrorText);
			}

			NewSpawnable.ObjectTemplate = NewObject<UObject>(Outer, SourceBlueprint->GeneratedClass, TemplateName, RF_Transactional);
		}

		else if (UBlueprintGeneratedClass* SourceBlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(&SourceObject))
		{
			if (UBlueprint* BlueprintGeneratedBy = Cast<UBlueprint>(SourceBlueprintGeneratedClass->ClassGeneratedBy))
			{
				if (!OwnerMovieScene.GetClass()->IsChildOf(BlueprintGeneratedBy->GeneratedClass->ClassWithin))
				{
					ErrorText = FText::Format(LOCTEXT("ClassWithin", "Unable to add spawnable for class of type '{0}' since it has a required outer class '{1}'."), FText::FromString(SourceObject.GetName()), FText::FromString(BlueprintGeneratedBy->GeneratedClass->ClassWithin->GetName()));
					return MakeError(ErrorText);
				}

				NewSpawnable.ObjectTemplate = NewObject<UObject>(Outer, BlueprintGeneratedBy->GeneratedClass, TemplateName, RF_Transactional);
			}
		}

		// At this point we have to assume it's an asset
		else
		{
			UActorFactory* FactoryToUse = ActorFactory ? ActorFactory : FActorFactoryAssetProxy::GetFactoryForAssetObject(&SourceObject);
			if (!FactoryToUse)
			{
				ErrorText = FText::Format(LOCTEXT("CouldNotFindFactory", "Unable to create spawnable from asset '{0}' - no valid factory could be found."), FText::FromString(SourceObject.GetName()));
			}

			if (FactoryToUse)
			{
				if (!FactoryToUse->CanCreateActorFrom(FAssetData(&SourceObject), ErrorText))
				{
					if (!ErrorText.IsEmpty())
					{
						ErrorText = FText::Format(LOCTEXT("CannotCreateActorFromAsset_Ex", "Unable to create spawnable from  asset '{0}'. {1}."), FText::FromString(SourceObject.GetName()), ErrorText);
					}
					else
					{
						ErrorText = FText::Format(LOCTEXT("CannotCreateActorFromAsset", "Unable to create spawnable from  asset '{0}'."), FText::FromString(SourceObject.GetName()));
					}
				}

				UWorld* World = EditScene->GetWorld();
				if (World)
				{
					const FName ActorName = MakeUniqueObjectName(World->PersistentLevel, FactoryToUse->NewActorClass->StaticClass(), TemplateName);

					AActor* Instance = FactoryToUse->CreateActor(&SourceObject, World->PersistentLevel, FTransform(), RF_Transient | RF_Transactional, ActorName);
					Instance->bIsEditorPreviewActor = false;
					NewSpawnable.ObjectTemplate = StaticDuplicateObject(Instance, Outer, TemplateName, RF_AllFlags & ~RF_Transient);

					const bool bNetForce = false;
					const bool bShouldModifyLevel = false;
					World->DestroyActor(Instance, bNetForce, bShouldModifyLevel);
				}
			}
		}

		if (!NewSpawnable.ObjectTemplate || !NewSpawnable.ObjectTemplate->IsA<AActor>())
		{
			if (UClass* InClass = Cast<UClass>(&SourceObject))
			{
				if (!InClass->IsChildOf(AActor::StaticClass()))
				{
					ErrorText = FText::Format(LOCTEXT("NotAnActorClass", "Unable to add spawnable for class of type '{0}' since it is not a valid actor class."), FText::FromString(InClass->GetName()));
					return MakeError(ErrorText);
				}

				NewSpawnable.ObjectTemplate = NewObject<UObject>(Outer, InClass, TemplateName, RF_Transactional);
			}

			if (!NewSpawnable.ObjectTemplate || !NewSpawnable.ObjectTemplate->IsA<AActor>())
			{
				if (ErrorText.IsEmpty())
				{
					ErrorText = FText::Format(LOCTEXT("UnknownClassError", "Unable to create a new spawnable object from {0}."), FText::FromString(SourceObject.GetName()));
				}

				return MakeError(ErrorText);
			}
		}

		return MakeValue(NewSpawnable);
	}
	void SetupDefaultsForSpawnable(UObject* SpawnedObject, const FGuid& Guid, const TOptional<FTransformData>& TransformData, TSharedRef<ISequencer> Sequencer, USequencerSettings* Settings) override
	{
		TOptional<FTransformData> DefaultTransform = TransformData;

		UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
		UMovieScene* OwnerMovieScene = Sequence->GetMovieScene();

		// Ensure it has a spawn track
		UGameActionDynamicSpawnTrack* SpawnTrack = Cast<UGameActionDynamicSpawnTrack>(OwnerMovieScene->FindTrack(UGameActionDynamicSpawnTrack::StaticClass(), Guid, NAME_None));
		if (!SpawnTrack)
		{
			SpawnTrack = Cast<UGameActionDynamicSpawnTrack>(OwnerMovieScene->AddTrack(UGameActionDynamicSpawnTrack::StaticClass(), Guid));
		}

		check(SpawnTrack)
		UGameActionSpawnByTemplateSection* SpawnSection = Cast<UGameActionSpawnByTemplateSection>(SpawnTrack->CreateNewSection());
		SpawnSection->SpawnerSettings = NewObject<UGameActionSequenceSpawnerSettings>(SpawnSection);
		// 默认不为引用，防止创建Spawnable时找不到Property
		SpawnSection->SpawnerSettings->bAsReference = false;
		SpawnTrack->AddSection(*SpawnSection);

		FMovieSceneSpawnRegister::DestroySpawnedObject(Guid, Sequencer->GetRootTemplateID(), *Sequencer);
		Sequencer->ForceEvaluate();
		AActor* SpawnedActor = Cast<AActor>(Sequencer->FindSpawnedObjectOrTemplate(Guid));
		if (SpawnedActor)
		{
			DefaultTransform.Reset();
			DefaultTransform.Emplace();
			DefaultTransform->Translation = SpawnedActor->GetActorLocation();
			DefaultTransform->Rotation = SpawnedActor->GetActorRotation();
			DefaultTransform->Scale = FVector(1.0f, 1.0f, 1.0f);

			Sequencer->OnActorAddedToSequencer().Broadcast(SpawnedActor, Guid);

			EditScene->SelectActor(SpawnedActor);
		}
	}
};

void FGameActionSequencer::OpenGameActionSequencer(UBPNode_GameActionSegment* ActionSegmentNode)
{
	UGameActionSegment* GameActionSegment = CastChecked<UGameActionSegment>(ActionSegmentNode->GameActionSegment);
	check(GameActionSegment && GameActionSegment->GameActionSequence);

	if (PreviewActionSegmentNode.IsValid())
	{
		CloseGameActionSequencer();
	}
	PreviewActionSegmentNode = ActionSegmentNode;

	bool IsEditable = EditScene->GameActionBlueprint->IsRootBlueprint();
	UGameActionSequence* ShowSequence = GameActionSegment->GameActionSequence;
	if (IsEditable == false)
	{
		for (UGameActionBlueprint* GameActionBlueprint = EditScene->GameActionBlueprint; GameActionBlueprint != nullptr; GameActionBlueprint = GameActionBlueprint->ParentClass ? Cast<UGameActionBlueprint>(GameActionBlueprint->ParentClass->ClassGeneratedBy) : nullptr)
		{
			TArray<FGameActionSequenceOverride>& SequenceOverrides = GameActionBlueprint->SequenceOverrides;
			if (FGameActionSequenceOverride* SequenceOverride = SequenceOverrides.FindByPredicate([&](const FGameActionSequenceOverride& E) { return E.NodeOverride.Get() == ActionSegmentNode; }))
			{
				if (SequenceOverride->bEnableOverride)
				{
					ShowSequence = SequenceOverride->SequenceOverride;
					IsEditable = GameActionBlueprint == EditScene->GameActionBlueprint;
				}
			}
		}
	}
	if (IsEditable)
	{
		EditingActionSequence = ShowSequence;
		ShowSequence->BelongToEditScene = EditScene;
	}

	UGameActionInstanceBase* GameActionInstance = EditScene->GetGameActionInstance();
	if (ensure(GameActionInstance) == false)
	{
		return;
	}
	
	FSequencerInitParams SequencerInitParams;
	{
		FSequencerViewParams ViewParams(TEXT("GameActionSettings"));
		{
			ViewParams.UniqueName = "GameActionSequenceEditor";
			// ViewParams.OnGetAddMenuContent = OnGetSequencerAddMenuContent;
		}

		SequencerInitParams.ViewParams = ViewParams;
		SequencerInitParams.RootSequence = ShowSequence;
		SequencerInitParams.PlaybackContext = TAttribute<UObject*>::Create([Scene = TWeakPtr<FGameActionEditScene>(EditScene)]()->UGameActionInstanceBase*
		{
			if (Scene.IsValid())
			{
				return Scene.Pin()->GetGameActionInstance();
			}
			return nullptr;
		});
		SequencerInitParams.SpawnRegister = MakeShareable(new FGameActionEditorSpawnRegister(EditScene));;
		SequencerInitParams.bEditWithinLevelEditor = false;
		SequencerInitParams.ToolkitHost = ToolkitHost.Pin();
	}
	
	ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
	SequencerModule.GetToolBarExtensibilityManager()->AddExtender(GameActionToolbarExtender);
	ON_SCOPE_EXIT
	{
		SequencerModule.GetToolBarExtensibilityManager()->RemoveExtender(GameActionToolbarExtender);
	};
	
	PreviewSequencer = SequencerModule.CreateSequencer(SequencerInitParams);
	USequencerSettings* SequencerSettings = PreviewSequencer->GetSequencerSettings();
	SequencerSettings->SetCompileDirectorOnEvaluate(false);
	SequencerSettings->SetShowRangeSlider(true);
	SequencerSettings->SetKeepPlayRangeInSectionBounds(false);
	SequencerSettings->SetInfiniteKeyAreas(true);
	PreviewSequencer->SetPlaybackStatus(EMovieScenePlayerStatus::Stopped);

	OnActorMoveHandle = GEditor->OnActorMoved().AddLambda([this](AActor* Actor)
	{
		if (Actor->GetWorld() == EditScene->GetWorld())
		{
			WhenPreviewObjectChanged(Actor);
		}
	});

	// 选中Track时同步选中对应的Actor
	PreviewSequencer->GetSelectionChangedObjectGuids().AddLambda([this](TArray<FGuid> ObjectGuids)
	{
		for (int32 Idx = ObjectGuids.Num() - 1; Idx >= 0; --Idx)
		{
			const FGuid Guid = ObjectGuids[Idx];

			UObject* BoundObject = PreviewSequencer->FindSpawnedObjectOrTemplate(Guid);
			if (ensure(BoundObject))
			{
				if (AActor* Actor = Cast<AActor>(BoundObject))
				{
					EditScene->SelectActor(Actor);
				}
				else if (UActorComponent* Component = Cast<UActorComponent>(BoundObject))
				{
					EditScene->SelectComponent(Component);
				}
				else
				{
					EditScene->SelectObject(BoundObject);
				}
			}
		}
	});

	PreviewSequencer->GetSelectionChangedSections().AddLambda([this](TArray<UMovieSceneSection*> Sections)
	{
		EditScene->ClearDrawableObject();
		UMovieScene* MovieScene = PreviewSequencer->GetFocusedMovieSceneSequence()->GetMovieScene();

		auto GetSectionBoundObject = [&](UMovieSceneSection* Section) -> UObject*
		{
			FGuid Guid;
			if (MovieScene->FindTrackBinding(*Section->GetTypedOuter<UMovieSceneTrack>(), Guid))
			{
				return PreviewSequencer->FindSpawnedObjectOrTemplate(Guid);
			}
			return nullptr;
		};

		UMovieSceneSection* FocusedSection = nullptr;
		
		const TSet<FSequencerSelectedKey>& SelectedKeys = SequencerHack::HackSelectedKeys(PreviewSequencer.Get());
		for (const FSequencerSelectedKey& Key : SelectedKeys)
		{
			if (Key.KeyHandle.IsSet())
			{
				if (UGameActionKeyEventSection* KeyEventSection = Cast<UGameActionKeyEventSection>(Key.Section))
				{
					TMovieSceneChannelData<FGameActionKeyEventValue> Data = KeyEventSection->KeyEventChannel.GetData();
					const int32 ValueIdx = Data.GetIndex(Key.KeyHandle.GetValue());
					const FGameActionKeyEventValue& Value = Data.GetValues()[ValueIdx];
					if (UGameActionKeyEvent* KeyEvent = Value.KeyEvent)
					{
						EditScene->SetDrawableObject(KeyEvent, GetSectionBoundObject(KeyEventSection));
						return;
					}
				}
				else
				{
					FocusedSection = Key.Section;
				}
			}
		}

		if (FocusedSection == nullptr && Sections.Num() > 0)
		{
			FocusedSection = Sections[0];
		}
		
		if (FocusedSection)
		{
			if (UGameActionStateEventSection* EventSection = Cast<UGameActionStateEventSection>(FocusedSection))
			{
				if (UGameActionStateEvent* StateEvent = EventSection->StateEvent)
				{
					EditScene->SetDrawableObject(StateEvent, GetSectionBoundObject(EventSection));
					return;
				}
			}
			else if (UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(FocusedSection))
			{
				if (TransformSection->GetTypedOuter<UMovieSceneTransformTrack>())
				{
					EditScene->SetDrawableObject(&WidgetSectionVisual, GetSectionBoundObject(TransformSection));
					WidgetSectionVisual.WidgetSection = TransformSection;
					SyncWidgetSectionPosition();
					WidgetSectionVisual.OnTransformChanged.BindLambda([this](const FTransform& NewTransform)
					{
						if (UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(WidgetSectionVisual.WidgetSection))
						{
							const FFrameNumber Time = PreviewSequencer->GetLocalTime().Time.FrameNumber;
							if (TransformSection->GetRange().Contains(Time))
							{
								if (TransformSection->TryModify() == false)
								{
									return;
								}
								
								const TArrayView<FMovieSceneFloatChannel*> FloatChannels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();

								const FVector Location = NewTransform.GetLocation();
								FloatChannels[0]->GetData().UpdateOrAddKey(Time, FMovieSceneFloatValue(Location.X));
								FloatChannels[1]->GetData().UpdateOrAddKey(Time, FMovieSceneFloatValue(Location.Y));
								FloatChannels[2]->GetData().UpdateOrAddKey(Time, FMovieSceneFloatValue(Location.Z));

								const FRotator Rotation = NewTransform.GetRotation().Rotator();
								FloatChannels[3]->GetData().UpdateOrAddKey(Time, FMovieSceneFloatValue(Rotation.Roll));
								FloatChannels[4]->GetData().UpdateOrAddKey(Time, FMovieSceneFloatValue(Rotation.Pitch));
								FloatChannels[5]->GetData().UpdateOrAddKey(Time, FMovieSceneFloatValue(Rotation.Yaw));

								const FVector Scale = NewTransform.GetScale3D();
								FloatChannels[6]->GetData().UpdateOrAddKey(Time, FMovieSceneFloatValue(Scale.X));
								FloatChannels[7]->GetData().UpdateOrAddKey(Time, FMovieSceneFloatValue(Scale.Y));
								FloatChannels[8]->GetData().UpdateOrAddKey(Time, FMovieSceneFloatValue(Scale.Z));

								PreviewSequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
								PreviewSequencer->ForceEvaluate();
							}
						}
					});
					return;
				}
			}
			else if (UMovieSceneVectorSection* VectorSection = Cast<UMovieSceneVectorSection>(FocusedSection))
			{
				EditScene->SetDrawableObject(&WidgetSectionVisual, GetSectionBoundObject(VectorSection));
				WidgetSectionVisual.WidgetSection = VectorSection;
				SyncWidgetSectionPosition();
				WidgetSectionVisual.OnTransformChanged.BindLambda([this](const FTransform& NewTransform)
				{
					if (UMovieSceneVectorSection* VectorSection = Cast<UMovieSceneVectorSection>(WidgetSectionVisual.WidgetSection))
					{
						const FFrameNumber Time = PreviewSequencer->GetLocalTime().Time.FrameNumber;
						if (VectorSection->GetRange().Contains(Time))
						{
							if (VectorSection->TryModify() == false)
							{
								return;
							}
							
							const TArrayView<FMovieSceneFloatChannel*> FloatChannels = VectorSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();

							const FVector Location = NewTransform.GetLocation();
							FloatChannels[0]->GetData().UpdateOrAddKey(Time, FMovieSceneFloatValue(Location.X));
							FloatChannels[1]->GetData().UpdateOrAddKey(Time, FMovieSceneFloatValue(Location.Y));
							FloatChannels[2]->GetData().UpdateOrAddKey(Time, FMovieSceneFloatValue(Location.Z));

							PreviewSequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
							PreviewSequencer->ForceEvaluate();
						}
					}
				});
				return;
			}
		}
	});
	PreviewSequencer->OnGlobalTimeChanged().AddSP(this, &FGameActionSequencer::SyncWidgetSectionPosition);
	PreviewSequencer->OnMovieSceneDataChanged().AddLambda([this](EMovieSceneDataChangeType ChangeType)
	{
		if (UGameActionBlueprint* GameActionBlueprint = EditScene->GameActionBlueprint)
		{
			GameActionBlueprint->Status = EBlueprintStatus::BS_Dirty;
		}
	});
	PreviewSequencer->OnCameraCut().AddLambda([this](UObject* CameraObject, bool bIsJump)
	{
		// TODO：参考FSequencer::UpdateCameraCut完成镜头轨的预览
		if (PreviewSequencer->IsPerspectiveViewportCameraCutEnabled())
		{
			EditScene->CameraCutsViewActor = Cast<AActor>(CameraObject);
			EditScene->ToViewActor.Reset();
		}
		else
		{
			EditScene->CameraCutsViewActor.Reset();
		}
	});

	ActionSequencerWidget->SetActiveWidgetIndex(1);
	TSharedRef<SWidget> SequencerWidget = PreviewSequencer->GetSequencerWidget();
	SequencerWidget->SetEnabled(IsEditable);
	SequencerContent->SetContent(SequencerWidget);
}

void FGameActionSequencer::CloseGameActionSequencer()
{
	if (PreviewActionSegmentNode.IsValid())
	{
		if (EditingActionSequence.IsValid())
		{
			EditingActionSequence->BelongToEditScene = nullptr;
		}
		PreviewActionSegmentNode = nullptr;
	}
	if (PreviewSequencer.IsValid())
	{
		PreviewSequencer->Close();
		PreviewSequencer = nullptr;
	}
	GEditor->OnActorMoved().Remove(OnActorMoveHandle);
	ActionSequencerWidget->SetActiveWidgetIndex(0);
	SequencerContent->SetContent(SNullWidget::NullWidget);
}

void FGameActionSequencer::WhenSelectedNodesChanged(const TSet<class UObject*>& NewSelection)
{
	for (UObject* Node : NewSelection)
	{
		if (UBPNode_GameActionSegment* GameActionSegmentNode = Cast<UBPNode_GameActionSegment>(Node))
		{
			if (PreviewActionSegmentNode.Get() != GameActionSegmentNode)
			{
				OpenGameActionSequencer(GameActionSegmentNode);
			}
			break;
		}
	}
}

void FGameActionSequencer::WhenDeleteNode(UBPNode_GameActionSegmentBase* GameActionSegmentNode)
{
	if (GameActionSegmentNode == PreviewActionSegmentNode.Get())
	{
		CloseGameActionSequencer();
	}
}

void FGameActionSequencer::PostBlueprintCompiled()
{
	if (UBPNode_GameActionSegment* ActionSegmentNode = PreviewActionSegmentNode.Get())
	{
		OpenGameActionSequencer(ActionSegmentNode);
	}
}

void FGameActionSequencer::WhenPreviewObjectChanged(UObject* PreviewObject)
{
	if (ISequencer* Sequencer = PreviewSequencer.Get())
	{
		UMovieSceneSequence* MovieSequence = Sequencer->GetFocusedMovieSceneSequence();
		AActor* PreviewActor = Cast<AActor>(PreviewObject);
		if (PreviewActor == nullptr)
		{
			PreviewActor = Cast<AActor>(MovieSequence->GetParentObject(PreviewObject));
		}
		if (PreviewActor == nullptr)
		{
			return;
		}
		
		UMovieScene* MovieScene = MovieSequence->GetMovieScene();
		for (int32 Idx = 0; Idx < MovieScene->GetSpawnableCount(); ++Idx)
		{
			// 同步Spawnable实例的数据至Template
			FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(Idx);
			if (PreviewActor == Sequencer->FindSpawnedObjectOrTemplate(Spawnable.GetGuid()))
			{
				AActor* Template = CastChecked<AActor>(Spawnable.GetObjectTemplate());
				if (ensure(Template) == false)
				{
					return;
				}

				MovieScene->Modify();
				EditorUtilities::CopyActorProperties(PreviewActor, Template);
				Spawnable.SpawnTransform = PreviewActor->GetActorTransform();
				return;
			}
		}
	}
}

TSharedRef<SWidget> FGameActionSequencer::GetSequencerWidget() const
{
	return ActionSequencerWidget.ToSharedRef();
}

void FGameActionSequencer::SetInSimulation(bool bInSimulation)
{
	ActionSequencerWidget->SetEnabled(bInSimulation);
}

void FGameActionSequencer::SyncWidgetSectionPosition()
{
	const FFrameNumber Time = PreviewSequencer->GetLocalTime().Time.FrameNumber;
	if (UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(WidgetSectionVisual.WidgetSection))
	{
		const TArrayView<FMovieSceneFloatChannel*> FloatChannels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();

		float X, Y, Z;
		FloatChannels[0]->Evaluate(Time, X);
		FloatChannels[1]->Evaluate(Time, Y);
		FloatChannels[2]->Evaluate(Time, Z);
		const FVector Location{ X, Y, Z };

		float Roll, Pitch, Yaw;
		FloatChannels[3]->Evaluate(Time, Roll);
		FloatChannels[4]->Evaluate(Time, Pitch);
		FloatChannels[5]->Evaluate(Time, Yaw);
		const FRotator Rotation{ Pitch, Yaw, Roll };

		float SX, SY, SZ;
		FloatChannels[6]->Evaluate(Time, SX);
		FloatChannels[7]->Evaluate(Time, SY);
		FloatChannels[8]->Evaluate(Time, SZ);
		const FVector Size{ SX, SY, SZ };

		WidgetSectionVisual.Transform = FTransform(Rotation.Quaternion(), Location, Size);
	}
	else if (UMovieSceneVectorSection* VectorSection = Cast<UMovieSceneVectorSection>(WidgetSectionVisual.WidgetSection))
	{
		const TArrayView<FMovieSceneFloatChannel*> FloatChannels = VectorSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
		float X, Y, Z;
		FloatChannels[0]->Evaluate(Time, X);
		FloatChannels[1]->Evaluate(Time, Y);
		FloatChannels[2]->Evaluate(Time, Z);
		const FVector Location{ X, Y, Z };

		WidgetSectionVisual.Transform = FTransform(Location);
	}
}

bool FGameActionSequencer::FGameActionDetailKeyframeHandler::IsPropertyKeyable(UClass* InObjectClass, const IPropertyHandle& InPropertyHandle) const
{
	const FCanKeyPropertyParams CanKeyPropertyParams(InObjectClass, InPropertyHandle);

	const TSharedPtr<ISequencer> Sequencer = GetSequencer();
	if (Sequencer.IsValid() && Sequencer->CanKeyProperty(CanKeyPropertyParams))
	{
		return true;
	}
	return false;
}

bool FGameActionSequencer::FGameActionDetailKeyframeHandler::IsPropertyKeyingEnabled() const
{
	const TSharedPtr<ISequencer> Sequencer = GetSequencer();
	if (Sequencer.IsValid() && Sequencer->GetFocusedMovieSceneSequence() && Sequencer->GetAllowEditsMode() != EAllowEditsMode::AllowLevelEditsOnly)
	{
		return true;
	}
	return false;
}

bool FGameActionSequencer::FGameActionDetailKeyframeHandler::IsPropertyAnimated(const IPropertyHandle& PropertyHandle, UObject* ParentObject) const
{
	TSharedPtr<ISequencer> Sequencer = GetSequencer();
	if (Sequencer.IsValid() && Sequencer->GetFocusedMovieSceneSequence())
	{
		const FGuid ObjectHandle = Sequencer->GetHandleToObject(ParentObject);
		if (ObjectHandle.IsValid())
		{
			UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
			FProperty* Property = PropertyHandle.GetProperty();
			TSharedRef<FPropertyPath> PropertyPath = FPropertyPath::CreateEmpty();
			PropertyPath->AddProperty(FPropertyInfo(Property));
			const FName PropertyName(*PropertyPath->ToString(TEXT(".")));
			return MovieScene->FindTrack(nullptr, ObjectHandle, PropertyName) != nullptr;
		}

		return false;
	}
	return false;
}

void FGameActionSequencer::FGameActionDetailKeyframeHandler::OnKeyPropertyClicked(const IPropertyHandle& KeyedPropertyHandle)
{
	TArray<UObject*> Objects;
	KeyedPropertyHandle.GetOuterObjects(Objects);
	const FKeyPropertyParams KeyPropertyParams(Objects, KeyedPropertyHandle, ESequencerKeyMode::ManualKeyForced);

	TSharedPtr<ISequencer> Sequencer = GetSequencer();
	if (Sequencer.IsValid())
	{
		Sequencer->KeyProperty(KeyPropertyParams);
	}
}

const TSharedPtr<ISequencer> FGameActionSequencer::FGameActionDetailKeyframeHandler::GetSequencer() const
{
	return GameActionSequencer.Pin()->PreviewSequencer;
}

void FGameActionSequencer::CreateGameActionCamera()
{
	UGameActionSequence* GameActionSequence = CastChecked<UGameActionSequence>(PreviewSequencer->GetFocusedMovieSceneSequence());
	UMovieScene* FocusedMovieScene = GameActionSequence->GetMovieScene();
	if (FocusedMovieScene->IsReadOnly())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateCameraHere", "Create Camera Here"));

	ACineCameraActor* NewCamera = NewObject<ACineCameraActor>(FocusedMovieScene, ACineCameraActor::StaticClass(), NAME_None, RF_Transactional);
	NewCamera->GetCineCameraComponent()->bConstrainAspectRatio = false;
	const FGuid CameraGuid = GameActionSequence->AddSpawnable(NewCamera->GetName(), *NewCamera);
	UGameActionDynamicSpawnTrack* SpawnTrack = FocusedMovieScene->AddTrack<UGameActionDynamicSpawnTrack>(CameraGuid);
	UGameActionSpawnByTemplateSection* SpawnSection = NewObject<UGameActionSpawnByTemplateSection>(SpawnTrack);
	SpawnSection->SpawnerSettings = NewObject<UGameActionSequenceSpawnerSettings>(SpawnSection);
	SpawnSection->SpawnerSettings->DestroyDelayTime = 0.5f;
	SpawnSection->SpawnerSettings->bAsReference = false;
	SpawnSection->SpawnerSettings->Ownership = EGameActionSpawnOwnership::Sequence;
	SpawnTrack->AddSection(*SpawnSection);
	
	if (EditScene->EditorViewportClient.IsValid())
	{
		const TSharedPtr<FEditorViewportClient> ViewportClient = EditScene->EditorViewportClient.Pin();
		NewCamera->SetActorLocationAndRotation(ViewportClient->GetViewLocation(), ViewportClient->GetViewRotation(), false);
	}

	MovieSceneToolHelpers::CameraAdded(FocusedMovieScene, CameraGuid, PreviewSequencer->GetLocalTime().Time.FloorToFrame());
	PreviewSequencer->OnActorAddedToSequencer().Broadcast(NewCamera, CameraGuid);
	PreviewSequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
}

void UGameActionSequencerWidgetSectionVisual::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UGameActionSequencerWidgetSectionVisual, Transform))
	{
		OnTransformChanged.ExecuteIfBound(Transform);
	}
}

#undef LOCTEXT_NAMESPACE
