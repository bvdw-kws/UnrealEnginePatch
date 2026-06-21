#include "EnginePatchManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Runtime/Launch/Resources/Version.h"

FString FEnginePatchManager::GetCurrentEngineVersion()
{
	return FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION);
}

FString FEnginePatchManager::ResolveEnginePath(const FString& RelativePath)
{
	return FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Source") / RelativePath);
}

FString FEnginePatchManager::MakeBeginMarker(const FString& PatchId, const FString& OpId)
{
	return FString::Printf(TEXT("// @@PATCH_BEGIN(%s::%s)"), *PatchId, *OpId);
}

FString FEnginePatchManager::MakeEndMarker(const FString& PatchId, const FString& OpId)
{
	return FString::Printf(TEXT("// @@PATCH_END(%s::%s)"), *PatchId, *OpId);
}

bool FEnginePatchManager::IsOperationApplied(const FString& FilePath, const FString& PatchId, const FString& OpId)
{
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *FilePath)) return false;
	return Content.Contains(MakeBeginMarker(PatchId, OpId));
}

EPatchStatus FEnginePatchManager::GetPatchStatus(const FEnginePatch& Patch)
{
	FString EngineVersion = GetCurrentEngineVersion();
	const FEnginePatchVersion* MatchedVersion = Patch.Versions.FindByPredicate(
		[&](const FEnginePatchVersion& V) { return V.EngineVersion == EngineVersion; });

	if (!MatchedVersion) return EPatchStatus::NotApplicable;

	// Consider Applied if ALL operations in all files are applied.
	int32 AppliedCount = 0;
	int32 TotalCount = 0;
	for (const FEnginePatchFile& PFile : MatchedVersion->Files)
	{
		FString FullPath = ResolveEnginePath(PFile.File);
		for (const FEnginePatchOperation& Op : PFile.Operations)
		{
			++TotalCount;
			if (IsOperationApplied(FullPath, Patch.PatchId, Op.Id)) ++AppliedCount;
		}
	}
	if (TotalCount == 0) return EPatchStatus::NotApplied;
	if (AppliedCount == TotalCount) return EPatchStatus::Applied;
	if (AppliedCount == 0) return EPatchStatus::NotApplied;
	return EPatchStatus::Error; // partial application
}

bool FEnginePatchManager::ApplyOperation(const FString& FilePath, const FString& PatchId, const FEnginePatchOperation& Op, FString& OutError)
{
	if (IsOperationApplied(FilePath, PatchId, Op.Id)) return true; // idempotent

	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *FilePath))
	{
		OutError = FString::Printf(TEXT("Cannot read file: %s"), *FilePath);
		return false;
	}

	int32 LineIndex = Op.Line - 1; // convert to 0-based

	// Validate remove lines match
	if (Op.Remove.Num() > 0)
	{
		for (int32 i = 0; i < Op.Remove.Num(); ++i)
		{
			if (LineIndex + i >= Lines.Num())
			{
				OutError = FString::Printf(TEXT("Line %d out of range in %s"), Op.Line + i, *FilePath);
				return false;
			}
			FString Expected = Op.Remove[i].TrimStartAndEnd();
			FString Actual   = Lines[LineIndex + i].TrimStartAndEnd();
			if (Expected != Actual)
			{
				OutError = FString::Printf(TEXT("Line %d mismatch in %s\nExpected: %s\nActual:   %s"),
					Op.Line + i, *FilePath, *Expected, *Actual);
				return false;
			}
		}
	}

	// Build replacement block
	TArray<FString> Block;
	Block.Add(MakeBeginMarker(PatchId, Op.Id));
	for (const FString& RemovedLine : Op.Remove)
	{
		Block.Add(FString::Printf(TEXT("// @@REMOVED: %s"), *RemovedLine));
	}
	for (const FString& AddedLine : Op.Add)
	{
		Block.Add(AddedLine);
	}
	Block.Add(MakeEndMarker(PatchId, Op.Id));

	// Replace remove lines (or insert if Remove is empty)
	int32 RemoveCount = FMath::Max(Op.Remove.Num(), 0);
	if (RemoveCount > 0)
	{
		Lines.RemoveAt(LineIndex, RemoveCount);
	}
	for (int32 i = 0; i < Block.Num(); ++i)
	{
		Lines.Insert(Block[i], LineIndex + i);
	}

	if (!FFileHelper::SaveStringArrayToFile(Lines, *FilePath))
	{
		OutError = FString::Printf(TEXT("Cannot write file: %s"), *FilePath);
		return false;
	}
	return true;
}

bool FEnginePatchManager::ApplyPatch(const FEnginePatch& Patch, FString& OutError)
{
	FString EngineVersion = GetCurrentEngineVersion();
	const FEnginePatchVersion* MatchedVersion = Patch.Versions.FindByPredicate(
		[&](const FEnginePatchVersion& V) { return V.EngineVersion == EngineVersion; });
	if (!MatchedVersion) return true; // NotApplicable — not an error

	for (const FEnginePatchFile& PFile : MatchedVersion->Files)
	{
		FString FullPath = ResolveEnginePath(PFile.File);
		for (const FEnginePatchOperation& Op : PFile.Operations)
		{
			if (!ApplyOperation(FullPath, Patch.PatchId, Op, OutError)) return false;
		}
	}
	return true;
}

bool FEnginePatchManager::UnpatchPatch(const FEnginePatch& Patch, FString& OutError)
{
	FString EngineVersion = GetCurrentEngineVersion();
	const FEnginePatchVersion* MatchedVersion = Patch.Versions.FindByPredicate(
		[&](const FEnginePatchVersion& V) { return V.EngineVersion == EngineVersion; });
	if (!MatchedVersion) return true; // NotApplicable — not an error

	// Iterate operations in reverse order to handle line drift correctly during removal
	for (const FEnginePatchFile& PFile : MatchedVersion->Files)
	{
		FString FullPath = ResolveEnginePath(PFile.File);
		for (int32 i = PFile.Operations.Num() - 1; i >= 0; --i)
		{
			const FEnginePatchOperation& Op = PFile.Operations[i];
			if (!UnpatchOperation(FullPath, Patch.PatchId, Op, OutError)) return false;
		}
	}
	return true;
}

bool FEnginePatchManager::UnpatchOperation(const FString& FilePath, const FString& PatchId, const FEnginePatchOperation& Op, FString& OutError)
{
	// Stub for Task 5 - UnpatchOperation
	OutError = TEXT("UnpatchOperation not yet implemented");
	return false;
}
