// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

GAMEACTION_RUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(GameAction_Log, Display, All);

#define GameAction_Log(CategoryName, FMT, ...) UE_LOG(GameAction_Log, CategoryName, TEXT(FMT), ##__VA_ARGS__)
