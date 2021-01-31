// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GGJ_Character.generated.h"

UCLASS()
class GGJ_2021_API AGGJ_Character : public ACharacter
{
	GENERATED_BODY()
public:
	AGGJ_Character();
};

UCLASS()
class GGJ_2021_API ASplitSmileBody : public AActor
{
	GENERATED_BODY()
public:
	ASplitSmileBody();
	
	void Tick(float DeltaTime) override;

	UPROPERTY(BlueprintReadOnly)
	FVector Velocity;

	UPROPERTY(BlueprintReadOnly)
	uint8 bCanCombine : 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float BodyScale = 0.f;

	void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};

UCLASS()
class GGJ_2021_API ASmile : public AGGJ_Character
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere)
	TSubclassOf<ASplitSmileBody> SplitBodyClass;
	
	UPROPERTY(EditAnywhere)
	UCurveFloat* SlimeAttractionCurve;

	UPROPERTY(EditAnywhere)
	UCurveFloat* BodyAttractionCurve;
	
	UPROPERTY(EditAnywhere)
	uint8 bDrawDebug : 1;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ASplitSmileBody* SplitBody;

	UPROPERTY(BlueprintReadWrite)
	uint8 bIsInvokeCombine : 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float BodyScale = 1.f;

	void Tick(float DeltaTime) override;
	void TryCombine();
	void NotifyActorBeginOverlap(AActor* OtherActor) override;
	void NotifyActorEndOverlap(AActor* OtherActor) override;

	UFUNCTION(BlueprintCallable)
	void Split(const FVector& Velocity, float LostScale);

	UFUNCTION(BlueprintCallable)
	void Combine();
};
