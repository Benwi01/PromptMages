// Prompt-Mages: per-level spell state - the shared Ink pool, live objects and the cast feed.

#include "PMSpellSubsystem.h"
#include "PMPromptParser.h"
#include "PMSpawnedObject.h"
#include "PromptMages.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

bool UPMSpellSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPMSpellSubsystem::Deinitialize()
{
	OnToast.Clear();
	Super::Deinitialize();
}

void UPMSpellSubsystem::SetInkPool(float NewMax)
{
	MaxInk = FMath::Max(0.f, NewMax);
	Ink = MaxInk;
}

float UPMSpellSubsystem::GetCurrentCost(const FPMParsedPrompt& Prompt) const
{
	const int32 TimesCast = CastCounts.FindRef(Prompt.Key);
	const float Surge = 1.f + 0.2f * TimesCast;
	return FMath::RoundToFloat(Prompt.InkCost * Surge * 10.f) / 10.f;
}

int32 UPMSpellSubsystem::CastPrompt(const FPMParsedPrompt& Prompt, const FVector& GroundPoint, const FString& CasterName, AActor* Caster)
{
	if (!Prompt.CanCast())
	{
		return 0;
	}

	const float Cost = GetCurrentCost(Prompt);
	if (!CanAfford(Cost))
	{
		return 0;
	}
	if (!bInfiniteInk)
	{
		Ink = FMath::Max(0.f, Ink - Cost);
	}
	InkByPlayer.FindOrAdd(CasterName) += Cost;
	CastCounts.FindOrAdd(Prompt.Key)++;

	// Copies spawn in a ring around the target, staggered in height so they don't merge.
	const float Radius = APMSpawnedObject::ComputeRadius(*Prompt.Noun, Prompt.SizeScale);
	const float Ring = Prompt.Quantity > 1 ? Radius * 1.2f + 30.f * Prompt.Quantity : 0.f;
	int32 Spawned = 0;
	for (int32 i = 0; i < Prompt.Quantity; ++i)
	{
		const float Angle = 2.f * PI * i / Prompt.Quantity;
		const FVector Offset(FMath::Cos(Angle) * Ring, FMath::Sin(Angle) * Ring, 0.f);
		if (SpawnNoun(*Prompt.Noun, Prompt.Traits, Prompt.SizeScale, Prompt.DisplayName.Replace(*FString::Printf(TEXT("%d x "), Prompt.Quantity), TEXT("")),
			GroundPoint + Offset, CasterName, Prompt.bMystery, Prompt.MisheardWord, i * 80.f))
		{
			++Spawned;
		}
	}

	FPMCastRecord& Record = Feed.Insert_GetRef(FPMCastRecord(), 0);
	Record.CasterName = CasterName;
	Record.Description = Prompt.DisplayName;
	Record.Ink = Cost;
	if (Feed.Num() > 3)
	{
		Feed.SetNum(3);
	}

	UE_LOG(LogPromptMages, Log, TEXT("%s cast '%s' -> %s for %.1f Ink (%.1f left)"), *CasterName, *Prompt.RawText, *Prompt.DisplayName, Cost, Ink);
	return Spawned;
}

APMSpawnedObject* UPMSpellSubsystem::SpawnNoun(const FPMNounDef& Def, const FPMTraits& Traits, float SizeScale, const FString& DisplayName,
	const FVector& GroundPoint, const FString& CasterName, bool bMystery, const FString& MisheardWord, float ExtraHeight)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const float HalfHeight = APMSpawnedObject::ComputeHalfHeight(Def, SizeScale);
	const FVector Location = FindSpawnLocation(GroundPoint, HalfHeight, ExtraHeight, nullptr);
	const FRotator Rotation(0.f, FMath::FRandRange(0.f, 360.f), 0.f);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APMSpawnedObject* Object = World->SpawnActor<APMSpawnedObject>(APMSpawnedObject::StaticClass(), Location, Rotation, Params);
	if (!Object)
	{
		return nullptr;
	}

	Object->Setup(Def, Traits, SizeScale, DisplayName, CasterName, bMystery, MisheardWord);
	RegisterObject(Object);
	return Object;
}

FVector UPMSpellSubsystem::FindSpawnLocation(const FVector& GroundPoint, float HalfHeight, float ExtraHeight, AActor* Ignore) const
{
	const float MinZ = GroundPoint.Z + HalfHeight + 20.f;
	float Z = GroundPoint.Z + DropHeight + HalfHeight + ExtraHeight;

	// Don't spawn inside a ceiling: drop from just under it instead.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PMSpawnCeiling), false, Ignore);
	FHitResult Hit;
	const FVector Start = GroundPoint + FVector(0.f, 0.f, 10.f);
	const FVector End(GroundPoint.X, GroundPoint.Y, Z + HalfHeight);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		Z = Hit.ImpactPoint.Z - HalfHeight - 20.f;
	}
	return FVector(GroundPoint.X, GroundPoint.Y, FMath::Max(Z, MinZ));
}

void UPMSpellSubsystem::RegisterObject(APMSpawnedObject* Object)
{
	LiveObjects.RemoveAll([](const TWeakObjectPtr<APMSpawnedObject>& Ptr) { return !Ptr.IsValid(); });
	LiveObjects.Add(Object);

	// Object cap: the oldest object is Repossessed (GDD 3.4).
	while (LiveObjects.Num() > MaxObjects)
	{
		if (APMSpawnedObject* Oldest = LiveObjects[0].Get())
		{
			Oldest->Repossess();
		}
		LiveObjects.RemoveAt(0);
	}
}

int32 UPMSpellSubsystem::GetLiveObjectCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<APMSpawnedObject>& Ptr : LiveObjects)
	{
		Count += Ptr.IsValid() ? 1 : 0;
	}
	return Count;
}

void UPMSpellSubsystem::ClearObjects()
{
	for (const TWeakObjectPtr<APMSpawnedObject>& Ptr : LiveObjects)
	{
		if (APMSpawnedObject* Object = Ptr.Get())
		{
			Object->Destroy();
		}
	}
	LiveObjects.Reset();
}

void UPMSpellSubsystem::Toast(const FString& Message)
{
	UE_LOG(LogPromptMages, Log, TEXT("[Toast] %s"), *Message);
	OnToast.Broadcast(Message);
}

// --- Console commands for testing ---------------------------------------------------------------

namespace PMConsole
{
	UPMSpellSubsystem* Get(UWorld* World)
	{
		return World ? World->GetSubsystem<UPMSpellSubsystem>() : nullptr;
	}

	static FAutoConsoleCommandWithWorldAndArgs InfiniteInk(TEXT("pm.InfiniteInk"), TEXT("pm.InfiniteInk 1|0 - toggle infinite Ink (Practice Pit mode)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UPMSpellSubsystem* S = Get(World))
			{
				const bool bOn = Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : !S->IsInfiniteInk();
				S->SetInfiniteInk(bOn);
				S->Toast(bOn ? TEXT("Infinite Ink: ON") : TEXT("Infinite Ink: OFF"));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs SetInk(TEXT("pm.SetInk"), TEXT("pm.SetInk <amount> - set the shared Ink pool size and refill it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UPMSpellSubsystem* S = Get(World))
			{
				S->SetInkPool(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 60.f);
				S->Toast(FString::Printf(TEXT("Ink pool set to %s"), *FPMPromptParser::FormatInk(S->GetMaxInk())));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs RefillInk(TEXT("pm.RefillInk"), TEXT("pm.RefillInk - refill the shared Ink pool."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UPMSpellSubsystem* S = Get(World))
			{
				S->RefillInk();
				S->Toast(TEXT("Ink refilled."));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs Ban(TEXT("pm.Ban"), TEXT("pm.Ban <word> [word...] - ban words for this level. pm.Ban with no words clears the list."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UPMSpellSubsystem* S = Get(World))
			{
				if (Args.Num() == 0)
				{
					S->ClearBannedWords();
					S->Toast(TEXT("Banned words cleared."));
					return;
				}
				for (const FString& Word : Args)
				{
					S->BanWord(Word);
				}
				S->Toast(FString::Printf(TEXT("Company policy: we're sunsetting \"%s\" this quarter."), *FString::Join(Args, TEXT("\", \""))));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs Clear(TEXT("pm.ClearObjects"), TEXT("pm.ClearObjects - remove every summoned object."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UPMSpellSubsystem* S = Get(World))
			{
				S->ClearObjects();
			}
		}));
}
