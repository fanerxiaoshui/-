// Fill out your copyright notice in the Description page of Project Settings.


#include "GameAction/GameActionComponent.h"
#include <Net/UnrealNetwork.h>
#include <Engine/ActorChannel.h>
#include <GameFramework/Character.h>

#include "GameAction/GameActionInstance.h"
#include "GameAction/GameActionSegment.h"
#include "Sequence/GameActionSequencePlayer.h"
#include "Utils/GameAction_Log.h"

// Sets default values for this component's properties
UGameActionComponent::UGameActionComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
	SetIsReplicatedByDefault(true);

	SharedPlayer = CreateDefaultSubobject<UGameActionSequencePlayer>(GET_MEMBER_NAME_CHECKED(UGameActionComponent, SharedPlayer));
}

// Called when the game starts
void UGameActionComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
	if (HasAuthority())
	{
		for (const TSubclassOf<UGameActionInstanceBase>& DefaultAction : DefaultActions)
		{
			if (ensure(DefaultAction))
			{
				UGameActionInstanceBase* ActionInstance = NewObject<UGameActionInstanceBase>(this, DefaultAction, DefaultAction->GetFName());
				AddGameActionNoCheck(ActionInstance);
			}
		}
	}
}

void UGameActionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	for (int32 Idx = ActionInstances.Num() - 1; Idx >= 0; --Idx)
	{
		UGameActionInstanceBase* ActionInstance = ActionInstances[Idx];
		if (ActionInstance->IsActived())
		{
			ActionInstance->DeactiveInstance();
		}
	}
}

// Called every frame
void UGameActionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...

	{
		SharedPlayer->Update(DeltaTime);
	}
	
	// 反向迭代，防止当ActionInstance在Tick过程中销毁自己导致漏迭代
	for (int32 Idx = ActionInstances.Num() - 1; Idx >=0; --Idx)
	{
		UGameActionInstanceBase* ActionInstance = ActionInstances[Idx];
		ActionInstance->Tick(DeltaTime);
	}
}

void UGameActionComponent::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UGameActionComponent, ActionInstances);
}

bool UGameActionComponent::ReplicateSubobjects(class UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool WroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);
	for (UGameActionInstanceBase* ActionInstance : ActionInstances)
	{
		ActionInstance->ReplicateSubobject(WroteSomething, Channel, Bunch, RepFlags);
		WroteSomething |= Channel->ReplicateSubobject(ActionInstance, *Bunch, *RepFlags);
	}
	WroteSomething |= Channel->ReplicateSubobject(SharedPlayer, *Bunch, *RepFlags);
	return WroteSomething;
}

void UGameActionComponent::OnRep_ActionInstances()
{
	TSet<UGameActionInstanceBase*> ActionInstancesSet{ ActionInstances };
	TSet<UGameActionInstanceBase*> DeactivedInstances = PreActionInstances.Difference(ActionInstancesSet);
	TSet<UGameActionInstanceBase*> CurActivedInstances = ActionInstancesSet.Difference(PreActionInstances);
	
	for (UGameActionInstanceBase* ActionInstance : CurActivedInstances)
	{
		ActionInstance->ConstructInstance();
	}
	for (UGameActionInstanceBase* ActionInstance : DeactivedInstances)
	{
		// 处理PlayGameAction后Instance立马销毁导致ActivedSegment没同步导致未Deactived的问题
		if (ActionInstance->IsActived())
		{
			ActionInstance->ActivedSegment->DeactiveAction();
			ActionInstance->DeactiveInstance();
		}
		ActionInstance->DestructInstance();
	}

	PreActionInstances = ActionInstancesSet;
}

UGameActionInstanceBase* UGameActionComponent::AddGameAction(TSubclassOf<UGameActionInstanceBase> Action)
{
	check(HasAuthority());
	if (ensureAlways(Action && ContainGameAction(Action) == false))
	{
		const FName InstanceName = Action->GetFName();
		UGameActionInstanceBase* ActionInstance = FindObjectFast<UGameActionInstanceBase>(this, InstanceName);
		if (ActionInstance == nullptr)
		{
			ActionInstance = NewObject<UGameActionInstanceBase>(this, Action, InstanceName);
		}
		AddGameActionNoCheck(ActionInstance);
		return ActionInstance;
	}
	return nullptr;
}

void UGameActionComponent::RemoveGameAction(TSubclassOf<UGameActionInstanceBase> Action)
{
	check(HasAuthority());
	const int32 Idx = ActionInstances.IndexOfByPredicate([&](UGameActionInstanceBase* E) { return E->GetClass() == Action; } );
	if (ensure(Idx != INDEX_NONE))
	{
		UGameActionInstanceBase* ActionInstance = ActionInstances[Idx];
		ActionInstances.RemoveAt(Idx);
		ActionInstance->DestructInstance();
	}
}

UGameActionInstanceBase* UGameActionComponent::FindGameAction(TSubclassOf<UGameActionInstanceBase> Action) const
{
#if DO_CHECK
	ensure(const_cast<TArray<UGameActionInstanceBase*>&>(ActionInstances).Remove(nullptr) == 0);
#endif
	const int32 Idx = ActionInstances.IndexOfByPredicate([&](UGameActionInstanceBase* E) { return E->GetClass() == Action; });
	return Idx != INDEX_NONE ? ActionInstances[Idx] : nullptr;
}

void UGameActionComponent::PlayGameAction(TSubclassOf<UGameActionInstanceBase> Action, FName EntryName)
{
	if (ensure(Action && ContainGameAction(Action) == false))
	{
		if (HasAuthority())
		{
			PlayGameActionOnServer(Action, EntryName);
		}
		else
		{
			PlayGameActionToServer(Action, EntryName);
		}
	}
}

UGameActionInstanceBase* UGameActionComponent::CreateGameActionToPlay(TSubclassOf<UGameActionInstanceBase> Action)
{
	check(HasAuthority());
	UGameActionInstanceBase* Instance = AddGameAction(Action);
	return Instance;
}

bool UGameActionComponent::TryPlayCreatedGameAction(UGameActionInstanceBase* Instance, const FGameActionEntry& Entry)
{
	if (ensure(Instance))
	{
		const bool PlaySucceed = [&]
		{
			for (const FGameActionEntryTransition& EntryTransition : Entry.Transitions)
			{
				if (EntryTransition.CanTransition(nullptr, false))
				{
					Instance->ActiveInstance();
					EntryTransition.TransitionToSegment->ActiveAction();
					return true;
				}
			}
			return false;
		}();
		if (ensure(PlaySucceed))
		{
			Instance->OnInstanceDeactivedNative.AddWeakLambda(this, [this](UGameActionInstanceBase* ActionInstance, bool IsAbort)
			{
				RemoveGameAction(ActionInstance->GetClass());
				ActionInstance->OnInstanceDeactivedNative.RemoveAll(this);
			});
			if (IsLocalControlled() == false)
			{
				Instance->EntryCreatedActionToClient(this, Instance->ActivedSegment);
			}
		}
		else
		{
			RemoveGameAction(Instance->GetClass());
		}
		return PlaySucceed;
	}
	return false;
}

void UGameActionComponent::PlayGameActionOnServer(const TSubclassOf<UGameActionInstanceBase>& Action, FName EntryName)
{
	if (ensure(Action && ContainGameAction(Action) == false))
	{
		UGameActionInstanceBase* Instance = CreateGameActionToPlay(Action);
		if (EntryName != NAME_None)
		{
			FStructProperty* EntryProperty = FindFProperty<FStructProperty>(Action, EntryName);
			if (ensure(EntryProperty && EntryProperty->Struct == FGameActionEntry::StaticStruct()))
			{
				TryPlayCreatedGameAction(Instance, *EntryProperty->ContainerPtrToValuePtr<FGameActionEntry>(Instance));
			}
		}
		else
		{
			TryPlayCreatedGameAction(Instance, Instance->DefaultEntry);
		}
	}
}

bool UGameActionComponent::IsAnyActionActived() const
{
	if (IsSharedPlayerPlaying())
	{
		return true;
	}
	return ActionInstances.ContainsByPredicate([](UGameActionInstanceBase* E) { return E->IsActived(); });
}

bool UGameActionComponent::IsLocalControlled() const
{
	return CastChecked<ACharacter>(GetOwner())->IsLocallyControlled();
}

bool UGameActionComponent::HasAuthority() const
{
	return GetOwner()->HasAuthority();
}

void UGameActionComponent::PlayGameActionToServer_Implementation(TSubclassOf<UGameActionInstanceBase> Action, FName EntryName)
{
	PlayGameActionOnServer(Action, EntryName);
}

void UGameActionComponent::AddGameActionNoCheck(UGameActionInstanceBase* ActionInstance)
{
	ActionInstances.Add(ActionInstance);
	ActionInstance->OwningComponent = this;
	if (ActionInstance->bSharePlayer)
	{
		ActionInstance->SequencePlayer = SharedPlayer;
	}
	ActionInstance->ConstructInstance();
}

bool UGameActionComponent::IsSharedPlayerPlaying() const
{
	return SharedPlayer->IsPlaying();
}

UGameActionInstanceBase* UGameActionComponent::GetSharedPlayerActiveAction() const
{
	if (IsSharedPlayerPlaying())
	{
		const int32 Index = ActionInstances.IndexOfByPredicate([&](UGameActionInstanceBase* E) { return SharedPlayer->GameAction == E; });
		if (Index != INDEX_NONE)
		{
			return ActionInstances[Index];
		}
	}
	return nullptr;
}

void UGameActionComponent::AbortSharedPlayerActiveAction()
{
	if (UGameActionInstanceBase* SharedPlayerAction = GetSharedPlayerActiveAction())
	{
		SharedPlayerAction->AbortGameAction();
	}
}
