// Fill out your copyright notice in the Description page of Project Settings.


#include "GameAction/GameActionCompilerContext.h"
#include <MovieScene.h>
#include <Kismet2/BlueprintEditorUtils.h>
#include <K2Node_FunctionEntry.h>
#include <K2Node_VariableSet.h>
#include <K2Node_IfThenElse.h>
#include <K2Node_CallFunction.h>
#include <GameFramework/Actor.h>
#include <Engine/Engine.h>
#include <Engine/BlueprintGeneratedClass.h>

#include "GameAction_Editor.h"
#include "Blueprint/GameActionBlueprint.h"
#include "Blueprint/EdGraph_GameAction.h"
#include "Blueprint/BPNode_GameActionSegment.h"
#include "Blueprint/BPNode_GameActionEntry.h"
#include "Blueprint/BPNode_GameActionTransition.h"
#include "GameAction/GameActionSegment.h"
#include "GameAction/GameActionInstance.h"
#include "Sequence/GameActionDynamicSpawnTrack.h"
#include "Sequence/GameActionSequence.h"
#include "Sequence/GameActionSequenceCustomSpawner.h"

void FActionNodeRootSeacher::SearchImpl(UEdGraphNode* Node)
{
	if (Visited.Contains(Node))
	{
		return;
	}
	Visited.Add(Node);

	if (UBPNode_GameActionSegmentBase* GameActionSegmentNode = Cast<UBPNode_GameActionSegmentBase>(Node))
	{
		ActionNodes.Add(GameActionSegmentNode);
	}
	else if (UBPNode_GameActionTransitionBase* TransitionNode = Cast<UBPNode_GameActionTransitionBase>(Node))
	{
		TransitionNodes.Add(TransitionNode);
	}

	for (UEdGraphPin* Pin : Node->GetAllPins())
	{
		if (Pin->Direction == EEdGraphPinDirection::EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				SearchImpl(LinkedPin->GetOwningNode());
			}
		}
	}
}

FActionNodeRootSeacher::FActionNodeRootSeacher(UEdGraph_GameAction* GameActionGraph)
{
	for (UEdGraphNode* EdNode : GameActionGraph->Nodes)
	{
		if (UBPNode_GameActionEntry* EntryNode = Cast<UBPNode_GameActionEntry>(EdNode))
		{
			Entries.Add(EntryNode);
			for (UEdGraphPin* LinkedPin : EntryNode->FindPinChecked(UEdGraphSchema_K2::PN_Then)->LinkedTo)
			{
				SearchImpl(LinkedPin->GetOwningNode());
			}
		}
	}
}

void FActionNodeRootSeacher::Search(UEdGraph_GameAction* GameActionGraph)
{
	*this = FActionNodeRootSeacher(GameActionGraph);
}

FGameActionCompilerContext::FGameActionCompilerContext(UGameActionBlueprint* SourceSketch, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompilerOptions)
	:Super(SourceSketch, InMessageLog, InCompilerOptions)
{

}

void FGameActionCompilerContext::CreateClassVariablesFromBlueprint()
{
	Super::CreateClassVariablesFromBlueprint();

	UGameActionBlueprint* GameActionBlueprint = CastChecked<UGameActionBlueprint>(Blueprint);
	UEdGraph_GameAction* GameActionGraph = GameActionBlueprint->GetGameActionGraph();
	UGameActionScene* GameActionScene = GameActionBlueprint->GameActionScene;
	if (GameActionGraph == nullptr || GameActionScene == nullptr)
	{
		return;
	}
	ActionNodeRootSeacher.Search(GameActionGraph);

	for (int32 Idx = ActionNodeRootSeacher.ActionNodes.Num() - 1; Idx >= 0; --Idx)
	{
		UBPNode_GameActionSegmentBase* GameActionSegmentNode = ActionNodeRootSeacher.ActionNodes[Idx];
		UBlueprint::ForceLoad(GameActionSegmentNode);
		UBlueprint::ForceLoadMembers(GameActionSegmentNode);

		UClass* ActionClass = GameActionSegmentNode->GameActionSegment->GetClass();

		const FName RefVarName = GameActionSegmentNode->GetRefVarName();
		if (FProperty* NodeRefProperty = CreateVariable(RefVarName, FEdGraphPinType(UEdGraphSchema_K2::PC_Object, NAME_None, ActionClass, EPinContainerType::None, false, FEdGraphTerminalType())))
		{
			NodeRefProperty->SetPropertyFlags(CPF_BlueprintVisible | CPF_SaveGame | CPF_DisableEditOnInstance | CPF_PersistentInstance | CPF_ExportObject | CPF_InstancedReference);
			NodeRefProperty->SetMetaData(TEXT("Category"), TEXT("游戏行为片段"));
			NodeRefProperty->SetMetaData(UGameActionBlueprint::MD_GameActionTemplateReference, TEXT("true"));
		}
		else
		{
			MessageLog.Error(TEXT("无法创建游戏行为片段引用变量[%s]，请检查是否有重名变量，若存在请修改"), *RefVarName.ToString());
		}
	}

	UBlueprint::ForceLoad(GameActionScene);
	UBlueprint::ForceLoadMembers(GameActionScene);
	for (int32 Idx = GameActionScene->TemplateDatas.Num() - 1; Idx >= 0; --Idx)
	{
		const FGameActionSceneActorData& ActorData = GameActionScene->TemplateDatas[Idx];
		if (ActorData.bAsPossessable == false)
		{
			continue;
		}
		
		if (AActor* TemplateActor = ActorData.Template)
		{
			const FName PossessableDataVarName = *FString::Printf(TEXT("%s_Data"), *TemplateActor->GetFName().ToString());
			UStruct* DataType = FGameAction_EditorModule::GetChecked().GetPossessableDataFactory(TemplateActor->GetClass()).DataType;
			check(DataType);
			if (FStructProperty* DataProperty = FindFProperty<FStructProperty>(GameActionBlueprint->ParentClass, PossessableDataVarName))
			{
				if (DataType->IsChildOf(DataProperty->Struct) == false)
				{
					MessageLog.Error(TEXT("父类中[%s]变量的类型和当前Possessable_Data的类型不匹配"), *PossessableDataVarName.ToString());
				}
			}
			else
			{
				if (FProperty* PossessableDataProperty = CreateVariable(PossessableDataVarName, FEdGraphPinType(UEdGraphSchema_K2::PC_Struct, NAME_None, DataType, EPinContainerType::None, false, FEdGraphTerminalType())))
				{
					PossessableDataProperty->SetPropertyFlags(CPF_Edit | CPF_EditConst | CPF_BlueprintVisible | CPF_BlueprintReadOnly);
					PossessableDataProperty->SetMetaData(TEXT("Category"), TEXT("实体引用|Possessable"));
				}
				else
				{
					MessageLog.Error(TEXT("无法创建PossessableData变量[%s]，请检查是否有重名变量，若存在请修改"), *PossessableDataVarName.ToString());
				}
			}
			
			const FName PossessableVarName = TemplateActor->GetFName();
			UClass* PossessableClass = TemplateActor->GetClass();
			// 处理仍然保留REINST类型引用的问题
			if (PossessableClass->GetName().StartsWith("REINST_"))
			{
				PossessableClass = CastChecked<UBlueprint>(PossessableClass->ClassGeneratedBy)->GeneratedClass;
			}

			// 支持父类声明的Possessable引用
			if (FObjectProperty* RefProperty = FindFProperty<FObjectProperty>(GameActionBlueprint->ParentClass, PossessableVarName))
			{
				if (PossessableClass->IsChildOf(RefProperty->PropertyClass) == false)
				{
					MessageLog.Error(TEXT("父类中[%s]变量的类型和当前Possessable的类型不匹配"), *PossessableVarName.ToString());
				}
			}
			else
			{
				if (FProperty* PossessableProperty = CreateVariable(PossessableVarName, FEdGraphPinType(UEdGraphSchema_K2::PC_Object, NAME_None, PossessableClass, EPinContainerType::None, false, FEdGraphTerminalType())))
				{
					PossessableProperty->SetPropertyFlags(CPF_BlueprintVisible | CPF_DisableEditOnInstance | CPF_Net);
					PossessableProperty->SetMetaData(UGameActionBlueprint::MD_GameActionPossessableReference, TEXT("true"));
					if (ActorData.bGenerateNetSetter)
					{
						PossessableProperty->SetMetaData(FBlueprintMetadata::MD_PropertySetFunction, *FString::Printf(TEXT("NetSet%s"), *PossessableProperty->GetName()));
					}
					PossessableProperty->SetMetaData(TEXT("Category"), TEXT("实体引用|Possessable"));
					NewClass->NumReplicatedProperties += 1;
				}
				else
				{
					MessageLog.Error(TEXT("无法创建Possessable变量[%s]，请检查是否有重名变量，若存在请修改"), *PossessableVarName.ToString());
				}
			}
		}
	}

	TSet<UObject*> VisitedTemplates;
	for (UBPNode_GameActionSegmentBase* GameActionSegmentNode : ActionNodeRootSeacher.ActionNodes)
	{
		if (UGameActionSequence* GameActionSequence = GameActionSegmentNode->GetGameActionSequence())
		{
			UMovieScene* MovieScene = GameActionSequence->GetMovieScene();
			for (int32 Idx = 0; Idx < MovieScene->GetSpawnableCount(); ++Idx)
			{
				FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(Idx);
				UObject* Template = Spawnable.GetObjectTemplate();
				if (ensure(Template))
				{
					if (VisitedTemplates.Contains(Template))
					{
						continue;
					}
					VisitedTemplates.Add(Template);
					
					UGameActionDynamicSpawnTrack* SpawnTrack = MovieScene->FindTrack<UGameActionDynamicSpawnTrack>(Spawnable.GetGuid());
					if (SpawnTrack == nullptr || SpawnTrack->GetAllSections().Num() == 0)
					{
						continue;
					}
					const UGameActionDynamicSpawnSectionBase* SpawnSection = SpawnTrack->SpawnSection[0];
					UBlueprint::ForceLoad(SpawnSection->GetSpawnerSettings());

					if (SpawnSection->AsReference() == false)
					{
						continue;
					}
					
					UBlueprint::ForceLoad(Template);
					UBlueprint::ForceLoadMembers(Template);

					UClass* SpawnedClass = Template->GetClass();
					const FName SpawnableVarName = Template->GetFName();
					// 处理仍然保留REINST类型引用的问题
					if (SpawnedClass->GetName().StartsWith("REINST_"))
					{
						SpawnedClass = CastChecked<UBlueprint>(SpawnedClass->ClassGeneratedBy)->GeneratedClass;
					}
					
					// 支持父类声明的Spawnable引用
					if (FObjectProperty* RefProperty = FindFProperty<FObjectProperty>(GameActionBlueprint->ParentClass, SpawnableVarName))
					{
						if (SpawnedClass->IsChildOf(RefProperty->PropertyClass) == false)
						{
							MessageLog.Error(TEXT("父类中[%s]变量的类型和当前Spawnable的类型不匹配"), *SpawnableVarName.ToString());
						}
					}
					else
					{
						if (FProperty* SpawnableProperty = CreateVariable(SpawnableVarName, FEdGraphPinType(UEdGraphSchema_K2::PC_Object, NAME_None, SpawnedClass, EPinContainerType::None, false, FEdGraphTerminalType())))
						{
							SpawnableProperty->SetPropertyFlags(CPF_BlueprintVisible | CPF_DisableEditOnInstance | CPF_BlueprintReadOnly);
							if (AActor* TemplateActor = Cast<AActor>(Template))
							{
								if (TemplateActor->GetIsReplicated())
								{
									SpawnableProperty->SetPropertyFlags(CPF_Net);
								}
							}
							SpawnableProperty->SetMetaData(UGameActionBlueprint::MD_GameActionSpawnableReference, TEXT("true"));
							SpawnableProperty->SetMetaData(TEXT("Category"), TEXT("实体引用|Spawnable"));
							NewClass->NumReplicatedProperties += 1;
						}
						else
						{
							MessageLog.Error(TEXT("无法创建Spawnable变量[%s]，请检查是否有重名变量，若存在请修改"), *SpawnableVarName.ToString());
						}
					}
				}
			}
		}
	}

	// 生成Possessable变量赋值的函数骨架
	for (const FGameActionSceneActorData& ActorData : GameActionScene->TemplateDatas)
	{
		if (ActorData.bAsPossessable == false || ActorData.Template == nullptr || ActorData.bGenerateNetSetter == false)
		{
			continue;
		}

		const FName PossessableVarName = ActorData.Template->GetFName();
		FObjectProperty* Property = FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, PossessableVarName);
		if (Property && Property->Owner.IsA<UBlueprintGeneratedClass>())
		{
			UClass* SkeletonClass = GameActionBlueprint->SkeletonGeneratedClass;
			const auto AddPossessableSetFunction = [&] (FName FunctionNameFName, EFunctionFlags FuncFlags)
			{
				// 参考 FBlueprintCompilationManagerImpl::FastGenerateSkeletonClass
				UFunction* NewFunction = NewObject<UFunction>(SkeletonClass, FunctionNameFName, RF_Public | RF_Transient);
				NewFunction->StaticLink(true);
				NewFunction->FunctionFlags = FuncFlags;

				const FEdGraphPinType ParamType { UEdGraphSchema_K2::PC_Object, NAME_None, Property->PropertyClass, EPinContainerType::None, false, FEdGraphTerminalType() };
				FProperty* Param = FKismetCompilerUtilities::CreatePropertyOnScope(NewFunction, Property->GetFName(), ParamType, SkeletonClass, CPF_BlueprintVisible | CPF_BlueprintReadOnly, Schema, MessageLog);
				Param->SetFlags(RF_Transient);
				Param->PropertyFlags |= CPF_Parm;
				NewFunction->ChildProperties = Param;

				// 参考 FAnimBlueprintCompilerContext::SetCalculatedMetaDataAndFlags
				NewFunction->NumParms = 1;
				NewFunction->ParmsSize = Property->GetSize();
				
				SkeletonClass->AddFunctionToFunctionMap(NewFunction, NewFunction->GetFName());
			};
			
			const FString SetVarToClientName = FString::Printf(TEXT("__Set%sToClient"), *PossessableVarName.ToString());
			AddPossessableSetFunction(*SetVarToClientName, FUNC_Net | FUNC_NetClient | FUNC_BlueprintCallable);
			
			const FString SetVarToServerName = FString::Printf(TEXT("__Set%sToServer"), *PossessableVarName.ToString());
			AddPossessableSetFunction(*SetVarToServerName, FUNC_Net | FUNC_NetServer | FUNC_BlueprintCallable);
			
			const FString FunctionName = FString::Printf(TEXT("NetSet%s"), *PossessableVarName.ToString());
			AddPossessableSetFunction(*FunctionName, FUNC_Public | FUNC_BlueprintCallable);
		}
	}
}

void FGameActionCompilerContext::CreateFunctionList()
{
	Super::CreateFunctionList();

	UGameActionBlueprint* GameActionBlueprint = CastChecked<UGameActionBlueprint>(Blueprint);
	if (GameActionBlueprint->IsRootBlueprint() == false)
	{
		return;
	}

	UGameActionScene* GameActionScene = GameActionBlueprint->GameActionScene;
	if (GameActionScene == nullptr)
	{
		return;
	}

	// 创建Possessable变量的网络赋值函数
	for (const FGameActionSceneActorData& ActorData : GameActionScene->TemplateDatas)
	{
		if (ActorData.bAsPossessable == false || ActorData.Template == nullptr || ActorData.bGenerateNetSetter == false)
		{
			continue;
		}

		const FName PossessableVarName = ActorData.Template->GetFName();
		FObjectProperty* Property = FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, PossessableVarName);
		if (Property && Property->Owner.IsA<UBlueprintGeneratedClass>())
		{
			// 将Setter的Meta信息临时移除，防止蓝图函数编译时产生递归的情况
			const FString CachedSetFunctionName = Property->GetMetaData(FBlueprintMetadata::MD_PropertySetFunction);
			Property->RemoveMetaData(FBlueprintMetadata::MD_PropertySetFunction);
			
			const auto CreatFunctionGraph = [this, Property](const FName& FunctionName, EFunctionFlags FuncFlags)
			{
				UEdGraph* FunctionGraph = SpawnIntermediateFunctionGraph(FunctionName.ToString());
				const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(FunctionGraph->GetSchema());

				Schema->CreateDefaultNodesForGraph(*FunctionGraph);
				K2Schema->MarkFunctionEntryAsEditable(FunctionGraph, true);
				K2Schema->AddExtraFunctionFlags(FunctionGraph, FuncFlags);

				UK2Node_FunctionEntry* EntryNode = CastChecked<UK2Node_FunctionEntry>(FunctionGraph->Nodes[0]);
				EntryNode->CustomGeneratedFunctionName = FunctionName;
				TSharedPtr<FUserPinInfo> InputPin = MakeShareable(new FUserPinInfo());
				InputPin->PinName = Property->GetFName();
				InputPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
				InputPin->PinType.PinSubCategoryObject = Property->PropertyClass;
				InputPin->DesiredPinDirection = EGPD_Output;
				EntryNode->CreatePinFromUserDefinition(InputPin);

				FGraphNodeCreator<UK2Node_VariableSet> VariableSetCreator(*FunctionGraph);
				UK2Node_VariableSet* SetVariable = VariableSetCreator.CreateNode();
				SetVariable->VariableReference.SetFromField<FObjectProperty>(Property, Blueprint->SkeletonGeneratedClass);
				VariableSetCreator.Finalize();

				EntryNode->FindPinChecked(UEdGraphSchema_K2::PN_Then)->MakeLinkTo(SetVariable->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
				EntryNode->FindPinChecked(Property->GetFName())->MakeLinkTo(SetVariable->FindPinChecked(Property->GetFName()));

				ProcessOneFunctionGraph(FunctionGraph, false);
			};
			
			const FString SetVarToClientName = FString::Printf(TEXT("__Set%sToClient"), *PossessableVarName.ToString());
			CreatFunctionGraph(*SetVarToClientName, FUNC_Net | FUNC_NetClient | FUNC_BlueprintCallable);

			const FString SetVarToServerName = FString::Printf(TEXT("__Set%sToServer"), *PossessableVarName.ToString());
			CreatFunctionGraph(*SetVarToServerName, FUNC_Net | FUNC_NetServer | FUNC_BlueprintCallable);
			
			{
				const FString FunctionName = FString::Printf(TEXT("NetSet%s"), *PossessableVarName.ToString());
				UEdGraph* FunctionGraph = SpawnIntermediateFunctionGraph(FunctionName);

				const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(FunctionGraph->GetSchema());

				Schema->CreateDefaultNodesForGraph(*FunctionGraph);
				K2Schema->MarkFunctionEntryAsEditable(FunctionGraph, true);
				K2Schema->AddExtraFunctionFlags(FunctionGraph, FUNC_Public | FUNC_BlueprintCallable);

				UK2Node_FunctionEntry* EntryNode = CastChecked<UK2Node_FunctionEntry>(FunctionGraph->Nodes[0]);
				EntryNode->CustomGeneratedFunctionName = *FunctionName;
				TSharedPtr<FUserPinInfo> InputPin = MakeShareable(new FUserPinInfo());
				InputPin->PinName = Property->GetFName();
				InputPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
				InputPin->PinType.PinSubCategoryObject = Property->PropertyClass;
				InputPin->DesiredPinDirection = EGPD_Output;
				EntryNode->CreatePinFromUserDefinition(InputPin);

				FGraphNodeCreator<UK2Node_VariableSet> VariableSetCreator(*FunctionGraph);
				UK2Node_VariableSet* SetVariable = VariableSetCreator.CreateNode();
				SetVariable->VariableReference.SetFromField<FObjectProperty>(Property, Blueprint->SkeletonGeneratedClass);
				VariableSetCreator.Finalize();

				UEdGraphPin* VarInputPin = EntryNode->FindPinChecked(Property->GetFName());
				EntryNode->FindPinChecked(UEdGraphSchema_K2::PN_Then)->MakeLinkTo(SetVariable->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
				VarInputPin->MakeLinkTo(SetVariable->FindPinChecked(Property->GetFName()));
				
				FGraphNodeCreator<UK2Node_IfThenElse> BranchCreator(*FunctionGraph);
				UK2Node_IfThenElse* BranchNode = BranchCreator.CreateNode();
				BranchCreator.Finalize();

				FGraphNodeCreator<UK2Node_CallFunction> CallHasAuthorityCreator(*FunctionGraph);
				UK2Node_CallFunction* CallHasAuthorityNode = CallHasAuthorityCreator.CreateNode();
				CallHasAuthorityNode->FunctionReference.SetSelfMember(GET_FUNCTION_NAME_CHECKED(UGameActionInstanceBase, HasAuthority));
				CallHasAuthorityCreator.Finalize();
				
				SetVariable->FindPinChecked(UEdGraphSchema_K2::PN_Then)->MakeLinkTo(BranchNode->GetExecPin());
				CallHasAuthorityNode->GetReturnValuePin()->MakeLinkTo(BranchNode->GetConditionPin());
				
				{
					FGraphNodeCreator<UK2Node_CallFunction> CallClientFunctionCreator(*FunctionGraph);
					UK2Node_CallFunction* CallClientFunctionNode = CallClientFunctionCreator.CreateNode();
					CallClientFunctionNode->FunctionReference.SetSelfMember(*SetVarToClientName);
					CallClientFunctionCreator.Finalize();

					CallClientFunctionNode->GetExecPin()->MakeLinkTo(BranchNode->GetThenPin());
					VarInputPin->MakeLinkTo(CallClientFunctionNode->FindPinChecked(Property->GetFName()));
				}

				{
					FGraphNodeCreator<UK2Node_CallFunction> CallServerFunctionCreator(*FunctionGraph);
					UK2Node_CallFunction* CallServerFunctionNode = CallServerFunctionCreator.CreateNode();
					CallServerFunctionNode->FunctionReference.SetSelfMember(*SetVarToServerName);
					CallServerFunctionCreator.Finalize();

					CallServerFunctionNode->GetExecPin()->MakeLinkTo(BranchNode->GetElsePin());
					VarInputPin->MakeLinkTo(CallServerFunctionNode->FindPinChecked(Property->GetFName()));
				}
				
				ProcessOneFunctionGraph(FunctionGraph, false);
			}

			Property->SetMetaData(FBlueprintMetadata::MD_PropertySetFunction, *CachedSetFunctionName);
		}
	}
}

void FGameActionCompilerContext::OnPostCDOCompiled()
{
	Super::OnPostCDOCompiled();
	UGameActionBlueprint* GameActionBlueprint = CastChecked<UGameActionBlueprint>(Blueprint);
	UClass* GameActionInstanceClass = Blueprint->GeneratedClass;
	UGameActionInstanceBase* GameActionInstanceCDO = CastChecked<UGameActionInstanceBase>(Blueprint->GeneratedClass->ClassDefaultObject);

	UBlueprint::ForceLoad(GameActionBlueprint);
	UBlueprint::ForceLoadMembers(GameActionBlueprint);

	if (GameActionBlueprint->IsRootBlueprint())
	{
		// 清理入口函数
		for (UBPNode_GameActionEntry* Entry : ActionNodeRootSeacher.Entries)
		{
			if (UFunction* EntryFunction = GameActionInstanceClass->FindFunctionByName(Entry->CustomFunctionName, EIncludeSuperFlag::ExcludeSuper))
			{
				GameActionInstanceClass->RemoveFunctionFromFunctionMap(EntryFunction);
			}
		}

		// 移除空实现的跳转函数
		for (UBPNode_GameActionTransitionBase* TransitionNode : ActionNodeRootSeacher.TransitionNodes)
		{
			UGameActionTransitionGraph* BoundGraph = TransitionNode->BoundGraph;
			UEdGraphPin* AutonomousPin = BoundGraph->ResultNode->FindPinChecked(GameActionTransitionResultUtils::AutonomousPinName);
			const bool IsAutonomousPinDefault = AutonomousPin->LinkedTo.Num() == 0 && AutonomousPin->GetDefaultAsString() == TEXT("true");
			UEdGraphPin* ServerPin = BoundGraph->ResultNode->FindPinChecked(GameActionTransitionResultUtils::ServerPinName);
			const bool IsServerPinDefault = ServerPin->LinkedTo.Num() == 0 && ServerPin->GetDefaultAsString() == TEXT("true");
			if (IsAutonomousPinDefault && IsServerPinDefault)
			{
				if (UFunction* TransitionFunction = GameActionInstanceClass->FindFunctionByName(BoundGraph->EntryNode->CustomGeneratedFunctionName, EIncludeSuperFlag::ExcludeSuper))
				{
					GameActionInstanceClass->RemoveFunctionFromFunctionMap(TransitionFunction);
				}
			}
		}

		TMap<FName, UGameActionSegmentBase*> InstanceMap;
		for (UBPNode_GameActionSegmentBase* GameActionSegmentNode : ActionNodeRootSeacher.ActionNodes)
		{
			if (GameActionSegmentNode->GameActionSegment == nullptr)
			{
				continue;
			}
			UClass* ActionClass = GameActionSegmentNode->GameActionSegment->GetClass();
			const FName RefVarName = GameActionSegmentNode->GetRefVarName();

			// 先把旧的Segment删除
			if (UGameActionSegmentBase* GameActionSegment = FindObjectFast<UGameActionSegmentBase>(GameActionInstanceCDO, RefVarName))
			{
				UBlueprint::ForceLoad(GameActionSegment);
				GameActionSegment->ConditionalPostLoad();
				GameActionSegment->MarkPendingKill();
				GameActionSegment->ConditionalBeginDestroy();
			}

			FObjectDuplicationParameters Parameters(GameActionSegmentNode->GameActionSegment, GameActionInstanceCDO);
			Parameters.DestName = RefVarName;
			Parameters.DestClass = GameActionSegmentNode->GameActionSegment->GetClass();
			Parameters.ApplyFlags = RF_Transactional | RF_DefaultSubObject | RF_Public;
			UGameActionSegmentBase* GameActionSegmentTemplate = CastChecked<UGameActionSegmentBase>(::StaticDuplicateObjectEx(Parameters));
			GameActionSegmentTemplate->BPNodeTemplate = GameActionSegmentNode;

			FObjectProperty* ActionProperty = FindFProperty<FObjectProperty>(GameActionInstanceClass, RefVarName);
			*ActionProperty->ContainerPtrToValuePtr<UGameActionSegmentBase*>(GameActionInstanceCDO) = GameActionSegmentTemplate;
			InstanceMap.Add(RefVarName, GameActionSegmentTemplate);

			// 修改编译后的Spawnable的Outer为Class，因为Outer为Blueprint会导致打包后Template等丢失
			if (UGameActionSegment* Segment = Cast<UGameActionSegment>(GameActionSegmentTemplate))
			{
				UMovieScene* MovieScene = Segment->GameActionSequence->GetMovieScene();
				for (int32 Idx = 0; Idx < MovieScene->GetSpawnableCount(); ++Idx)
				{
					FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(Idx);
					UObject* Template = Spawnable.GetObjectTemplate();
					if (Template == nullptr)
					{
						continue;
					}

					FObjectDuplicationParameters SpawnableParameters(Template, GameActionInstanceClass);
					SpawnableParameters.DestName = Spawnable.GetObjectTemplate()->GetFName();
					SpawnableParameters.DestClass = Spawnable.GetObjectTemplate()->GetClass();
					SpawnableParameters.ApplyFlags = RF_Transactional | RF_DefaultSubObject | RF_Public;

					Spawnable.SetObjectTemplate(::StaticDuplicateObjectEx(SpawnableParameters));

					UGameActionDynamicSpawnTrack* SpawnTrack = MovieScene->FindTrack<UGameActionDynamicSpawnTrack>(Spawnable.GetGuid());
					if (ensure(SpawnTrack && SpawnTrack->SpawnSection.Num() == 1))
					{
						if (UGameActionSpawnByTemplateSection* SpawnByTemplateSection = Cast<UGameActionSpawnByTemplateSection>(SpawnTrack->SpawnSection[0]))
						{
							UGameActionSequenceSpawnerSettings* SpawnerSettings = SpawnByTemplateSection->SpawnerSettings;
							if (ensure(SpawnerSettings))
							{
								FObjectDuplicationParameters CustomSpawnerParameters(SpawnerSettings, GameActionInstanceClass);
								CustomSpawnerParameters.DestName = SpawnerSettings->GetFName();
								CustomSpawnerParameters.DestClass = SpawnerSettings->GetClass();
								CustomSpawnerParameters.ApplyFlags = RF_Transactional | RF_DefaultSubObject | RF_Public;

								SpawnByTemplateSection->SpawnerSettings = CastChecked<UGameActionSequenceSpawnerSettings>(::StaticDuplicateObjectEx(CustomSpawnerParameters));
							}
						}
						else if (UGameActionSpawnBySpawnerSection* SpawnBySpawnerSection = Cast<UGameActionSpawnBySpawnerSection>(SpawnTrack->SpawnSection[0]))
						{
							UGameActionSequenceCustomSpawnerBase* CustomSpawner = SpawnBySpawnerSection->CustomSpawner;
							if (ensure(CustomSpawner))
							{
								FObjectDuplicationParameters CustomSpawnerParameters(CustomSpawner, GameActionInstanceClass);
								CustomSpawnerParameters.DestName = CustomSpawner->GetFName();
								CustomSpawnerParameters.DestClass = CustomSpawner->GetClass();
								CustomSpawnerParameters.ApplyFlags = RF_Transactional | RF_DefaultSubObject | RF_Public;

								SpawnBySpawnerSection->CustomSpawner = CastChecked<UGameActionSequenceCustomSpawnerBase>(::StaticDuplicateObjectEx(CustomSpawnerParameters));
							}
						}
					}
				}
			}

			// 添加输入变量结算函数
			{
				const FName EvaluateExposedInputsEventName = *FString::Printf(TEXT("%s_EvaluateExposedInputsEvent"), *GameActionSegmentNode->GetRefVarName().ToString());
				if (UFunction* EventFunction = FindUField<UFunction>(GameActionInstanceClass, EvaluateExposedInputsEventName))
				{
					GameActionSegmentTemplate->EvaluateExposedInputsEvent.BindUFunction(GameActionInstanceCDO, EvaluateExposedInputsEventName);
				}
			}

			// 添加行为片段事件
			for (TFieldIterator<FDelegateProperty> It(ActionClass); It; ++It)
			{
				if (It->GetBoolMetaData(TEXT("GameActionSegmentEvent")))
				{
					const FName EventName = *FString::Printf(TEXT("%s_%s"), *RefVarName.ToString(), *It->GetName());
					if (UFunction* EventFunction = FindUField<UFunction>(GameActionInstanceClass, EventName))
					{
						It->ContainerPtrToValuePtr<FScriptDelegate>(GameActionSegmentTemplate)->BindUFunction(GameActionInstanceCDO, EventName);
					}
				}
			}
		}

		for (const TPair<FName, UGameActionSegmentBase*>& Pair : InstanceMap)
		{
			const FName& RefVarName = Pair.Key;
			UGameActionSegmentBase* GameActionSegmentTemplate = Pair.Value;
			FTransitionData& TransitionData = ActionTransitionDatas[RefVarName];
			check(GameActionSegmentTemplate->TickTransitions.Num() == 0);
			TransitionData.TickDatas.Sort([](const FTransitionData::FTickData& LHS, const FTransitionData::FTickData& RHS) { return LHS.Order < RHS.Order; });
			for (const FTransitionData::FTickData& Data : TransitionData.TickDatas)
			{
				FGameActionTickTransition& TickTransition = GameActionSegmentTemplate->TickTransitions.AddDefaulted_GetRef();
				TickTransition.TransitionToSegment = InstanceMap[Data.SegmentName];
				if (GameActionInstanceCDO->FindFunction(Data.Condition) != nullptr)
				{
					TickTransition.Condition.BindUFunction(GameActionInstanceCDO, Data.Condition);
					check(TickTransition.Condition.IsBound());
				}
			}
			check(GameActionSegmentTemplate->EventTransitions.Num() == 0);
			TransitionData.EventDatas.Sort([](const FTransitionData::FEventData& LHS, const FTransitionData::FEventData& RHS) { return LHS.Order < RHS.Order; });
			for (const FTransitionData::FEventData& Data : TransitionData.EventDatas)
			{
				FGameActionEventTransition& EventTransition = GameActionSegmentTemplate->EventTransitions.AddDefaulted_GetRef();
				EventTransition.EventName = Data.Event;
				EventTransition.TransitionToSegment = InstanceMap[Data.SegmentName];
				if (GameActionInstanceCDO->FindFunction(Data.Condition) != nullptr)
				{
					EventTransition.Condition.BindUFunction(GameActionInstanceCDO, Data.Condition);
					check(EventTransition.Condition.IsBound());
				}
			}
		}

		TArray<FStructProperty*> AllEntryProperties;
		for (TFieldIterator<FStructProperty> It(GameActionInstanceClass); It; ++It)
		{
			if (It->Struct->IsChildOf(StaticStruct<FGameActionEntry>()))
			{
				AllEntryProperties.Add(*It);
			}
		}
		for (UBPNode_GameActionEntry* Entry : ActionNodeRootSeacher.Entries)
		{
			if (FStructProperty* EntryProperty = Entry->EntryTransitionProperty.Get())
			{
				AllEntryProperties.Remove(EntryProperty);

				if (EntryProperty->Struct->IsChildOf(StaticStruct<FGameActionEntry>()) == false)
				{
					continue;
				}

				if (FGameActionEntry* EntryData = EntryProperty->ContainerPtrToValuePtr<FGameActionEntry>(GameActionInstanceCDO))
				{
					EntryData->Transitions.Empty();
					TArray<UBPNode_GameActionEntryTransition*> TransitionNodes;
					for (UEdGraphPin* Pin : Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then)->LinkedTo)
					{
						if (UBPNode_GameActionEntryTransition* TransitionNode = Cast<UBPNode_GameActionEntryTransition>(Pin->GetOwningNode()))
						{
							TransitionNodes.Add(TransitionNode);
						}
					}

					TransitionNodes.Sort([](const UBPNode_GameActionEntryTransition& LHS, const UBPNode_GameActionEntryTransition& RHS) { return LHS.NodePosY < RHS.NodePosY; });
					for (UBPNode_GameActionEntryTransition* TransitionNode : TransitionNodes)
					{
						FGameActionEntryTransition& EntryTransition = EntryData->Transitions.AddDefaulted_GetRef();
						EntryTransition.TransitionToSegment = InstanceMap[TransitionNode->ToNode->GetRefVarName()];
						const FName Condition = TransitionNode->GetTransitionName();
						if (GameActionInstanceCDO->FindFunction(Condition) != nullptr)
						{
							EntryTransition.Condition.BindUFunction(GameActionInstanceCDO, Condition);
							check(EntryTransition.Condition.IsBound());
						}
					}
				}
			}
			else
			{
				MessageLog.Error(TEXT("@@入口无法在类定义中找到，请删除该入口"), Entry);
			}
		}
		for (FStructProperty* NotFindEntry : AllEntryProperties)
		{
			MessageLog.Error(TEXT("@@入口在类定义中存在，但是在当前图表中无法找到，请添加该入口"), *NotFindEntry->GetDisplayNameText().ToString());
		}

		if (UGameActionScene* GameActionScene = GameActionBlueprint->GameActionScene)
		{
			for (const FGameActionSceneActorData& ActorData : GameActionScene->TemplateDatas)
			{
				if (ActorData.bAsPossessable == false)
				{
					continue;
				}
				if (AActor* TemplateActor = ActorData.Template)
				{
					const FName PossessableDataVarName = *FString::Printf(TEXT("%s_Data"), *TemplateActor->GetFName().ToString());
					FStructProperty* PossessableDataProperty = FindFProperty<FStructProperty>(GameActionInstanceClass, PossessableDataVarName);
					if (PossessableDataProperty == nullptr)
					{
						return;
					}

					FGameActionPossessableActorData* PossessableActorData = PossessableDataProperty->ContainerPtrToValuePtr<FGameActionPossessableActorData>(GameActionInstanceCDO);
					check(PossessableActorData);
					FGameAction_EditorModule::GetChecked().GetPossessableDataFactory(TemplateActor->GetClass()).SetDataValue(TemplateActor, *PossessableActorData);
				}
			}
		}
	}
	else
	{
		UGameActionInstanceBase* ParentCDO = GameActionBlueprint->ParentClass->GetDefaultObject<UGameActionInstanceBase>();
		const TArray<FGameActionSequenceOverride>& SequenceOverrides = GameActionBlueprint->SequenceOverrides;

		// 父类中重命名或者添加了行为片段子类中的片段会丢失
		// 通过这个函数将父类的Action同步给子类
		// 导致的问题是子类无法修改父类的数据
		UEngine::CopyPropertiesForUnrelatedObjects(ParentCDO, GameActionInstanceCDO);
		
		for (TFieldIterator<FObjectProperty> It(GameActionInstanceClass); It; ++It)
		{
			if (It->PropertyClass && It->PropertyClass->IsChildOf<UGameActionSegmentBase>() && It->GetBoolMetaData(UGameActionBlueprint::MD_GameActionTemplateReference))
			{
				UGameActionSegmentBase* ActionTemplate = *It->ContainerPtrToValuePtr<UGameActionSegmentBase*>(GameActionInstanceCDO);
				UGameActionSegmentBase* ParentActionTemplate = *It->ContainerPtrToValuePtr<UGameActionSegmentBase*>(ParentCDO);

				ActionTemplate->BPNodeTemplate = ParentActionTemplate->BPNodeTemplate;
				
				// 子类覆盖父类的ActionSequence
				UGameActionSegment* ActionSequenceTemplate = Cast<UGameActionSegment>(ActionTemplate);
				UGameActionSegment* ParentActionSequenceTemplate = Cast<UGameActionSegment>(ParentActionTemplate);
				if (ActionSequenceTemplate && ParentActionSequenceTemplate)
				{
					UGameActionSequence* SequenceOverride = nullptr;
					if (const FGameActionSequenceOverride* Override = SequenceOverrides.FindByPredicate([&](const FGameActionSequenceOverride& E)
					{
						if (UBPNode_GameActionSegment* GameActionSegment = Cast<UBPNode_GameActionSegment>(E.NodeOverride.Get()))
						{
							if (E.bEnableOverride == false || ensure(E.SequenceOverride) == false)
							{
								return false;
							}

							return GameActionSegment->GetRefVarName() == It->GetFName();
						}
						return false;
					}))
					{
						SequenceOverride = Override->SequenceOverride;
					}
					else
					{
						SequenceOverride = ParentActionSequenceTemplate->GameActionSequence;
					}

					if (ensure(SequenceOverride))
					{
						FObjectDuplicationParameters Parameters(SequenceOverride, ActionTemplate);
						Parameters.DestClass = SequenceOverride->GetClass();
						Parameters.ApplyFlags = RF_Transactional | RF_Public;
						ActionSequenceTemplate->GameActionSequence = CastChecked<UGameActionSequence>(::StaticDuplicateObjectEx(Parameters));
					}
				}
			}
		}
	}
}
