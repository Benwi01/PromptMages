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

/** Static tuning for one wand spell. */
struct FPMSpellDef
{
	FString Name;
	float ManaCost = 20.f;
	float Cooldown = 1.f;
	FLinearColor Color = FLinearColor::White;

	static const FPMSpellDef& Get(EPMSpell Spell)
	{
		static const FPMSpellDef Defs[] = {
			{ TEXT("Fireball"), 30.f, 1.2f, FLinearColor(1.f, 0.35f, 0.05f) },
			{ TEXT("Gust"),     20.f, 0.8f, FLinearColor(0.75f, 0.9f, 1.f) },
			{ TEXT("Tether"),   15.f, 0.6f, FLinearColor(0.3f, 1.f, 0.4f) },
			{ TEXT("Frost"),    25.f, 1.0f, FLinearColor(0.4f, 0.8f, 1.f) },
		};
		return Defs[FMath::Clamp(static_cast<int32>(Spell), 0, 3)];
	}
};
