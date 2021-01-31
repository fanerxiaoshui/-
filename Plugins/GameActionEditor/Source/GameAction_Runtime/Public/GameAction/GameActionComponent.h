	// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameActionType.h"
#include "Components/ActorComponent.h"
#include "Templates/SubclassOf.h"
#include "GameActionComponent.generated.h"

class UGameActionSegmentBase;
class UGameActionInstanceBase;
class UGameActionSequencePlayer;

UCLASS(ClassGroup=(Gameplay), meta=(BlueprintSpawnableComponent))
class GAMEACTION_RUNTIME_API UGameActionComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UGameActionComponent();

	void BeginPlay() override;
	void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	bool ReplicateSubobjects(class UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags) override;

	UPROPERTY(EditAnywhere, Category = "配置")
	TSet<TSubclassOf<UGameActionInstanceBase>> DefaultActions;

	UPROPERTY(Transient)
	TSet<UGameActionInstanceBase*> PreActionInstances;
	UPROPERTY(ReplicatedUsing = OnRep_ActionInstances, VisibleAnywhere, BlueprintReadOnly)
	TArray<UGameActionInstanceBase*> ActionInstances;
	UFUNCTION()
	void OnRep_ActionInstances();

	UFUNCTION(BlueprintCallable, Category = "GameAction", BlueprintAuthorityOnly, meta = (DeterminesOutputType = Action))
	UGameActionInstanceBase* AddGameAction(TSubclassOf<UGameActionInstanceBase> Action);

	UFUNCTION(BlueprintCallable, Category = "GameAction", BlueprintAuthorityOnly)
	void RemoveGameAction(TSubclassOf<UGameActionInstanceBase> Action);

	UFUNCTION(BlueprintCallable, Category = "GameAction", meta = (DeterminesOutputType = Action))
	UGameActionInstanceBase* FindGameAction(TSubclassOf<UGameActionInstanceBase> Action) const;
	template<typename T>
	T* FindGameActionNative(const TSubclassOf<T>& Action) const { return static_cast<T*>(FindGameAction(Action)); }

	UFUNCTION(BlueprintCallable, Category = "GameAction")
	bool ContainGameAction(TSubclassOf<UGameActionInstanceBase> Action) const { return FindGameAction(Action) != nullptr; }

	// 用来播放不常驻的行为，例如使用道具、交互等（这种行为的起始现在由服务器发起，无法主端预测）
	UFUNCTION(BlueprintCallable, Category = "GameAction")
	void PlayGameAction(TSubclassOf<UGameActionInstanceBase> Action, FName EntryName);

	UFUNCTION(BlueprintCallable, Category = "GameAction", BlueprintAuthorityOnly, meta = (DeterminesOutputType = Action))
	UGameActionInstanceBase* CreateGameActionToPlay(TSubclassOf<UGameActionInstanceBase> Action);
	template<typename T>
	T* CreateGameActionToPlayNative(const TSubclassOf<T>& Action) { return static_cast<T*>(CreateGameActionToPlay(Action)); }

	UFUNCTION(BlueprintCallable, Category = "GameAction", BlueprintAuthorityOnly)
	bool TryPlayCreatedGameAction(UGameActionInstanceBase* Instance, const FGameActionEntry& Entry);
	
	void PlayGameActionOnServer(const TSubclassOf<UGameActionInstanceBase>& Action, FName EntryName);

	UFUNCTION(BlueprintCallable, Category = "GameAction")
	bool IsAnyActionActived() const;

	bool IsLocalControlled() const;
	bool HasAuthority() const;
public:
	UFUNCTION(Server, Reliable)
	void PlayGameActionToServer(TSubclassOf<UGameActionInstanceBase> Action, FName EntryName);
protected:
	void AddGameActionNoCheck(UGameActionInstanceBase* ActionInstance);

private:
	friend class UGameActionInstanceBase;
	UPROPERTY()
	UGameActionSequencePlayer* SharedPlayer = nullptr;

public:
	UFUNCTION(BlueprintCallable, Category = "GameAction")
	bool IsSharedPlayerPlaying() const;
	UFUNCTION(BlueprintCallable, Category = "GameAction")
	UGameActionInstanceBase* GetSharedPlayerActiveAction() const;
	UFUNCTION(BlueprintCallable, Category = "GameAction")
	void AbortSharedPlayerActiveAction();
};
