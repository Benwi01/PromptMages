// Prompt-Mages: wand projectiles (Fireball, Frost bolt).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PMSpellTypes.h"
#include "PMSpellProjectile.generated.h"

class UPointLightComponent;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;
class UNiagaraComponent;

UCLASS()
class PROMPTMAGES_API APMSpellProjectile : public AActor
{
	GENERATED_BODY()

public:
	APMSpellProjectile();

	void Launch(EPMSpell InSpell, AActor* InCaster, const FVector& Direction);

	/** Fireball blast: knocks things back, launches characters and sets flammable objects alight. */
	static void Explode(UWorld* World, const FVector& Center, AActor* Caster);

	/** Frost shatter: freezes objects and characters near the impact. */
	static void Shatter(UWorld* World, const FVector& Center, AActor* Caster);

	/** Freezes a character in place for a few seconds. */
	static void FreezeCharacter(class ACharacter* Character, float Seconds);

protected:
	virtual void LifeSpanExpired() override;

private:
	UFUNCTION()
	void OnStop(const FHitResult& Hit);

	void Detonate(const FVector& Location);

	UPROPERTY(VisibleAnywhere, Category = "Prompt Mages")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Prompt Mages")
	TObjectPtr<UStaticMeshComponent> Core;

	UPROPERTY(VisibleAnywhere, Category = "Prompt Mages")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(VisibleAnywhere, Category = "Prompt Mages")
	TObjectPtr<UProjectileMovementComponent> Movement;

	/** The trail; it stops emitting at impact and its last particles fade out before the bolt is removed. */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> Trail;

	EPMSpell Spell = EPMSpell::Fireball;
	TWeakObjectPtr<AActor> Caster;
	bool bDetonated = false;
};
