// Copyright 2026 Winyunq. All Rights Reserved.

#include "Commands/RTSCommandRequirements.h"

namespace
{
	const FName InvalidRequirement(TEXT("InvalidRequirement"));
	const FName UnknownRequirementState(TEXT("UnknownRequirementState"));
	const FName RequirementNotMet(TEXT("RequirementNotMet"));

	int32 CombineRoots(FRTSCommandRequirements& Requirements, int32 First, int32 Second)
	{
		if (First == INDEX_NONE) return Second;
		if (Second == INDEX_NONE) return First;
		FRTSRequirementNode Combined;
		Combined.Operator = ERTSRequirementOperator::All;
		for (const int32 Root : {First, Second})
		{
			if (Requirements.Nodes.IsValidIndex(Root))
			{
				const FRTSRequirementNode& Node = Requirements.Nodes[Root];
				if (Node.Operator == ERTSRequirementOperator::All
					&& Node.DisplayText.IsEmpty() && Node.FailureReason.IsNone())
				{
					Combined.Children.Append(Node.Children);
					continue;
				}
			}
			Combined.Children.Add(Root);
		}
		return Requirements.AddNode(Combined);
	}
}

int32 FRTSCommandRequirements::AddNode(const FRTSRequirementNode& Node)
{
	return Nodes.Add(Node);
}

int32 FRTSCommandRequirements::AppendUse(const FRTSRequirementNode& Node)
{
	const int32 Added = AddNode(Node);
	UseRoot = CombineRoots(*this, UseRoot, Added);
	return Added;
}

void FRTSCommandRequirements::Append(const FRTSCommandRequirements& Other)
{
	if (&Other == this)
	{
		const FRTSCommandRequirements Copy = Other;
		Append(Copy);
		return;
	}
	const int32 Offset = Nodes.Num();
	const auto Remap = [&Other, Offset](int32 Index)
	{
		return Other.Nodes.IsValidIndex(Index) ? Offset + Index : MAX_int32;
	};
	for (const FRTSRequirementNode& Source : Other.Nodes)
	{
		FRTSRequirementNode Copy = Source;
		for (int32& Child : Copy.Children) Child = Remap(Child);
		Nodes.Add(MoveTemp(Copy));
	}
	const int32 AddedUse = Other.UseRoot == INDEX_NONE ? INDEX_NONE : Remap(Other.UseRoot);
	const int32 AddedShow = Other.ShowRoot == INDEX_NONE ? INDEX_NONE : Remap(Other.ShowRoot);
	UseRoot = CombineRoots(*this, UseRoot, AddedUse);
	ShowRoot = CombineRoots(*this, ShowRoot, AddedShow);
}

FRTSCommandRequirementEvaluation FRTSCommandRequirementEvaluator::Evaluate(
	const FRTSCommandRequirements& Requirements,
	TFunctionRef<bool(const FRTSRequirementNode&, FRTSRequirementFact&)> QueryFact)
{
	FRTSCommandRequirementEvaluation Evaluation;
	Evaluation.Nodes.SetNum(Requirements.Nodes.Num());
	TArray<uint8> Visits;
	Visits.Init(0, Requirements.Nodes.Num());
	TArray<uint8> ValidNodes;
	ValidNodes.Init(1, Requirements.Nodes.Num());
	const auto Visit = [&](auto&& Self, int32 Index) -> void
	{
		if (!Requirements.Nodes.IsValidIndex(Index))
		{
			Evaluation.bValid = false;
			return;
		}
		if (Visits[Index] == 2) return;
		FRTSRequirementNodeResult& Result = Evaluation.Nodes[Index];
		if (Visits[Index] == 1)
		{
			Evaluation.bValid = false;
			ValidNodes[Index] = 0;
			Result.FailureReason = InvalidRequirement;
			return;
		}
		Visits[Index] = 1;
		Result.bEvaluated = true;
		const FRTSRequirementNode& Node = Requirements.Nodes[Index];
		Result.DisplayText = Node.DisplayText;
		const bool bComposite = Node.Operator == ERTSRequirementOperator::All
			|| Node.Operator == ERTSRequirementOperator::Any || Node.Operator == ERTSRequirementOperator::Not;
		if (bComposite)
		{
			bool bAllKnown = true;
			bool bAnyKnownTrue = false;
			bool bAnyKnownFalse = false;
			FName ChildFailure;
			for (const int32 Child : Node.Children)
			{
				Self(Self, Child);
				if (!Evaluation.Nodes.IsValidIndex(Child))
				{
					ValidNodes[Index] = 0;
					bAllKnown = false;
					if (ChildFailure.IsNone()) ChildFailure = InvalidRequirement;
					continue;
				}
				const FRTSRequirementNodeResult& ChildResult = Evaluation.Nodes[Child];
				ValidNodes[Index] &= ValidNodes[Child];
				bAllKnown &= ChildResult.bKnown;
				bAnyKnownTrue |= ChildResult.bKnown && ChildResult.bSatisfied;
				bAnyKnownFalse |= ChildResult.bKnown && !ChildResult.bSatisfied;
				if ((!ChildResult.bKnown || !ChildResult.bSatisfied) && ChildFailure.IsNone())
					ChildFailure = ChildResult.FailureReason;
			}
			if (Node.Operator == ERTSRequirementOperator::All)
			{
				Result.bKnown = bAnyKnownFalse || bAllKnown;
				Result.bSatisfied = bAllKnown && !bAnyKnownFalse;
			}
			else if (Node.Operator == ERTSRequirementOperator::Any)
			{
				Result.bKnown = bAnyKnownTrue || bAllKnown;
				Result.bSatisfied = bAnyKnownTrue;
			}
			else if (Node.Children.Num() == 1)
			{
				Result.bKnown = bAllKnown;
				Result.bSatisfied = bAllKnown && bAnyKnownFalse;
			}
			else
			{
				Evaluation.bValid = false;
				ValidNodes[Index] = 0;
				ChildFailure = InvalidRequirement;
			}
			if (!Result.bKnown || !Result.bSatisfied)
				Result.FailureReason = !Node.FailureReason.IsNone() ? Node.FailureReason : ChildFailure;
		}
		else
		{
			const bool bFactOperator = Node.Operator == ERTSRequirementOperator::TechnologyCompleted
				|| Node.Operator == ERTSRequirementOperator::TechnologyQueuedOrBetter
				|| Node.Operator == ERTSRequirementOperator::StateTag || Node.Operator == ERTSRequirementOperator::Unlock;
			if (!bFactOperator || !Node.Children.IsEmpty() || Node.MinimumCount < 1)
			{
				Evaluation.bValid = false;
				ValidNodes[Index] = 0;
				Result.FailureReason = InvalidRequirement;
			}
			else
			{
				FRTSRequirementFact Fact;
				Result.bKnown = QueryFact(Node, Fact);
				Result.ActualCount = Fact.Value;
				Result.bSatisfied = Result.bKnown && Fact.Value >= Node.MinimumCount;
				if (Result.DisplayText.IsEmpty()) Result.DisplayText = Fact.DisplayText;
				if (!Result.bSatisfied)
					Result.FailureReason = !Node.FailureReason.IsNone() ? Node.FailureReason
						: Result.bKnown ? RequirementNotMet : UnknownRequirementState;
			}
		}
		if (Result.DisplayText.IsEmpty())
		{
			if (!Node.Id.IsNone()) Result.DisplayText = FText::FromName(Node.Id);
			else if (Node.Tag.IsValid()) Result.DisplayText = FText::FromName(Node.Tag.GetTagName());
		}
		if ((!Result.bKnown || !Result.bSatisfied) && Result.FailureReason.IsNone())
			Result.FailureReason = Result.bKnown ? RequirementNotMet : UnknownRequirementState;
		if (!ValidNodes[Index])
		{
			Result.bKnown = false;
			Result.bSatisfied = false;
			Result.FailureReason = InvalidRequirement;
		}
		Visits[Index] = 2;
	};
	const auto ResolveRoot = [&](int32 Root)
	{
		FRTSRequirementNodeResult Result;
		if (Root == INDEX_NONE)
		{
			Result.bKnown = true;
			Result.bSatisfied = true;
			return Result;
		}
		Visit(Visit, Root);
		if (Evaluation.Nodes.IsValidIndex(Root)) return Evaluation.Nodes[Root];
		Result.FailureReason = InvalidRequirement;
		return Result;
	};
	const FRTSRequirementNodeResult Use = ResolveRoot(Requirements.UseRoot);
	const FRTSRequirementNodeResult Show = ResolveRoot(Requirements.ShowRoot);
	Evaluation.bVisible = Show.bKnown && Show.bSatisfied;
	Evaluation.bUsable = Evaluation.bValid && Use.bKnown && Use.bSatisfied;
	if (!Evaluation.bUsable)
		Evaluation.FailureReason = !Evaluation.bValid ? InvalidRequirement : Use.FailureReason;
	return Evaluation;
}

FText FRTSCommandRequirementEvaluator::FormatUseRequirements(
	const FRTSCommandRequirements& Requirements, const FRTSCommandRequirementEvaluation& Evaluation)
{
	if (Requirements.UseRoot == INDEX_NONE) return FText::GetEmpty();
	if (!Evaluation.bValid || Evaluation.Nodes.Num() != Requirements.Nodes.Num())
		return NSLOCTEXT("RTSRequirements", "InvalidConditions", "前置条件配置无效");
	TArray<FString> Lines;
	const auto FormatNode = [&](auto&& Self, int32 Index, int32 Depth) -> void
	{
		if (!Requirements.Nodes.IsValidIndex(Index) || !Evaluation.Nodes.IsValidIndex(Index)) return;
		const FRTSRequirementNode& Node = Requirements.Nodes[Index];
		const FRTSRequirementNodeResult& Result = Evaluation.Nodes[Index];
		FText Label = Result.DisplayText;
		if (Label.IsEmpty())
		{
			switch (Node.Operator)
			{
			case ERTSRequirementOperator::All:
				Label = NSLOCTEXT("RTSRequirements", "All", "满足以下全部条件");
				break;
			case ERTSRequirementOperator::Any:
				Label = NSLOCTEXT("RTSRequirements", "Any", "满足以下至少一项条件");
				break;
			case ERTSRequirementOperator::Not:
				Label = NSLOCTEXT("RTSRequirements", "Not", "以下条件必须不成立");
				break;
			default:
				Label = NSLOCTEXT("RTSRequirements", "Condition", "前置条件");
				break;
			}
		}
		if (Node.MinimumCount > 1 && Node.Operator != ERTSRequirementOperator::All
			&& Node.Operator != ERTSRequirementOperator::Any && Node.Operator != ERTSRequirementOperator::Not)
		{
			const FText Current = Result.bKnown ? FText::AsNumber(Result.ActualCount)
				: NSLOCTEXT("RTSRequirements", "UnknownCount", "未知");
			Label = FText::Format(NSLOCTEXT("RTSRequirements", "CountCondition", "{0}：需要 {1}，当前 {2}"),
				Label, FText::AsNumber(Node.MinimumCount), Current);
		}
		const FText Status = !Result.bKnown
			? NSLOCTEXT("RTSRequirements", "Unknown", "状态不可用")
			: Result.bSatisfied ? NSLOCTEXT("RTSRequirements", "Satisfied", "已满足")
				: NSLOCTEXT("RTSRequirements", "Unsatisfied", "未满足");
		Lines.Add(FString::ChrN(Depth * 2, TEXT(' '))
			+ FText::Format(NSLOCTEXT("RTSRequirements", "Line", "{0}（{1}）"), Label, Status).ToString());
		for (const int32 Child : Node.Children) Self(Self, Child, Depth + 1);
	};
	FormatNode(FormatNode, Requirements.UseRoot, 0);
	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}
