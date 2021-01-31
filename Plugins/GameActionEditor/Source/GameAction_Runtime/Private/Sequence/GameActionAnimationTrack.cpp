// Fill out your copyright notice in the Description page of Project Settings.


#include "Sequence/GameActionAnimationTrack.h"
#include <Compilation/MovieSceneCompilerRules.h>
#include <Channels/MovieSceneChannelProxy.h>
#include <Animation/AnimInstance.h>

#include "GameAction/GameActionInstance.h"
#include "Sequence/GameActionSequencePlayer.h"

#define LOCTEXT_NAMESPACE "GameActionAnimationTrack"

namespace GameAction
{
	struct FMinimalAnimParameters
	{
		FMinimalAnimParameters(UAnimMontage* Montage, float InFromEvalTime, float InToEvalTime, float InBlendWeight, FFrameTime FrameTime, const FMovieSceneByteChannel& TearDownStrategyChannel, const FMovieSceneEvaluationScope& InScope, FObjectKey InSection)
			: Montage(Montage)
			, FromEvalTime(InFromEvalTime)
			, ToEvalTime(InToEvalTime)
			, BlendWeight(InBlendWeight)
			, FrameTime(FrameTime)
			, TearDownStrategyChannel(TearDownStrategyChannel)
			, EvaluationScope(InScope)
			, Section(InSection)
		{}

		UAnimMontage* Montage;
		float FromEvalTime;
		float ToEvalTime;
		float BlendWeight;
		FFrameTime FrameTime;
		const FMovieSceneByteChannel& TearDownStrategyChannel;
		FMovieSceneEvaluationScope EvaluationScope;
		FObjectKey Section;
	};
	struct FSimulatedAnimParameters
	{
		FMinimalAnimParameters AnimParams;
	};

	/** Montage player per section data */
	struct FMontagePlayerPerSectionData
	{
		TWeakObjectPtr<UAnimMontage> Montage = nullptr;
		int32 MontageInstanceId = INDEX_NONE;
		FAlphaBlend BlendIn;
	};

	struct FBlendedAnimation
	{
		TArray<FMinimalAnimParameters> SimulatedAnimations;
		TArray<FMinimalAnimParameters> AllAnimations;

		FBlendedAnimation& Resolve(TMovieSceneInitialValueStore<FBlendedAnimation>& InitialValueStore)
		{
			return *this;
		}
	};

	void BlendValue(FBlendedAnimation& OutBlend, const FMinimalAnimParameters& InValue, float Weight, EMovieSceneBlendType BlendType, TMovieSceneInitialValueStore<FBlendedAnimation>& InitialValueStore)
	{
		OutBlend.AllAnimations.Add(InValue);
	}
	void BlendValue(FBlendedAnimation& OutBlend, const FSimulatedAnimParameters& InValue, float Weight, EMovieSceneBlendType BlendType, TMovieSceneInitialValueStore<FBlendedAnimation>& InitialValueStore)
	{
		OutBlend.SimulatedAnimations.Add(InValue.AnimParams);
	}

	struct FGameActionAnimationActuator : TMovieSceneBlendingActuator<FBlendedAnimation>
	{
		FGameActionAnimationActuator() : TMovieSceneBlendingActuator<FBlendedAnimation>(GetActuatorTypeID()) {}

		static FMovieSceneBlendingActuatorID GetActuatorTypeID()
		{
			static FMovieSceneAnimTypeID TypeID = TMovieSceneAnimTypeID<FGameActionAnimationActuator, 0>();
			return FMovieSceneBlendingActuatorID(TypeID);
		}

		static FMovieSceneAnimTypeID GetAnimControlTypeID()
		{
			static FMovieSceneAnimTypeID TypeID = TMovieSceneAnimTypeID<FGameActionAnimationActuator, 2>();
			return TypeID;
		}

		FBlendedAnimation RetrieveCurrentValue(UObject* InObject, IMovieScenePlayer* Player) const override
		{
			check(false);
			return FBlendedAnimation();
		}

		void Actuate(UObject* InObject, const FBlendedAnimation& InFinalValue, const TBlendableTokenStack<FBlendedAnimation>& OriginalStack, const FMovieSceneContext& Context, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
		{
			ensureMsgf(InObject, TEXT("Attempting to evaluate an Animation track with a null object."));

			USkeletalMeshComponent* SkeletalMeshComponent = SkeletalMeshComponentFromObject(InObject);
			if (!SkeletalMeshComponent)
			{
				return;
			}
			UAnimInstance* SequencerInstance = SkeletalMeshComponent->GetAnimInstance();

			const EMovieScenePlayerStatus::Type PlayerStatus = Player.GetPlaybackStatus();

			// When jumping from one cut to another cut, the delta time should be 0 so that anim notifies before the current position are not evaluated. Note, anim notifies at the current time should still be evaluated.
			const double DeltaTime = (Context.HasJumped() ? FFrameTime(0) : Context.GetRange().Size<FFrameTime>()) / Context.GetFrameRate();

			const bool bResetDynamics = PlayerStatus == EMovieScenePlayerStatus::Stepping ||
				PlayerStatus == EMovieScenePlayerStatus::Jumping ||
				PlayerStatus == EMovieScenePlayerStatus::Scrubbing ||
				(DeltaTime == 0.0f && PlayerStatus != EMovieScenePlayerStatus::Stopped);

			//Need to zero all weights first since we may be blending animation that are keeping state but are no longer active.

			if (SequencerInstance)
			{
				for (const TPair<FObjectKey, FMontagePlayerPerSectionData >& Pair : MontageData)
				{
					const int32 InstanceId = Pair.Value.MontageInstanceId;
					FAnimMontageInstance* MontageInstanceToUpdate = SequencerInstance->GetMontageInstanceForID(InstanceId);
					if (MontageInstanceToUpdate)
					{
						MontageInstanceToUpdate->SetDesiredWeight(0.0f);
						MontageInstanceToUpdate->SetWeight(0.0f);
					}
				}
			}

			if (InFinalValue.SimulatedAnimations.Num() != 0 && Player.MotionVectorSimulation.IsValid())
			{
				ApplyAnimations(PersistentData, Player, SkeletalMeshComponent, InFinalValue.SimulatedAnimations, DeltaTime, bResetDynamics);

				SkeletalMeshComponent->TickAnimation(0.f, false);
				SkeletalMeshComponent->RefreshBoneTransforms();
				SkeletalMeshComponent->FinalizeBoneTransform();
				SkeletalMeshComponent->ForceMotionVector();

				SimulateMotionVectors(PersistentData, SkeletalMeshComponent, Player);
			}

			ApplyAnimations(PersistentData, Player, SkeletalMeshComponent, InFinalValue.AllAnimations, DeltaTime, bResetDynamics);

			// If the skeletal component has already ticked this frame because tick prerequisites weren't set up yet or a new binding was created, forcibly tick this component to update.
			// This resolves first frame issues where the skeletal component ticks first, then the sequencer binding is resolved which sets up tick prerequisites
			// for the next frame.
			//if (SkeletalMeshComponent->PoseTickedThisFrame() || (SequencerInstance && SequencerInstance->GetSourceAnimInstance() != ExistingAnimInstance))
			//{
			//	SkeletalMeshComponent->TickAnimation(0.f, false);

			//	SkeletalMeshComponent->RefreshBoneTransforms();
			//	SkeletalMeshComponent->RefreshSlaveComponents();
			//	SkeletalMeshComponent->UpdateComponentToWorld();
			//	SkeletalMeshComponent->FinalizeBoneTransform();
			//	SkeletalMeshComponent->MarkRenderTransformDirty();
			//	SkeletalMeshComponent->MarkRenderDynamicDataDirty();
			//}

			// 子帧模拟处理
			// TODO：启用了子帧模拟的组件不能兼容AnimationBudget，因为现在用了AnimationBudget的接口，应该定制SkeletalMeshComponent并屏蔽掉AnimationBudget输入，使系统能兼容AnimationBudget
			if (UGameActionInstanceBase* GameActionInstance = Cast<UGameActionInstanceBase>(Player.GetPlaybackContext()))
			{
				UGameActionSequencePlayer* SequencePlayer = GameActionInstance->SequencePlayer;
				if (SequencePlayer->bIsInSubStepState)
				{
					SkeletalMeshComponent->EnableExternalTickRateControl(false);
					SkeletalMeshComponent->TickAnimation(DeltaTime, false);
					SkeletalMeshComponent->RefreshBoneTransforms();
					SkeletalMeshComponent->EnableExternalTickRateControl(true);

					struct FDisableExternalTickRateControlProducer : IMovieScenePreAnimatedTokenProducer
					{
						TWeakObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;
						
						FDisableExternalTickRateControlProducer(USkeletalMeshComponent* SkeletalMeshComponent)
							: SkeletalMeshComponent(SkeletalMeshComponent)
						{}

						IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const override
						{
							struct FToken : IMovieScenePreAnimatedToken
							{
								TWeakObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;
								
								FToken(const TWeakObjectPtr<USkeletalMeshComponent>& SkeletalMeshComponent)
									: SkeletalMeshComponent(SkeletalMeshComponent)
								{}

								void RestoreState(UObject& ObjectToRestore, IMovieScenePlayer& Player) override
								{
									if (SkeletalMeshComponent.IsValid())
									{
										SkeletalMeshComponent->EnableExternalTickRateControl(false);
									}
								}
							};

							return FToken(SkeletalMeshComponent);
						}
					};
					OriginalStack.SavePreAnimatedState(Player, *SkeletalMeshComponent, GetAnimControlTypeID(), FDisableExternalTickRateControlProducer(SkeletalMeshComponent));
				}
				else
				{
					SkeletalMeshComponent->EnableExternalTickRateControl(false);
				}
			}

			Player.PreAnimatedState.SetCaptureEntity(FMovieSceneEvaluationKey(), EMovieSceneCompletionMode::KeepState);
		}

	private:

		static USkeletalMeshComponent* SkeletalMeshComponentFromObject(UObject* InObject)
		{
			USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(InObject);
			if (SkeletalMeshComponent)
			{
				return SkeletalMeshComponent;
			}

			// then check to see if we are controlling an actor & if so use its first USkeletalMeshComponent 
			AActor* Actor = Cast<AActor>(InObject);

			if (!Actor)
			{
				if (UChildActorComponent* ChildActorComponent = Cast<UChildActorComponent>(InObject))
				{
					Actor = ChildActorComponent->GetChildActor();
				}
			}

			if (Actor)
			{
				return Actor->FindComponentByClass<USkeletalMeshComponent>();
			}
			return nullptr;
		}

		static void SimulateMotionVectors(FPersistentEvaluationData& PersistentData, USkeletalMeshComponent* SkeletalMeshComponent, IMovieScenePlayer& Player)
		{
			for (USceneComponent* Child : SkeletalMeshComponent->GetAttachChildren())
			{
				FName SocketName = Child->GetAttachSocketName();
				if (SocketName != NAME_None)
				{
					FTransform SocketTransform = SkeletalMeshComponent->GetSocketTransform(SocketName, RTS_Component);
					Player.MotionVectorSimulation->Add(SkeletalMeshComponent, SocketTransform, SocketName);
				}
			}
		}

		void ApplyAnimations(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player, USkeletalMeshComponent* SkeletalMeshComponent, TArrayView<const FMinimalAnimParameters> Parameters, float DeltaTime, bool bResetDynamics)
		{
			const EMovieScenePlayerStatus::Type PlayerStatus = Player.GetPlaybackStatus();

			for (const FMinimalAnimParameters& AnimParams : Parameters)
			{
				Player.PreAnimatedState.SetCaptureEntity(AnimParams.EvaluationScope.Key, AnimParams.EvaluationScope.CompletionMode);

				const float AssetPlayRate = FMath::IsNearlyZero(AnimParams.Montage->RateScale) ? 1.0f : AnimParams.Montage->RateScale;

				SetAnimPosition(PersistentData, Player, SkeletalMeshComponent,
					AnimParams, AnimParams.FromEvalTime / AssetPlayRate, AnimParams.ToEvalTime / AssetPlayRate,
					PlayerStatus == EMovieScenePlayerStatus::Playing
				);
			}
		}

		void SetAnimPosition(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player, USkeletalMeshComponent* SkeletalMeshComponent, const FMinimalAnimParameters& AnimParams, float InFromPosition, float InToPosition, bool bPlaying)
		{
			const FObjectKey Section = AnimParams.Section;
			UAnimMontage* InAnimMontage = AnimParams.Montage;
			const float Weight = AnimParams.BlendWeight;
			if (UAnimInstance* AnimInst = SkeletalMeshComponent->GetAnimInstance())
			{
				FMontagePlayerPerSectionData& DataContainer = MontageData.FindOrAdd(Section);

				FAnimMontageInstance* MontageInstanceToUpdate = AnimInst->GetMontageInstanceForID(DataContainer.MontageInstanceId);
				// 正在BlendOut的实例不能复用
				if (MontageInstanceToUpdate == nullptr || MontageInstanceToUpdate->bEnableAutoBlendOut == true)
				{
					AnimInst->Montage_Play(InAnimMontage, 1.f, EMontagePlayReturnType::MontageLength, InFromPosition, true);
					MontageInstanceToUpdate = AnimInst->GetActiveInstanceForMontage(InAnimMontage);
					MontageInstanceToUpdate->bEnableAutoBlendOut = false;
					DataContainer.BlendIn = InAnimMontage->BlendIn;
					DataContainer.Montage = InAnimMontage;
					DataContainer.MontageInstanceId = MontageInstanceToUpdate->GetInstanceID();
				}
				check(MontageInstanceToUpdate);
				
				// 更新混入的权重
				if (DataContainer.BlendIn.IsComplete() == false)
				{
					const float DeltaTime = InToPosition - InFromPosition;
					if (DeltaTime > 0.f)
					{
						DataContainer.BlendIn.Update(DeltaTime);
					}
					MontageInstanceToUpdate->SetDesiredWeight(Weight* DataContainer.BlendIn.GetDesiredValue());
					MontageInstanceToUpdate->SetWeight(Weight* DataContainer.BlendIn.GetAlpha());
				}
				else
				{
					MontageInstanceToUpdate->SetDesiredWeight(Weight);
					MontageInstanceToUpdate->SetWeight(Weight);
				}

				// TODO:处理Sequence播放结束时混出
				
				if (bPlaying)
				{
					MontageInstanceToUpdate->SetNextPositionWithEvents(InFromPosition, InToPosition);
				}
				else
				{
					MontageInstanceToUpdate->SetPosition(InToPosition);
				}

				MontageInstanceToUpdate->bPlaying = bPlaying;
				
				const FMovieSceneAnimTypeID SlotTypeID = SectionToAnimationIDs.GetAnimTypeID(Section);

				struct FStopPlayingMontageTokenData
				{
					FStopPlayingMontageTokenData(const TWeakObjectPtr<UAnimInstance>& InTempInstance, const TWeakObjectPtr<UAnimMontage>& InTempMontage, int32 InTempMontageInstanceId, FFrameTime FrameTime, const FMovieSceneByteChannel& TearDownStrategyChannel)
						: WeakInstance(InTempInstance)
						, WeakMontage(InTempMontage)
						, MontageInstanceId(InTempMontageInstanceId)
						, FrameTime(FrameTime)
						, TearDownStrategyChannel(TearDownStrategyChannel)
					{}
					TWeakObjectPtr<UAnimInstance> WeakInstance;
					TWeakObjectPtr<UAnimMontage> WeakMontage;
					int32 MontageInstanceId;
					FFrameTime FrameTime;
#if WITH_EDITOR
					const FMovieSceneByteChannel TearDownStrategyChannel;
#else
					const FMovieSceneByteChannel& TearDownStrategyChannel;
#endif
				};
				struct FStopPlayingMontageTokenProducer : IMovieScenePreAnimatedTokenProducer, FStopPlayingMontageTokenData
				{
					FStopPlayingMontageTokenProducer(UAnimInstance* InTempInstance, UAnimMontage* InTempMontage, int32 InTempMontageInstanceId, FFrameTime FrameTime, const FMovieSceneByteChannel& TearDownStrategyChannel)
						: FStopPlayingMontageTokenData(InTempInstance, InTempMontage, InTempMontageInstanceId, FrameTime, TearDownStrategyChannel)
					{}

					IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const override
					{
						struct FToken : IMovieScenePreAnimatedToken, FStopPlayingMontageTokenData
						{
							FToken(const FStopPlayingMontageTokenData& TokenData)
								: FStopPlayingMontageTokenData(TokenData)
							{}

							void RestoreState(UObject& ObjectToRestore, IMovieScenePlayer& Player) override
							{
								UAnimInstance* AnimInstance = WeakInstance.Get();
								UAnimMontage* Montage = WeakMontage.Get();
								if (AnimInstance && Montage)
								{
									FAnimMontageInstance* MontageInstance = AnimInstance->GetMontageInstanceForID(MontageInstanceId);
									if (MontageInstance)
									{
										MontageInstance->bPlaying = true;
										
										uint8 TearDownStrategy;
										TearDownStrategyChannel.Evaluate(FrameTime, TearDownStrategy);
										switch (EGameActionAnimationTearDownStrategy(TearDownStrategy))
										{
										case EGameActionAnimationTearDownStrategy::Stop:
											MontageInstance->bEnableAutoBlendOut = true;
											MontageInstance->Stop(Montage->BlendOut, false);
										break;
										case EGameActionAnimationTearDownStrategy::PlayToEnd:
											MontageInstance->bEnableAutoBlendOut = Montage->bEnableAutoBlendOut;
										break;
										default:
											checkNoEntry();
										}
									}
								}
							}
						};

						return FToken(*this);
					}
				};
				Player.SavePreAnimatedState(*InAnimMontage, SlotTypeID, FStopPlayingMontageTokenProducer(AnimInst, InAnimMontage, DataContainer.MontageInstanceId, AnimParams.FrameTime, AnimParams.TearDownStrategyChannel));
			}
		}

		TMovieSceneAnimTypeIDContainer<FObjectKey> SectionToAnimationIDs;
		TMap<FObjectKey, FMontagePlayerPerSectionData> MontageData;
	};

}

template<> FMovieSceneAnimTypeID GetBlendingDataType<GameAction::FBlendedAnimation>()
{
	static FMovieSceneAnimTypeID TypeID = FMovieSceneAnimTypeID::Unique();
	return TypeID;
}

FGameActionAnimationParams::FGameActionAnimationParams()
{
	Weight.SetDefault(1.f);

	TearDownStrategy.SetEnum(StaticEnum<EGameActionAnimationTearDownStrategy>());
	TearDownStrategy.SetDefault((uint8)EGameActionAnimationTearDownStrategy::Stop);
}

float FGameActionAnimationParams::GetDuration() const
{
	return FMath::IsNearlyZero(PlayRate) || Montage == nullptr ? 0.f : Montage->SequenceLength / PlayRate;
}

float FGameActionAnimationParams::GetSequenceLength() const
{
	return Montage != nullptr ? Montage->SequenceLength : 0.f;
}

float FGameActionAnimationSectionTemplateParameters::MapTimeToAnimation(FFrameTime InPosition, FFrameRate InFrameRate) const
{
	const FFrameTime AnimationLength = GetSequenceLength() * InFrameRate;
	const int32 LengthInFrames = AnimationLength.FrameNumber.Value + static_cast<int>(AnimationLength.GetSubFrame() + 0.5f) + 1;
	//we only play end if we are not looping, and assuming we are looping if Length is greater than default length;
	const bool bLooping = (SectionEndTime.Value - SectionStartTime.Value + StartFrameOffset + EndFrameOffset) > LengthInFrames;

	InPosition = FMath::Clamp(InPosition, FFrameTime(SectionStartTime), FFrameTime(SectionEndTime - 1));

	const float SectionPlayRate = PlayRate * Montage->RateScale;
	const float AnimPlayRate = FMath::IsNearlyZero(SectionPlayRate) ? 1.0f : SectionPlayRate;

	const float FirstLoopSeqLength = GetSequenceLength() - InFrameRate.AsSeconds(FirstLoopStartFrameOffset + StartFrameOffset + EndFrameOffset);
	const float SeqLength = GetSequenceLength() - InFrameRate.AsSeconds(StartFrameOffset + EndFrameOffset);

	float AnimPosition = FFrameTime::FromDecimal((InPosition - SectionStartTime).AsDecimal() * AnimPlayRate) / InFrameRate;
	AnimPosition += InFrameRate.AsSeconds(FirstLoopStartFrameOffset);
	if (SeqLength > 0.f && (bLooping || !FMath::IsNearlyEqual(AnimPosition, SeqLength, 1e-4f)))
	{
		AnimPosition = FMath::Fmod(AnimPosition, SeqLength);
	}
	AnimPosition += InFrameRate.AsSeconds(StartFrameOffset);

	return AnimPosition;
}

FGameActionAnimationSectionTemplate::FGameActionAnimationSectionTemplate(const UGameActionAnimationSection& InSection)
	: Params(InSection.Params, InSection.GetInclusiveStartFrame(), InSection.GetExclusiveEndFrame())
{
}

void FGameActionAnimationSectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	if (Params.Montage)
	{
		const FOptionalMovieSceneBlendType BlendType = GetSourceSection()->GetBlendType();
		check(BlendType.IsValid());

		// Ensure the accumulator knows how to actually apply component transforms
		const FMovieSceneBlendingActuatorID ActuatorTypeID = GameAction::FGameActionAnimationActuator::GetActuatorTypeID();
		FMovieSceneBlendingAccumulator& Accumulator = ExecutionTokens.GetBlendingAccumulator();
		if (!Accumulator.FindActuator<GameAction::FBlendedAnimation>(ActuatorTypeID))
		{
			Accumulator.DefineActuator(ActuatorTypeID, MakeShared<GameAction::FGameActionAnimationActuator>());
		}

		// Calculate the time at which to evaluate the animation
		const float EvalTime = Params.MapTimeToAnimation(Context.GetTime(), Context.GetFrameRate());
		const float PreviousEvalTime = Params.MapTimeToAnimation(Context.GetPreviousTime(), Context.GetFrameRate());

		float ManualWeight = 1.f;
		Params.Weight.Evaluate(Context.GetTime(), ManualWeight);
		const float Weight = ManualWeight * EvaluateEasing(Context.GetTime());

		// Add the blendable to the accumulator
		GameAction::FMinimalAnimParameters AnimParams(
			Params.Montage, PreviousEvalTime, EvalTime, Weight, Context.GetTime(), Params.TearDownStrategy, ExecutionTokens.GetCurrentScope(), GetSourceSection()
		);
		ExecutionTokens.BlendToken(ActuatorTypeID, TBlendableToken<GameAction::FBlendedAnimation>(AnimParams, BlendType.Get(), 1.f));

		if (IMovieSceneMotionVectorSimulation::IsEnabled(PersistentData, Context))
		{
			const FFrameTime SimulatedTime = IMovieSceneMotionVectorSimulation::GetSimulationTime(Context);

			// Calculate the time at which to evaluate the animation
			const float CurrentEvalTime = Params.MapTimeToAnimation(Context.GetTime(), Context.GetFrameRate());
			const float SimulatedEvalTime = Params.MapTimeToAnimation(SimulatedTime, Context.GetFrameRate());

			float SimulatedManualWeight = 1.f;
			Params.Weight.Evaluate(SimulatedTime, SimulatedManualWeight);

			const float SimulatedWeight = SimulatedManualWeight * EvaluateEasing(SimulatedTime);

			GameAction::FSimulatedAnimParameters SimulatedAnimParams{ AnimParams };
			SimulatedAnimParams.AnimParams.FromEvalTime = CurrentEvalTime;
			SimulatedAnimParams.AnimParams.ToEvalTime = SimulatedEvalTime;
			SimulatedAnimParams.AnimParams.BlendWeight = SimulatedWeight;
			ExecutionTokens.BlendToken(ActuatorTypeID, TBlendableToken<GameAction::FBlendedAnimation>(SimulatedAnimParams, BlendType.Get(), 1.f));
		}
	}
}

UGameActionAnimationSection::UGameActionAnimationSection()
{
	BlendType = EMovieSceneBlendType::Absolute;

	EvalOptions.bCanEditCompletionMode = true;
	EvalOptions.CompletionMode = EMovieSceneCompletionMode::ProjectDefault;

	FMovieSceneChannelProxyData Channels;
#if WITH_EDITOR
	static FMovieSceneChannelMetaData WeightMetaData(TEXT("Weight"), LOCTEXT("WeightChannelName", "Weight"));
	WeightMetaData.bCanCollapseToTrack = false;
	Channels.Add(Params.Weight, WeightMetaData, TMovieSceneExternalValue<float>());

	static FMovieSceneChannelMetaData TearDownStrategyMetaData(TEXT("TearDownStrategy"), LOCTEXT("TearDownStrategyName", "序列结束时行为"));
	TearDownStrategyMetaData.bCanCollapseToTrack = false;
	Channels.Add(Params.TearDownStrategy, TearDownStrategyMetaData, TMovieSceneExternalValue<uint8>::Make());
#else
	Channels.Add(Params.Weight);
	Channels.Add(Params.TearDownStrategy);
#endif
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(Channels));
}

float UGameActionAnimationSection::MapTimeToAnimation(FFrameTime InPosition, FFrameRate InFrameRate) const
{
	const FGameActionAnimationSectionTemplateParameters TemplateParams(Params, GetInclusiveStartFrame(), GetExclusiveEndFrame());
	return TemplateParams.MapTimeToAnimation(InPosition, InFrameRate);
}

TOptional<TRange<FFrameNumber> > UGameActionAnimationSection::GetAutoSizeRange() const
{
	const FFrameRate FrameRate = GetTypedOuter<UMovieScene>()->GetTickResolution();

	const FFrameTime AnimationLength = Params.GetSequenceLength() * FrameRate;
	const int32 IFrameNumber = AnimationLength.FrameNumber.Value + static_cast<int>(AnimationLength.GetSubFrame() + 0.5f);

	return TRange<FFrameNumber>(GetInclusiveStartFrame(), GetInclusiveStartFrame() + IFrameNumber + 1);
}

FFrameNumber GetFirstLoopStartOffsetAtTrimTime(FQualifiedFrameTime TrimTime, const FGameActionAnimationParams& Params, FFrameNumber StartFrame, FFrameRate FrameRate)
{
	const float AnimPlayRate = FMath::IsNearlyZero(Params.PlayRate) ? 1.0f : Params.PlayRate;
	const float AnimPosition = (TrimTime.Time - StartFrame) / TrimTime.Rate * AnimPlayRate;
	const float SeqLength = Params.GetSequenceLength() - FrameRate.AsSeconds(Params.StartFrameOffset + Params.EndFrameOffset) / AnimPlayRate;

	FFrameNumber NewOffset = FrameRate.AsFrameNumber(FMath::Fmod(AnimPosition, SeqLength));
	NewOffset += Params.FirstLoopStartFrameOffset;

	const FFrameNumber SeqLengthInFrames = FrameRate.AsFrameNumber(SeqLength);
	while (NewOffset >= SeqLengthInFrames)
		NewOffset -= SeqLengthInFrames;

	return NewOffset;
}

void UGameActionAnimationSection::TrimSection(FQualifiedFrameTime TrimTime, bool bTrimLeft, bool bDeleteKeys)
{
	SetFlags(RF_Transactional);

	if (TryModify())
	{
		if (bTrimLeft)
		{
			const FFrameRate FrameRate = GetTypedOuter<UMovieScene>()->GetTickResolution();

			Params.FirstLoopStartFrameOffset = HasStartFrame() ? GetFirstLoopStartOffsetAtTrimTime(TrimTime, Params, GetInclusiveStartFrame(), FrameRate) : 0;
		}

		Super::TrimSection(TrimTime, bTrimLeft, bDeleteKeys);
	}
}

UMovieSceneSection* UGameActionAnimationSection::SplitSection(FQualifiedFrameTime SplitTime, bool bDeleteKeys)
{
	const FFrameNumber InitialFirstLoopStartFrameOffset = Params.FirstLoopStartFrameOffset;

	const FFrameRate FrameRate = GetTypedOuter<UMovieScene>()->GetTickResolution();

	const FFrameNumber NewOffset = HasStartFrame() ? GetFirstLoopStartOffsetAtTrimTime(SplitTime, Params, GetInclusiveStartFrame(), FrameRate) : 0;

	UMovieSceneSection* NewSection = Super::SplitSection(SplitTime, bDeleteKeys);
	if (NewSection != nullptr)
	{
		UGameActionAnimationSection* NewAnimationSection = Cast<UGameActionAnimationSection>(NewSection);
		NewAnimationSection->Params.FirstLoopStartFrameOffset = NewOffset;
	}

	// Restore original offset modified by splitting
	Params.FirstLoopStartFrameOffset = InitialFirstLoopStartFrameOffset;

	return NewSection;
}

void UGameActionAnimationSection::GetSnapTimes(TArray<FFrameNumber>& OutSnapTimes, bool bGetSectionBorders) const
{
	Super::GetSnapTimes(OutSnapTimes, bGetSectionBorders);

	const FFrameRate   FrameRate = GetTypedOuter<UMovieScene>()->GetTickResolution();
	const FFrameNumber StartFrame = GetInclusiveStartFrame();
	const FFrameNumber EndFrame = GetExclusiveEndFrame() - 1; // -1 because we don't need to add the end frame twice

	const float AnimPlayRate = FMath::IsNearlyZero(Params.PlayRate) || Params.Montage == nullptr ? 1.0f : Params.PlayRate * Params.Montage->RateScale;
	const float SeqLengthSeconds = Params.GetSequenceLength() - FrameRate.AsSeconds(Params.StartFrameOffset + Params.EndFrameOffset) / AnimPlayRate;
	const float FirstLoopSeqLengthSeconds = SeqLengthSeconds - FrameRate.AsSeconds(Params.FirstLoopStartFrameOffset) / AnimPlayRate;

	const FFrameTime SequenceFrameLength = SeqLengthSeconds * FrameRate;
	const FFrameTime FirstLoopSequenceFrameLength = FirstLoopSeqLengthSeconds * FrameRate;
	if (SequenceFrameLength.FrameNumber > 1)
	{
		// Snap to the repeat times
		bool IsFirstLoop = true;
		FFrameTime CurrentTime = StartFrame;
		while (CurrentTime < EndFrame)
		{
			OutSnapTimes.Add(CurrentTime.FrameNumber);
			if (IsFirstLoop)
			{
				CurrentTime += FirstLoopSequenceFrameLength;
				IsFirstLoop = false;
			}
			else
			{
				CurrentTime += SequenceFrameLength;
			}
		}
	}
}

TOptional<FFrameTime> UGameActionAnimationSection::GetOffsetTime() const
{
	return TOptional<FFrameTime>(Params.FirstLoopStartFrameOffset);
}

float UGameActionAnimationSection::GetTotalWeightValue(FFrameTime InTime) const
{
	float ManualWeight = 1.f;
	Params.Weight.Evaluate(InTime, ManualWeight);
	return ManualWeight * EvaluateEasing(InTime);
}

UGameActionAnimationTrack::UGameActionAnimationTrack()
{
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(100, 100, 255);
#endif
}

#if WITH_EDITORONLY_DATA
FText UGameActionAnimationTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("Game Action Animation", "游戏行为动画");
}
#endif

bool UGameActionAnimationTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UGameActionAnimationSection::StaticClass();
}

UMovieSceneSection* UGameActionAnimationTrack::CreateNewSection()
{
	return NewObject<UGameActionAnimationSection>(this, NAME_None, RF_Transactional);
}

FMovieSceneTrackRowSegmentBlenderPtr UGameActionAnimationTrack::GetRowSegmentBlender() const
{
	struct FGameActionAnimationRowCompilerRules : FMovieSceneTrackRowSegmentBlender
	{
		void Blend(FSegmentBlendData& BlendData) const override
		{
			MovieSceneSegmentCompiler::FilterOutUnderlappingSections(BlendData);
		}
	};
	return FGameActionAnimationRowCompilerRules();
}

FMovieSceneEvalTemplatePtr UGameActionAnimationTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FGameActionAnimationSectionTemplate(*CastChecked<UGameActionAnimationSection>(&InSection));
}

#if WITH_EDITOR
UAnimMontage* UGameActionAnimationTrack::CreateMontage(UObject* Outer, UAnimSequenceBase* AnimSequence)
{
	check(AnimSequence && AnimSequence->IsA<UAnimMontage>() == false);
	
	UAnimMontage* NewMontage = NewObject<UAnimMontage>(Outer, AnimSequence->GetFName(), RF_Transactional);
	
	// UAnimMontageFactory
	USkeleton* SourceSkeleton = AnimSequence->GetSkeleton();
	FAnimSegment NewSegment;
	NewSegment.AnimReference = AnimSequence;
	NewSegment.AnimStartTime = 0.f;
	NewSegment.AnimEndTime = AnimSequence->SequenceLength;
	NewSegment.AnimPlayRate = 1.f;
	NewSegment.LoopingCount = 1;
	NewSegment.StartPos = 0.f;

	FSlotAnimationTrack& NewTrack = NewMontage->SlotAnimTracks[0];
	NewTrack.AnimTrack.AnimSegments.Add(NewSegment);

	NewMontage->SetSequenceLength(AnimSequence->SequenceLength);

	NewMontage->SetSkeleton(SourceSkeleton);
	NewMontage->SetPreviewMesh(AnimSequence->GetPreviewMesh());

	FCompositeSection NewSection;
	NewSection.SetTime(0.0f);
	NewSection.SectionName = FName(TEXT("Default"));
	NewMontage->CompositeSections.Add(NewSection);

	return NewMontage;
}

UGameActionAnimationSection* UGameActionAnimationTrack::AddNewAnimationOnRow(FFrameNumber KeyTime, UAnimSequenceBase* AnimSequence, int32 RowIndex)
{
	UGameActionAnimationSection* NewSection = CastChecked<UGameActionAnimationSection>(CreateNewSection());
	{
		const FFrameTime AnimationLength = AnimSequence->SequenceLength * GetTypedOuter<UMovieScene>()->GetTickResolution();
		const int32 IFrameNumber = AnimationLength.FrameNumber.Value + static_cast<int>(AnimationLength.GetSubFrame() + 0.5f) + 1;
		NewSection->InitialPlacementOnRow(GetAllSections(), KeyTime, IFrameNumber, RowIndex);
		NewSection->Params.Animation = AnimSequence;
		if (UAnimMontage* Montage = Cast<UAnimMontage>(AnimSequence))
		{
			NewSection->Params.Montage = Montage;
		}
		else
		{
			NewSection->Params.Montage = CreateMontage(NewSection, AnimSequence);
		}
	}

	AddSection(*NewSection);

	return NewSection;
}
#endif

#undef LOCTEXT_NAMESPACE
