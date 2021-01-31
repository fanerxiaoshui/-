// Fill out your copyright notice in the Description page of Project Settings.


#include "GGJ_Character.h"
#include <DrawDebugHelpers.h>
#include <GameFramework/CharacterMovementComponent.h>
#include <Components/CapsuleComponent.h>

// Sets default values
AGGJ_Character::AGGJ_Character()
{
	PrimaryActorTick.bCanEverTick = true;
}

ASplitSmileBody::ASplitSmileBody()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASplitSmileBody::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	Velocity.Y = 0.f;
	
	const FVector PreLocation = GetActorLocation();

	const float GravityScale = (BodyScale - 0.2f);
	Velocity += FVector(0.f, 0.f, -4000.f * GravityScale) * DeltaTime;

	FHitResult HitResult;
	AddActorWorldOffset(Velocity * DeltaTime, true, &HitResult);
	if (HitResult.bBlockingHit)
	{
		const FVector BlockLocation = GetActorLocation();
		const FVector VelocityDirection = Velocity.GetSafeNormal();
		const FVector PlaneLocation = FMath::LinePlaneIntersection(BlockLocation + HitResult.ImpactNormal, BlockLocation + VelocityDirection * 100.f, BlockLocation, HitResult.ImpactNormal);
		const FVector AdjustDirection = (PlaneLocation - BlockLocation).GetSafeNormal();

		const FVector AdjustVelocity = AdjustDirection * ((1.f - HitResult.Time) * Velocity.Size()) * (1.f - GravityScale);
		AddActorWorldOffset(AdjustVelocity * DeltaTime, true);

		GetRootComponent()->AttachToComponent(HitResult.Component.Get(), FAttachmentTransformRules::KeepWorldTransform);
	}
	else
	{
		GetRootComponent()->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}

	Velocity = (GetActorLocation() - PreLocation) / DeltaTime;
}

void ASplitSmileBody::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (ASmile* Slime = Cast<ASmile>(GetOwner()))
	{
		Slime->BodyScale += BodyScale;
		Slime->SetActorScale3D(FVector(Slime->BodyScale));

		Slime->GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
	}
}

void ASmile::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (SplitBody)
	{
		const FVector BodyToSmile = GetActorLocation() - SplitBody->GetActorLocation();
		const FVector BodyToSmileDirection = BodyToSmile.GetSafeNormal();
		const FVector SmileToBodySmileDirection = -BodyToSmileDirection;
		UCharacterMovementComponent* MovementComponent = GetCharacterMovement();

		const float AttractionScale = FMath::Clamp(SplitBody->GetGameTimeSinceCreation() * 2.f, 0.f, 1.f);
		const FVector SlimeVelocity = SlimeAttractionCurve->GetFloatValue(BodyScale) * SmileToBodySmileDirection * AttractionScale;

		const FVector BodyVelocity = BodyAttractionCurve->GetFloatValue(SplitBody->BodyScale) * BodyToSmileDirection * AttractionScale;
		
		if (bIsInvokeCombine)
		{
			if (BodyScale < 0.5f)
			{
				MovementComponent->ClearAccumulatedForces();
				MovementComponent->AddImpulse(SlimeVelocity * DeltaTime * 100.f, true);
			}
			else
			{
				SplitBody->Velocity = BodyVelocity * DeltaTime * 100.f;
			}
		}
		else
		{
			MovementComponent->AddImpulse(SlimeVelocity * DeltaTime, true);

			SplitBody->Velocity += BodyVelocity * DeltaTime;
		}
	}

	TArray<AActor*> OverlapBodies;
	GetOverlappingActors(OverlapBodies, ASplitSmileBody::StaticClass());
	for (AActor* OverlapBody : OverlapBodies)
	{
		if (OverlapBody == SplitBody)
		{
			TryCombine();
		}
	}
}

void ASmile::TryCombine()
{
	if (SplitBody->bCanCombine && SplitBody->GetGameTimeSinceCreation() > 0.1f)
	{
		UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
		if (bIsInvokeCombine == false)
		{
			const FVector AdjustForce = SplitBody->Velocity * SplitBody->BodyScale - MovementComponent->Velocity * BodyScale;
			MovementComponent->AddImpulse(AdjustForce);
		}
		else
		{
			bIsInvokeCombine = false;
			MovementComponent->Velocity = FVector::ZeroVector;
		}

		SplitBody->Destroy();
		SplitBody = nullptr;
	}
}

void ASmile::NotifyActorBeginOverlap(AActor* OtherActor)
{
	if (SplitBody == OtherActor)
	{
		TryCombine();
	}
}

void ASmile::NotifyActorEndOverlap(AActor* OtherActor)
{
	if (SplitBody == OtherActor)
	{
		SplitBody->bCanCombine = true;
	}
}

void ASmile::Split(const FVector& Velocity, float LostScale)
{
	if (IsValid(SplitBody) == false)
	{
		BodyScale -= LostScale;
		SetActorScale3D(FVector(BodyScale));

		FActorSpawnParameters ActorSpawnParameters;
		ActorSpawnParameters.Owner = this;
		ActorSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SplitBody = GetWorld()->SpawnActor<ASplitSmileBody>(SplitBodyClass, GetActorLocation() + Velocity.GetSafeNormal() * 50.f, Velocity.Rotation(), ActorSpawnParameters);

		SplitBody->BodyScale = LostScale;
		SplitBody->SetActorScale3D(FVector(SplitBody->BodyScale));
		SplitBody->Velocity = Velocity;
	}
}

void ASmile::Combine()
{
	if (SplitBody && bIsInvokeCombine == false)
	{
		bIsInvokeCombine = true;

		if (BodyScale > 0.5f)
		{
			const FVector BodyToSmile = GetActorLocation() - SplitBody->GetActorLocation();
			const FVector BodyToSmileDirection = BodyToSmile.GetSafeNormal();

			SplitBody->Velocity = BodyToSmileDirection * 2000.f;
			if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(SplitBody->GetRootComponent()))
			{
				Root->SetCollisionProfileName(TEXT("SplitBodyCombine"));
			}
		}
		else
		{
			const FVector SmileToBody = SplitBody->GetActorLocation() - GetActorLocation();
			const FVector SmileToBodySmileDirection = SmileToBody.GetSafeNormal();

			UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
			MovementComponent->AddImpulse(SmileToBodySmileDirection * 2000.f, true);
			GetCapsuleComponent()->SetCollisionProfileName(TEXT("SmileCombine"));
		}
	}
}
