// Prompt-Mages: the apprentice wand's spells.

#pragma once

#include "CoreMinimal.h"
#include "PMSpellTypes.generated.h"

class USoundBase;
class UNiagaraSystem;

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

	/** Played at the wand tip when you cast it (empty = silent). */
	UPROPERTY(EditAnywhere, Category = "Spell")
	TObjectPtr<USoundBase> CastSound;

	/** Niagara effect at the wand tip when you cast (empty = none). */
	UPROPERTY(EditAnywhere, Category = "Spell|Effects")
	TSoftObjectPtr<UNiagaraSystem> CastEffect;

	/** Fireball/Frost: the trail that follows the bolt. Gust: the wind. Tether: the beam (gets User.BeamEnd). */
	UPROPERTY(EditAnywhere, Category = "Spell|Effects")
	TSoftObjectPtr<UNiagaraSystem> TravelEffect;

	/** Where it lands: the explosion, the frost shatter, the tether's grab spark. */
	UPROPERTY(EditAnywhere, Category = "Spell|Effects")
	TSoftObjectPtr<UNiagaraSystem> ImpactEffect;

	/** Makes all of this spell's effects bigger or smaller (also scales with blast radius / range). */
	UPROPERTY(EditAnywhere, Category = "Spell|Effects")
	float EffectScale = 1.f;
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
