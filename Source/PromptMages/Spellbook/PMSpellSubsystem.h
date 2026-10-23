// Prompt-Mages: per-level spell state - the shared Ink pool, live objects and the cast feed.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PMTypes.h"
#include "PMSpellSubsystem.generated.h"

class APMSpawnedObject;
struct FPMParsedPrompt;

struct FPMCastRecord
{
	FString CasterName;
	FString Description;
	float Ink = 0.f;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FPMToastSignature, const FString& /*Message*/);

/**
 * Owns the team's shared state for one level (GDD 3.4): one Ink pool for everyone,
 * the 24-object cap, surge pricing for repeated prompts, banned words and the team feed.
 * Single-player for now; this moves to GameState when networking arrives.
 */
UCLASS()
class PROMPTMAGES_API UPMSpellSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	// --- Ink ---
	float GetInk() const { return Ink; }
	float GetMaxInk() const { return MaxInk; }
	bool IsInfiniteInk() const { return bInfiniteInk; }
	bool CanAfford(float Cost) const { return bInfiniteInk || Cost <= Ink + KINDA_SMALL_NUMBER; }
	void SetInkPool(float NewMax);
	void RefillInk() { Ink = MaxInk; }
	void SetInfiniteInk(bool bEnabled) { bInfiniteInk = bEnabled; }
	const TMap<FString, float>& GetInkByPlayer() const { return InkByPlayer; }

	/** Price of a prompt right now, including surge pricing (+20% per identical recast). */
	float GetCurrentCost(const FPMParsedPrompt& Prompt) const;

	// --- Words ---
	const TArray<FString>& GetBannedWords() const { return BannedWords; }
	void BanWord(const FString& Word) { BannedWords.AddUnique(Word.ToLower()); }
	void ClearBannedWords() { BannedWords.Reset(); }

	// --- Casting ---
	/** Pays for and spawns every copy of the prompt around GroundPoint. Returns the number spawned. */
	int32 CastPrompt(const FPMParsedPrompt& Prompt, const FVector& GroundPoint, const FString& CasterName, AActor* Caster);

	/** Spawns one object above GroundPoint without paying. */
	APMSpawnedObject* SpawnNoun(const FPMNounDef& Def, const FPMTraits& Traits, float SizeScale, const FString& DisplayName,
		const FVector& GroundPoint, const FString& CasterName, bool bMystery, const FString& MisheardWord, float ExtraHeight);

	const TArray<FPMCastRecord>& GetFeed() const { return Feed; }
	int32 GetLiveObjectCount() const;
	int32 GetMaxObjects() const { return MaxObjects; }
	void ClearObjects();

	// --- Messages ---
	void Toast(const FString& Message);
	FPMToastSignature OnToast;

	/** Height objects drop from, above the target point (cm). */
	static constexpr float DropHeight = 800.f;

private:
	void RegisterObject(APMSpawnedObject* Object);
	FVector FindSpawnLocation(const FVector& GroundPoint, float HalfHeight, float ExtraHeight, AActor* Ignore) const;

	float MaxInk = 60.f;
	float Ink = 60.f;
	bool bInfiniteInk = false;
	int32 MaxObjects = 24;

	TArray<FString> BannedWords;
	TMap<FString, int32> CastCounts;
	TMap<FString, float> InkByPlayer;
	TArray<FPMCastRecord> Feed;

	UPROPERTY()
	TArray<TWeakObjectPtr<APMSpawnedObject>> LiveObjects;
};
