// Fill out your copyright notice in the Description page of Project Settings.


#include "Editor/GameActionPreviewScene.h"
#include <GameFramework/Character.h>
#include <Engine/Selection.h>
#include <EditorViewportClient.h>
#include <EditorModeManager.h>
#include <Animation/AnimInstance.h>
#include <Kismet/GameplayStatics.h>
#include <GameFramework/GameStateBase.h>

#include "Blueprint/GameActionBlueprint.h"
#include "GameAction/GameActionComponent.h"
#include "GameAction/GameActionInstance.h"

FGameActionPreviewScene::FGameActionPreviewScene(ConstructionValues CVS, UGameActionBlueprint* InGameActionBlueprint)
	: Super(CVS), GameActionBlueprint(InGameActionBlueprint)
{
	UWorld* World = GetWorld();
	World->bAllowAudioPlayback = true;
	FloorMeshComponent->SetWorldScale3D(FVector(1000.f, 1000.f, 1.f));
}

FGameActionPreviewScene::~FGameActionPreviewScene()
{
	if (ACharacter* Owner = PreviewOwner)
	{
		Owner->Destroy();
		PreviewOwner = nullptr;
	}
	for (AActor* PossessableActor : PreviewActors)
	{
		if (ensure(PossessableActor))
		{
			PossessableActor->Destroy();
		}
	}
	PreviewActors.Empty();
}

void FGameActionPreviewScene::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	GetWorld()->Tick(LEVELTICK_All, DeltaTime);
}

void FGameActionPreviewScene::SetPreviewGameActionScene(UGameActionScene* GameActionScene)
{
	check(GameActionScene);

	UWorld* World = GetWorld();
	// 初始化游戏世界依赖
	{
		const TSubclassOf<UGameInstance> GameInstanceType = GameActionScene->GameInstanceType;
		const TSubclassOf<AGameStateBase> GameStateType = GameActionScene->GameStateType;
		const TSubclassOf<AGameModeBase> GameModeType = GameActionScene->GameModeType;

		UGameInstance* GameInstance = NewObject<UGameInstance>(World, GameInstanceType);
		World->SetGameInstance(GameInstance);

		AGameStateBase* GameState = World->SpawnActor<AGameStateBase>(GameStateType);
		AGameModeBase* GameMode = World->SpawnActor<AGameModeBase>(GameModeType);
		// 无法直接通过SetGameMode设置，先这样设置进去
		World->CopyGameState(GameMode, GameState);
	}

	if (ACharacter* Owner = PreviewOwner)
	{
		Owner->Destroy();
		Owner->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
		PreviewOwner = nullptr;
	}
	for (AActor* PossessableActor : PreviewActors)
	{
		if (ensure(PossessableActor))
		{
			PossessableActor->Destroy();
			PossessableActor->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
		}
	}
	PreviewActors.Empty();

	{
		FActorSpawnParameters ActorSpawnParameters;
		ActorSpawnParameters.Template = GameActionScene->OwnerTemplate;
		ActorSpawnParameters.Name = UGameActionInstanceBase::GameActionOwnerName;
		PreviewOwner = GetWorld()->SpawnActor<ACharacter>(GameActionScene->OwnerTemplate->GetClass(), ActorSpawnParameters);
	}
	CreatePreviewInstance();

	UGameActionInstanceBase* GameActionInstance = GetGameActionInstance();
	UClass* GameActionInstanceClass = GameActionInstance->GetClass();
	for (const FGameActionSceneActorData& ActorData : GameActionScene->TemplateDatas)
	{
		if (AActor* TemplateActor = ActorData.Template)
		{
			const FName PossessableName = TemplateActor->GetFName();
			FActorSpawnParameters ActorSpawnParameters;
			ActorSpawnParameters.Name = PossessableName;
			ActorSpawnParameters.Template = TemplateActor;
			ActorSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* PossessableActor = World->SpawnActor<AActor>(TemplateActor->GetClass(), ActorData.GetSpawnTransform(), ActorSpawnParameters);
			PreviewActors.Add(PossessableActor);

			if (FObjectProperty* ObjectProperty = FindFProperty<FObjectProperty>(GameActionInstanceClass, PossessableName))
			{
				if (UObject** P_Possessable = ObjectProperty->ContainerPtrToValuePtr<UObject*>(GameActionInstance))
				{
					*P_Possessable = PossessableActor;
				}
			}
		}
	}
}

void FGameActionPreviewScene::CreatePreviewInstance()
{
	GameActionComponent = PreviewOwner->FindComponentByClass<UGameActionComponent>();
	if (GameActionComponent == nullptr)
	{
		GameActionComponent = NewObject<UGameActionComponent>(PreviewOwner);
		GameActionComponent->RegisterComponent();
		PreviewOwner->AddOwnedComponent(GameActionComponent);
	}
	else
	{
		GameActionComponent->ActionInstances.Empty();
	}
	const TSubclassOf<UGameActionInstanceBase> ActionType{ GameActionBlueprint->GeneratedClass };
	PreviewInstance = GameActionComponent->FindGameAction(ActionType);
	if (PreviewInstance == nullptr)
	{
		PreviewInstance = GameActionComponent->AddGameAction(ActionType);
	}
	PreviewInstance->bIsSimulation = true;
}

void FGameActionPreviewScene::SelectActor(AActor* Actor) const
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

void FGameActionPreviewScene::SelectComponent(UActorComponent* Component) const
{
	if (EditorViewportClient.IsValid())
	{
		USelection* Selection = EditorViewportClient.Pin()->GetModeTools()->GetSelectedActors();
		Selection->DeselectAll();
		Selection->Select(Component);
		GEditor->SelectNone(false, false);
		GEditor->SelectActor(Component->GetOwner(), true, true, false, true);
		GEditor->SelectComponent(Component, true, true, false);
	}
}

void FGameActionPreviewScene::SelectObject(UObject* Object) const
{
	if (EditorViewportClient.IsValid())
	{
		USelection* Selection = EditorViewportClient.Pin()->GetModeTools()->GetSelectedActors();
		Selection->DeselectAll();
		Selection->Select(Object);
		GEditor->SelectNone(false, false);
	}
}

void FGameActionPreviewScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(GameActionBlueprint);
	Collector.AddReferencedObject(PreviewInstance);
	Collector.AddReferencedObject(GameActionComponent);
	Collector.AddReferencedObject(PreviewOwner);
	Collector.AddReferencedObjects(PreviewActors);
}

FGameActionEditScene::FGameActionEditScene(UGameActionBlueprint* InGameActionBlueprint)
	: Super(ConstructionValues().ShouldSimulatePhysics(true).SetEditor(true), InGameActionBlueprint)
{
	DrawableObjectOrigin = PreviewOwner;

	SetPreviewGameActionScene(InGameActionBlueprint->GameActionScene);
}

void FGameActionEditScene::SetDrawableObject(UObject* Object, UObject* Origin)
{
	DrawableObject = Object;
	DrawableObjectOrigin = Origin ? Origin : PreviewOwner;
}

void FGameActionEditScene::ClearDrawableObject()
{
	DrawableObject = nullptr;
	DrawableObjectOrigin = PreviewOwner;
}

UObject* FGameActionEditScene::GetItemToTryDisplayingWidgetsFor(FTransform& OutWidgetToWorld) const
{
	if (const UObject* Origin = DrawableObjectOrigin.Get())
	{
		if (const AActor* Actor = Cast<AActor>(Origin))
		{
			// Character的原点为胶囊体底部
			if (const ACharacter* Character = Cast<ACharacter>(Origin))
			{
				FTransform TransformOrigin = Character->GetActorTransform();
				TransformOrigin.AddToTranslation(FVector(0.f, 0.f, -Character->GetDefaultHalfHeight()));
				OutWidgetToWorld = TransformOrigin;
			}
			else
			{
				OutWidgetToWorld = Actor->GetTransform();
			}
		}
		else if (const UActorComponent* Component = Cast<UActorComponent>(Origin))
		{
			if (const USceneComponent* SceneComponent = Cast<USceneComponent>(Origin))
			{
				OutWidgetToWorld = SceneComponent->GetComponentTransform();
			}
			else
			{
				OutWidgetToWorld = Component->GetOwner()->GetTransform();
			}
		}
		else if (const UAnimInstance* AnimInstance = Cast<UAnimInstance>(Origin))
		{
			OutWidgetToWorld = AnimInstance->GetOwningComponent()->GetComponentTransform();
		}
	}

	return DrawableObject.Get();
}

FGameActionSimulateScene::FGameActionSimulateScene(UGameActionBlueprint* InGameActionBlueprint)
	: Super(ConstructionValues().ShouldSimulatePhysics(true).SetEditor(false), InGameActionBlueprint)
{
	UWorld* World = GetWorld();
	World->bBegunPlay = true;
	
	// HACK：根骨骼位移的地面必须存在Owner
	UStaticMeshComponent* OldFloorComponent = FloorMeshComponent;
	AActor* FloorActor = World->SpawnActor<AActor>();
	FloorMeshComponent = NewObject<UStaticMeshComponent>(FloorActor);
	FloorMeshComponent->RegisterComponent();
	FloorMeshComponent->SetWorldTransform(OldFloorComponent->GetComponentTransform());
	FloorMeshComponent->SetStaticMesh(OldFloorComponent->GetStaticMesh());
	RemoveComponent(OldFloorComponent);
	OldFloorComponent->DestroyComponent();

	SetPreviewGameActionScene(InGameActionBlueprint->GameActionScene);
	
	// Pawn生成默认的Controller
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.ObjectFlags |= RF_Transient;
	SpawnInfo.bDeferConstruction = true;
	PlayerController = GetWorld()->SpawnActor<APlayerController>(SpawnInfo);
	PlayerController->SetAsLocalPlayerController();
	UGameplayStatics::FinishSpawningActor(PlayerController, FTransform());
	PlayerController->Possess(PreviewOwner);
	PlayerController->SetViewTarget(PreviewOwner);
	PlayerController->PlayerCameraManager->bDebugClientSideCamera = true;
	
	for (AActor* PossessableActor : PreviewActors)
	{
		if (APawn* Pawn = Cast<APawn>(PossessableActor))
		{
			Pawn->SpawnDefaultController();
		}
	}
}

void FGameActionSimulateScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);

	Collector.AddReferencedObject(PlayerController);
}
