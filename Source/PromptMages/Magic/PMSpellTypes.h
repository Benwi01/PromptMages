// Prompt-Mages: the apprentice wand's spells.

#pragma once

#include "CoreMinimal.h"
#include "PMSpellTypes.generated.h"

UENUM(BlueprintType)
enum class EPMSpell : uint8
{
	Fireball,
	Gust,
	Tether,
	Frost,
	Count UMETA(Hidden)
};

/** A wand spell's cost and cooldown (editable on the Wand component). */
USTRUCT(BlueprintType)
struct FPMSpellSettings
{
	GENERATED_BODY()

	FPMSpellSettings(float InManaCost = 20.f, float InCooldown = 1.f) : ManaCost(InManaCost), Cooldown(InCooldown) {}

	/** Mana it costs to cast. */
	UPROPERTY(EditAnywhere, Category = "Spell")
	float ManaCost = 20.f;

	/** Seconds before you can cast it again. */
	UPROPERTY(EditAnywhere, Category = "Spell")
	float Cooldown = 1.f;
};

/** Fixed look of one wand spell (name and colour). */
struct FPMSpellDef
{
	FString Name;
	FLinearColor Color = FLinearColor::White;

	static const FPMSpellDef& Get(EPMSpell Spell)
	{
		static const FPMSpellDef Defs[] = {
			{ TEXT("Fireball"), FLinearColor(1.f, 0.35f, 0.05f) },
			{ TEXT("Gust"),     FLinearColor(0.75f, 0.9f, 1.f) },
			{ TEXT("Tether"),   FLinearColor(0.3f, 1.f, 0.4f) },
			{ TEXT("Frost"),    FLinearColor(0.4f, 0.8f, 1.f) },
		};
		return Defs[FMath::Clamp(static_cast<int32>(Spell), 0, 3)];
	}
};
