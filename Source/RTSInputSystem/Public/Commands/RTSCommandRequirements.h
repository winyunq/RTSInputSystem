// Copyright 2026 Winyunq. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "RTSCommandRequirements.generated.h"

/** Shared command conditions. Domain owners supply facts; this graph only combines them. */
UENUM(BlueprintType)
enum class ERTSRequirementOperator : uint8
{
	All,
	Any,
	Not,
	TechnologyCompleted,
	TechnologyQueuedOrBetter,
	StateTag,
	Unlock
};

USTRUCT(BlueprintType)
struct RTSINPUTSYSTEM_API FRTSRequirementNode
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements")
	ERTSRequirementOperator Operator = ERTSRequirementOperator::All;

	/** Indices in the containing graph; Not requires exactly one child. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements")
	TArray<int32> Children;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements")
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements")
	FGameplayTagContainer TargetTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements", meta = (ClampMin = "1"))
	int32 MinimumCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements")
	FText DisplayText;

	/** Optional compatibility code interpreted by the caller, never by the evaluator. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements")
	FName FailureReason;
};

/**
 * @brief 保存命令使用条件和显示条件。
 * @details 节点通过索引引用子节点，UseRoot 与 ShowRoot 定位两类条件。追加条件时重新定位索引。
 *
 * ```
 * 条件节点 → UseRoot / ShowRoot
 * Append → 重映射节点索引 → 合并对应条件树
 * ```
 */
USTRUCT(BlueprintType)
struct RTSINPUTSYSTEM_API FRTSCommandRequirements
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements")
	TArray<FRTSRequirementNode> Nodes;

	/** An absent root imposes no condition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements")
	int32 UseRoot = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements")
	int32 ShowRoot = INDEX_NONE;

	int32 AddNode(const FRTSRequirementNode& Node);
	/** Adds a Use condition while retaining the existing Use graph and all Show references. 
	 * @brief 追加单个命令使用条件。
	 * @details 保存节点并通过 CombineRoots 合并到当前 UseRoot。
	 * @param Node 要加入的使用条件节点。
	 * @return 追加节点在 Nodes 中的下标。
	 */
	int32 AppendUse(const FRTSRequirementNode& Node);
	/** Copies another graph, adjusts its references, and ANDs the corresponding roots. 
	 * @brief 追加另一组命令条件。
	 * @details 按当前节点数偏移新节点及其子节点索引，然后把原有与新增的使用根、显示根分别合并。
	 * @param Other 要追加的条件节点与根节点。
	 */
	void Append(const FRTSCommandRequirements& Other);
};

/** Multiple command definitions can reference the same authored Use/Show conditions. */
UCLASS(BlueprintType)
class RTSINPUTSYSTEM_API URTSCommandRequirementAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Requirements")
	FRTSCommandRequirements Requirements;
};

USTRUCT(BlueprintType)
struct RTSINPUTSYSTEM_API FRTSRequirementFact
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Requirements")
	int32 Value = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Requirements")
	FText DisplayText;
};

USTRUCT(BlueprintType)
struct RTSINPUTSYSTEM_API FRTSRequirementNodeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Requirements")
	bool bEvaluated = false;

	UPROPERTY(BlueprintReadOnly, Category = "Requirements")
	bool bKnown = false;

	UPROPERTY(BlueprintReadOnly, Category = "Requirements")
	bool bSatisfied = false;

	UPROPERTY(BlueprintReadOnly, Category = "Requirements")
	int32 ActualCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Requirements")
	FText DisplayText;

	UPROPERTY(BlueprintReadOnly, Category = "Requirements")
	FName FailureReason;
};

/** One read-only evaluation supplies visibility, availability, and tooltip details. */
USTRUCT(BlueprintType)
struct RTSINPUTSYSTEM_API FRTSCommandRequirementEvaluation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Requirements")
	bool bVisible = true;

	UPROPERTY(BlueprintReadOnly, Category = "Requirements")
	bool bUsable = true;

	UPROPERTY(BlueprintReadOnly, Category = "Requirements")
	bool bValid = true;

	UPROPERTY(BlueprintReadOnly, Category = "Requirements")
	FName FailureReason;

	/** Results have the same indices as the definition nodes. */
	UPROPERTY(BlueprintReadOnly, Category = "Requirements")
	TArray<FRTSRequirementNodeResult> Nodes;
};

/** Display of the cost quoted by the original command owner; never used for payment. */
USTRUCT(BlueprintType)
struct RTSINPUTSYSTEM_API FRTSCommandCost
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "RTS Command")
	FName ResourceId;

	UPROPERTY(BlueprintReadWrite, Category = "RTS Command")
	int32 Amount = 0;
};

/** Independent label/value cells prepared by the owner from its loaded definition. */
USTRUCT(BlueprintType)
struct RTSINPUTSYSTEM_API FRTSCommandTooltipRow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "RTS Command")
	FText Label;

	UPROPERTY(BlueprintReadWrite, Category = "RTS Command")
	FText Value;

	UPROPERTY(BlueprintReadWrite, Category = "RTS Command")
	FName IconId;
};

/** Query result only; authoritative command and task state stays with its original owner. */
USTRUCT(BlueprintType)
struct RTSINPUTSYSTEM_API FRTSCommandState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "RTS Command")
	bool bHandled = false;

	UPROPERTY(BlueprintReadWrite, Category = "RTS Command")
	bool bVisible = true;

	UPROPERTY(BlueprintReadWrite, Category = "RTS Command")
	bool bAvailable = true;

	UPROPERTY(BlueprintReadWrite, Category = "RTS Command")
	FText Description;

	UPROPERTY(BlueprintReadWrite, Category = "RTS Command")
	TArray<FRTSCommandCost> Costs;

	UPROPERTY(BlueprintReadWrite, Category = "RTS Command")
	TArray<FRTSCommandTooltipRow> EffectRows;

	UPROPERTY(BlueprintReadWrite, Category = "RTS Command")
	float DurationSeconds = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "RTS Command")
	FText RequirementDescription;

	UPROPERTY(BlueprintReadWrite, Category = "RTS Command")
	FText StatusDescription;
};

struct RTSINPUTSYSTEM_API FRTSCommandRequirementEvaluator
{
	static FRTSCommandRequirementEvaluation Evaluate(const FRTSCommandRequirements& Requirements,
		TFunctionRef<bool(const FRTSRequirementNode&, FRTSRequirementFact&)> QueryFact);

	/** Formats graph relationships and evaluated facts without querying domain state again. */
	static FText FormatUseRequirements(const FRTSCommandRequirements& Requirements,
		const FRTSCommandRequirementEvaluation& Evaluation);
};
