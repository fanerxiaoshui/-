// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Templates/SubclassOf.h"
#include "GameActionEvent.generated.h"

class UGameActionInstanceBase;
class IMovieScenePlayer;

/**
 * 
 */
UCLASS(abstract, collapseCategories, DefaultToInstanced)
class GAMEACTION_RUNTIME_API UGameActionEventBase : public UObject
{
	GENERATED_BODY()
public:
	UGameActionEventBase();
	
	virtual FString GetEventName() const { return ReceiveGetEventName(); }
	
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "设置", meta = (DisplayName = "事件颜色"))
	FLinearColor EventColor = FColor(255, 200, 200, 255);

	UPROPERTY(EditAnywhere, Category = "设置", meta = (DisplayName = "编辑器下可执行"))
	uint8 bExecuteInEditor : 1;

	bool CanExecute() const;

	UPROPERTY(EditDefaultsOnly, Category = "设置", meta = (DisplayName = "支持的类型"))
	UClass* SupportClass;

	UPROPERTY(EditDefaultsOnly, Category = "设置", meta = (DisplayName = "支持的游戏行为"))
	TSubclassOf<UGameActionInstanceBase> SupportGameInstance;

	virtual bool IsSupportClass(const UClass* Class,
	                            const TSubclassOf<UGameActionInstanceBase>& ParentInstanceClass) const;
#endif
	UWorld* GetWorld() const override { return WorldContentObject ? WorldContentObject->GetWorld() : nullptr; }
protected:
	UFUNCTION(BlueprintNativeEvent, Category = "Event", meta = (DisplayName = "Get Event Name"))
	FString ReceiveGetEventName() const;

	friend class UGameActionStateInnerKeyEvent;
	UPROPERTY(Transient)
	mutable UObject* WorldContentObject = nullptr;
};

UCLASS(abstract, const, Blueprintable, meta = (DisplayName = "游戏行为帧事件"))
class GAMEACTION_RUNTIME_API UGameActionKeyEvent : public UGameActionEventBase
{
	GENERATED_BODY()
public:
	void ExecuteEvent(UObject* EventOwner, IMovieScenePlayer& Player) const;

protected:
	virtual void WhenEventExecute(UObject* EventOwner, IMovieScenePlayer& Player) const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Event")
	void ReceiveWhenEventExecute(UObject* EventOwner, UGameActionInstanceBase* GameActionInstance) const;
};

UCLASS(abstract, Blueprintable, meta = (DisplayName = "游戏行为状态事件"))
class GAMEACTION_RUNTIME_API UGameActionStateEvent : public UGameActionEventBase
{
	GENERATED_BODY()
public:
	UGameActionStateEvent()
		: bInstanced(false)
	{}
	
	void StartEvent(UObject* EventOwner, IMovieScenePlayer& Player);
	void TickEvent(UObject* EventOwner, IMovieScenePlayer& Player, float DeltaSeconds);
	void EndEvent(UObject* EventOwner, IMovieScenePlayer& Player, bool bIsCompleted);

	UPROPERTY(EditDefaultsOnly, Category = "设置", meta = (DisplayName = "实例化"))
	uint8 bInstanced : 1;
protected:
	virtual void WhenEventStart(UObject* EventOwner, IMovieScenePlayer& Player);
	virtual void WhenEventTick(UObject* EventOwner, IMovieScenePlayer& Player, float DeltaSeconds);
	virtual void WhenEventEnd(UObject* EventOwner, IMovieScenePlayer& Player, bool bIsCompleted);

	UFUNCTION(BlueprintImplementableEvent, Category = "Event")
	void ReceiveWhenEventStart(UObject* EventOwner, UGameActionInstanceBase* GameActionInstance);
	UFUNCTION(BlueprintImplementableEvent, Category = "Event")
	void ReceiveWhenEventTick(UObject* EventOwner, UGameActionInstanceBase* GameActionInstance, float DeltaSeconds);
	UFUNCTION(BlueprintImplementableEvent, Category = "Event")
	void ReceiveWhenEventEnd(UObject* EventOwner, UGameActionInstanceBase* GameActionInstance, bool bIsCompleted);
};

UCLASS(abstract, const, Blueprintable, meta = (DisplayName = "游戏行为状态内部帧事件"))
class GAMEACTION_RUNTIME_API UGameActionStateInnerKeyEvent : public UObject
{
	GENERATED_BODY()
public:
	void ExecuteEvent(UObject* EventOwner, UGameActionStateEvent* OwningState, IMovieScenePlayer& Player) const;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "设置", meta = (DisplayName = "事件颜色"))
	FLinearColor EventColor = FColor(255, 200, 200, 255);

	UPROPERTY(EditDefaultsOnly, Category = "设置", meta = (DisplayName = "支持的类型"))
	TSubclassOf<UGameActionStateEvent> SupportState = nullptr;
#endif

	virtual FString GetEventName() const { return ReceiveGetEventName(); }
#if WITH_EDITOR
	virtual bool IsSupportState(const UClass* StateType) const { return StateType->IsChildOf(SupportState); }
#endif
	UWorld* GetWorld() const override { return WorldContentObject ? WorldContentObject->GetWorld() : nullptr; }
protected:
	virtual void WhenEventExecute(UObject* EventOwner, UGameActionStateEvent* OwningState, IMovieScenePlayer& Player) const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Event")
	void ReceiveWhenEventExecute(UObject* EventOwner, UGameActionStateEvent* OwningState, UGameActionInstanceBase* GameActionInstance) const;
	
	UFUNCTION(BlueprintNativeEvent, Category = "Event", meta = (DisplayName = "Get Event Name"))
	FString ReceiveGetEventName() const;
	
	UPROPERTY(Transient)
	mutable UObject* WorldContentObject = nullptr;
};
