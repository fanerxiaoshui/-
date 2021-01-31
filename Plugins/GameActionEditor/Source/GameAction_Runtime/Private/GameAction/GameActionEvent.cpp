// Fill out your copyright notice in the Description page of Project Settings.


#include "GameAction/GameActionEvent.h"
#include <IMovieScenePlayer.h>
#include <GameFramework/Actor.h>
#include <Engine/World.h>

#include "GameAction/GameActionInstance.h"
#include "Utils/GameAction_Log.h"

UGameActionEventBase::UGameActionEventBase()
{
#if WITH_EDITORONLY_DATA
	bExecuteInEditor = false;
	SupportClass = AActor::StaticClass();
	SupportGameInstance = UGameActionInstanceBase::StaticClass();
#endif
}

#if WITH_EDITOR
bool UGameActionEventBase::CanExecute() const
{
	return bExecuteInEditor || GetWorld()->IsGameWorld();
}

bool UGameActionEventBase::IsSupportClass(const UClass* Class, const TSubclassOf<UGameActionInstanceBase>& ParentInstanceClass) const
{
	return Class->IsChildOf(SupportClass) && ParentInstanceClass->IsChildOf(SupportGameInstance);
}
#endif

FString UGameActionEventBase::ReceiveGetEventName_Implementation() const
{
#if WITH_EDITOR
	return GetClass()->GetDisplayNameText().ToString();
#endif
	return GetName();
}

void UGameActionKeyEvent::ExecuteEvent(UObject* EventOwner, IMovieScenePlayer& Player) const
{
	TGuardValue<UObject*> WorldContentObjectGuard(WorldContentObject, EventOwner);
#if WITH_EDITOR
	if (CanExecute() == false)
	{
		return;
	}
	FEditorScriptExecutionGuard EditorScriptExecutionGuard;
#endif
	GameAction_Log(Display, "[%s] 执行帧事件 [%s]", *EventOwner->GetName(), *GetEventName());
	WhenEventExecute(EventOwner, Player);
}

void UGameActionKeyEvent::WhenEventExecute(UObject* EventOwner, IMovieScenePlayer& Player) const
{
	ReceiveWhenEventExecute(EventOwner, Cast<UGameActionInstanceBase>(Player.GetPlaybackContext()));
}

void UGameActionStateEvent::StartEvent(UObject* EventOwner, IMovieScenePlayer& Player)
{
	TGuardValue<UObject*> WorldContentObjectGuard(WorldContentObject, EventOwner);
#if WITH_EDITOR
	if (CanExecute() == false)
	{
		return;
	}
	FEditorScriptExecutionGuard EditorScriptExecutionGuard;
#endif
	GameAction_Log(Display, "[%s] 开始状态事件 [%s]", *EventOwner->GetName(), *GetEventName());
	WhenEventStart(EventOwner, Player);
}

void UGameActionStateEvent::TickEvent(UObject* EventOwner, IMovieScenePlayer& Player, float DeltaSeconds)
{
	TGuardValue<UObject*> WorldContentObjectGuard(WorldContentObject, EventOwner);
#if WITH_EDITOR
	if (CanExecute() == false)
	{
		return;
	}
	FEditorScriptExecutionGuard EditorScriptExecutionGuard;
#endif
	WhenEventTick(EventOwner, Player, DeltaSeconds);
}

void UGameActionStateEvent::EndEvent(UObject* EventOwner, IMovieScenePlayer& Player, bool bIsCompleted)
{
	TGuardValue<UObject*> WorldContentObjectGuard(WorldContentObject, EventOwner);
#if WITH_EDITOR
	if (CanExecute() == false)
	{
		return;
	}
	FEditorScriptExecutionGuard EditorScriptExecutionGuard;
#endif
	GameAction_Log(Display, "[%s] 结束状态事件 [%s]", *EventOwner->GetName(), *GetEventName());
	WhenEventEnd(EventOwner, Player, bIsCompleted);
}

void UGameActionStateEvent::WhenEventStart(UObject* EventOwner, IMovieScenePlayer& Player)
{
	ReceiveWhenEventStart(EventOwner, Cast<UGameActionInstanceBase>(Player.GetPlaybackContext()));
}

void UGameActionStateEvent::WhenEventTick(UObject* EventOwner, IMovieScenePlayer& Player, float DeltaSeconds)
{
	ReceiveWhenEventTick(EventOwner, Cast<UGameActionInstanceBase>(Player.GetPlaybackContext()), DeltaSeconds);
}

void UGameActionStateEvent::WhenEventEnd(UObject* EventOwner, IMovieScenePlayer& Player, bool bIsCompleted)
{
	ReceiveWhenEventEnd(EventOwner, Cast<UGameActionInstanceBase>(Player.GetPlaybackContext()), bIsCompleted);
}

void UGameActionStateInnerKeyEvent::ExecuteEvent(UObject* EventOwner, UGameActionStateEvent* OwningState, IMovieScenePlayer& Player) const
{
	TGuardValue<UObject*> WorldContentObjectGuard(WorldContentObject, EventOwner);
	TGuardValue<UObject*> OwningStateWorldContentObjectGuard(OwningState->WorldContentObject, EventOwner);
#if WITH_EDITOR
	if (OwningState->CanExecute() == false)
	{
		return;
	}
	FEditorScriptExecutionGuard EditorScriptExecutionGuard;
#endif
	GameAction_Log(Display, "[%s] 执行状态内部帧事件 [%s]", *OwningState->GetName(), *GetEventName());
	WhenEventExecute(EventOwner, OwningState, Player);
}

void UGameActionStateInnerKeyEvent::WhenEventExecute(UObject* EventOwner, UGameActionStateEvent* OwningState, IMovieScenePlayer& Player) const
{
	ReceiveWhenEventExecute(EventOwner, OwningState, Cast<UGameActionInstanceBase>(Player.GetPlaybackContext()));
}

FString UGameActionStateInnerKeyEvent::ReceiveGetEventName_Implementation() const
{
#if WITH_EDITOR
	return GetClass()->GetDisplayNameText().ToString();
#endif
	return GetName();
}
