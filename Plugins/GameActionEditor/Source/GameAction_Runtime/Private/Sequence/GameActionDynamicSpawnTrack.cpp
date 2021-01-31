// Fill out your copyright notice in the Description page of Project Settings.


#include "Sequence/GameActionDynamicSpawnTrack.h"
#include <IMovieSceneTracksModule.h>
#include <MovieSceneExecutionToken.h>
#include <MovieSceneSequence.h>
#include <Channels/MovieSceneChannelProxy.h>
#include <Evaluation/MovieSceneEvaluationTrack.h>

#include "Blueprint/GameActionBlueprint.h"
#include "Sequence/GameActionSequenceCustomSpawner.h"
#include "Sequence/GameActionSequencePlayer.h"

#define LOCTEXT_NAMESPACE "GameActionDynamicSpawnTrack"

UGameActionDynamicSpawnSectionBase::UGameActionDynamicSpawnSectionBase()
{
	bSupportsInfiniteRange = true;
	SetRange(TRange<FFrameNumber>::All());
	SpawnableCurve.SetDefault(true);
#if WITH_EDITOR
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(SpawnableCurve, FMovieSceneChannelMetaData(), TMovieSceneExternalValue<bool>());
#else
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(SpawnableCurve);
#endif
}

bool UGameActionDynamicSpawnSectionBase::AsReference() const
{
	UGameActionSequenceSpawnerSettingsBase* SpawnerSettings = GetSpawnerSettings();
	if (ensure(SpawnerSettings))
	{
		return SpawnerSettings->bAsReference;
	}
	return false;
}

template<typename ImplType, typename TemplateType>
struct TGameActionSpawnObjectToken : IMovieSceneExecutionToken
{
	TGameActionSpawnObjectToken(bool bInSpawned)
		: bSpawned(bInSpawned)
	{}

	void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override final
	{
		MOVIESCENE_DETAILED_SCOPE_CYCLE_COUNTER(GameActionEval_SpawnTrack_TokenExecute)

		ImplType& Impl = static_cast<ImplType&>(*this);
		FMovieSceneSpawnRegister& SpawnRegister = Player.GetSpawnRegister();
		const bool bHasSpawnedObject = SpawnRegister.FindSpawnedObject(Operand.ObjectBindingID, Operand.SequenceID).Get() != nullptr;
		if (bSpawned)
		{
			// If it's not spawned, spawn it
			if (!bHasSpawnedObject)
			{
				const UMovieSceneSequence* Sequence = Player.State.FindSequence(Operand.SequenceID);
				if (Sequence)
				{
					TGuardValue<const UGameActionSequenceSpawnerSettingsBase*> CurrentSpawnSectionGuard(FGameActionPlayerContext::CurrentSpawnerSettings, &Impl.GetSpawnerSettings());
					Impl.PreSpawnObject(SpawnRegister);
					UObject* SpawnedObject = SpawnRegister.SpawnObject(Operand.ObjectBindingID, *Sequence->GetMovieScene(), Operand.SequenceID, Player);
					Impl.PostSpawnObject(SpawnedObject);

					if (SpawnedObject)
					{
						Player.OnObjectSpawned(SpawnedObject, Operand);
					}
				}
			}

			// ensure that pre animated state is saved
			if (Impl.GetSpawnerSettings().bDestroyWhenAborted)
			{
				for (TWeakObjectPtr<> Object : Player.FindBoundObjects(Operand))
				{
					if (UObject* ObjectPtr = Object.Get())
					{
						struct FSpawnTrackPreAnimatedTokenProducer : IMovieScenePreAnimatedTokenProducer
						{
							FMovieSceneEvaluationOperand Operand;
							FSpawnTrackPreAnimatedTokenProducer(FMovieSceneEvaluationOperand InOperand) : Operand(InOperand) {}

							IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const override
							{
								struct FToken : IMovieScenePreAnimatedToken
								{
									FMovieSceneEvaluationOperand OperandToDestroy;
									FToken(FMovieSceneEvaluationOperand InOperand) : OperandToDestroy(InOperand) {}

									void RestoreState(UObject& InObject, IMovieScenePlayer& Player) override
									{
										Player.GetSpawnRegister().DestroySpawnedObject(OperandToDestroy.ObjectBindingID, OperandToDestroy.SequenceID, Player);
									}
								};

								return FToken(Operand);
							}
						};
						Player.SavePreAnimatedState(*ObjectPtr, TemplateType::GetAnimTypeID(), FSpawnTrackPreAnimatedTokenProducer(Operand));
					}
				}
			}
		}
		else if (!bSpawned && bHasSpawnedObject)
		{
			SpawnRegister.DestroySpawnedObject(Operand.ObjectBindingID, Operand.SequenceID, Player);
		}
	}

	bool bSpawned;

private:
#if false
	void PreSpawnObject(FMovieSceneSpawnRegister& SpawnRegister) { check(false); }
	void PostSpawnObject(UObject* SpawnedObject) { check(false); }
	const UGameActionSequenceSpawnerSettingsBase& GetSpawnerSettings() const { check(false); }
#endif
};

void FGameActionSpawnByTemplateSectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	bool SpawnValue = false;
	if (Section->SpawnableCurve.Evaluate(Context.GetTime(), SpawnValue))
	{
		struct FGameActionSpawnObjectByTemplateToken : TGameActionSpawnObjectToken<FGameActionSpawnObjectByTemplateToken, FGameActionSpawnByTemplateSectionTemplate>
		{
			using Super = TGameActionSpawnObjectToken;
			FGameActionSpawnObjectByTemplateToken(bool bInSpawned, const UGameActionSpawnByTemplateSection* Section)
				:Super(bInSpawned), Section(Section)
			{}

			void PreSpawnObject(FMovieSceneSpawnRegister& SpawnRegister) {}
			void PostSpawnObject(UObject* SpawnedObject) {}
			const UGameActionSequenceSpawnerSettingsBase& GetSpawnerSettings() const { return *Section->SpawnerSettings; }

			const UGameActionSpawnByTemplateSection* Section;
		};
		ExecutionTokens.Add(FGameActionSpawnObjectByTemplateToken(SpawnValue, Section));
	}
}

UGameActionSequenceSpawnerSettingsBase* UGameActionSpawnByTemplateSection::GetSpawnerSettings() const
{
	return SpawnerSettings;
}

void FGameActionSpawnBySpawnerSectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	bool SpawnValue = false;
	if (ensure(Section->CustomSpawner) && Section->SpawnableCurve.Evaluate(Context.GetTime(), SpawnValue))
	{
		struct FGameActionSpawnObjectBySpawnerToken : TGameActionSpawnObjectToken<FGameActionSpawnObjectBySpawnerToken, FGameActionSpawnBySpawnerSectionTemplate>
		{
			using Super = TGameActionSpawnObjectToken;
			FGameActionSpawnObjectBySpawnerToken(bool bInSpawned, const UGameActionSpawnBySpawnerSection* Section)
				:Super(bInSpawned), Section(Section)
			{}
		
			void PreSpawnObject(FMovieSceneSpawnRegister& SpawnRegister)
			{
				FGameActionPlayerContext::CurrentSpawner = Section->CustomSpawner;
			}
			void PostSpawnObject(UObject* SpawnedObject)
			{
				FGameActionPlayerContext::CurrentSpawner = nullptr;
			}
			const UGameActionSequenceSpawnerSettingsBase& GetSpawnerSettings() const { return *Section->CustomSpawner; }
			const UGameActionSpawnBySpawnerSection* Section;
		};
		ExecutionTokens.Add(FGameActionSpawnObjectBySpawnerToken(SpawnValue, Section));
	}
}

UGameActionSequenceSpawnerSettingsBase* UGameActionSpawnBySpawnerSection::GetSpawnerSettings() const
{
	return CustomSpawner;
}

#if WITH_EDITOR
FText UGameActionDynamicSpawnTrack::GetDisplayName() const
{
	FMovieSceneSpawnable* Spawnable = GetOwingSpawnable();
	if (UObject* Template = Spawnable->GetObjectTemplate())
	{
		return FText::FromName(Template->GetFName());
	}
	return FText::FromName(NAME_None);
}

void UGameActionDynamicSpawnTrack::SetDisplayName(const FText& NewDisplayName)
{
	FMovieSceneSpawnable* Spawnable = GetOwingSpawnable();
	Spawnable->GetObjectTemplate()->Rename(*NewDisplayName.ToString());
}

bool UGameActionDynamicSpawnTrack::ValidateDisplayName(const FText& NewDisplayName, FText& OutErrorMessage) const
{
	const FMovieSceneSpawnable* Spawnable = GetOwingSpawnable();
	if (Spawnable == nullptr)
	{
		OutErrorMessage = LOCTEXT("轨道对应的Spawnable不存在", "轨道对应的Spawnable不存在");
		return false;
	}

	const UObject* Template = Spawnable->GetObjectTemplate();
	if (Template == nullptr)
	{
		OutErrorMessage = LOCTEXT("轨道对应的Spawnable Template不存在", "轨道对应的Spawnable Template不存在");
		return false;
	}
	
	UGameActionBlueprint* Blueprint = GetTypedOuter<UGameActionBlueprint>();
	FProperty* ExistedProperty = Blueprint->SkeletonGeneratedClass->FindPropertyByName(*NewDisplayName.ToString());
	if (ExistedProperty)
	{
		if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(ExistedProperty))
		{
			if (Template->GetClass()->IsChildOf(ObjectProperty->PropertyClass) == false)
			{
				OutErrorMessage = LOCTEXT("Spawnable类型与声明不匹配", "Spawnable类型与声明不匹配");
				return false;
			}
			
			if (ExistedProperty->GetOwner<UClass>() == Blueprint->SkeletonGeneratedClass)
			{
				OutErrorMessage = LOCTEXT("已存在重名Spawnable引用", "已存在重名Spawnable引用");
				return false;
			}
		}
		else
		{
			OutErrorMessage = LOCTEXT("已存在重名变量", "已存在重名变量");
			return false;
		}
	}
	return true;
}
#endif

UMovieSceneSection* UGameActionDynamicSpawnTrack::CreateNewSection()
{
	return NewObject<UGameActionSpawnByTemplateSection>(this, NAME_None, RF_Transactional);
}

FMovieSceneEvalTemplatePtr UGameActionDynamicSpawnTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	if (Cast<UGameActionSpawnByTemplateSection>(&InSection))
	{
		return FGameActionSpawnByTemplateSectionTemplate();
	}
	return FGameActionSpawnBySpawnerSectionTemplate(CastChecked<UGameActionSpawnBySpawnerSection>(&InSection));
}

void UGameActionDynamicSpawnTrack::PostCompile(FMovieSceneEvaluationTrack& Track, const FMovieSceneTrackCompilerArgs& Args) const
{
	// All objects must be spawned/destroyed before the sequence continues
	Track.SetEvaluationGroup(IMovieSceneTracksModule::GetEvaluationGroupName(EBuiltInEvaluationGroup::SpawnObjects));
	// Set priority to highest Possessable
	const uint16 EvaluationPriority = uint16(0xFFF);
	Track.SetEvaluationPriority(EvaluationPriority);
	Track.PrioritizeTearDown();
}

FMovieSceneSpawnable* UGameActionDynamicSpawnTrack::GetOwingSpawnable() const
{
	UMovieScene* MoiveScene = GetTypedOuter<UMovieScene>();
	FGuid Binding;
	MoiveScene->FindTrackBinding(*this, Binding);
	FMovieSceneSpawnable* Spawnable = MoiveScene->FindSpawnable(Binding);
	return Spawnable;
}

#undef LOCTEXT_NAMESPACE
