// Prompt-Mages: short-lived greybox visual effects (bursts, beams, gust cones) built from basic shapes.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PMSpellFX.generated.h"

class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;

/** Niagara spell effects. Each sets the user parameters Color (linear colour) and Scale (float). */
namespace PMVFX
{
	/** One-shot at a spot. Returns nullptr when the slot is empty or the asset is missing (callers fall back to greybox). */
	PROMPTMAGES_API UNiagaraComponent* Spawn(UWorld* World, const TSoftObjectPtr<UNiagaraSystem>& System, const FVector& Location,
		const FRotator& Rotation, const FLinearColor& Color, float Scale);
	/** Follows Parent (trails). */
	PROMPTMAGES_API UNiagaraComponent* SpawnAttached(USceneComponent* Parent, const TSoftObjectPtr<UNiagaraSystem>& System,
		const FLinearColor& Color, float Scale);
}

UENUM()
enum class EPMFXShape : uint8
{
	Burst,   // Expanding sphere (explosions, frost shatter)
	Beam,    // Stretched cylinder between two points (tether)
	Cone     // Cone pointing along a direction (gust)
};

/** A flash of shape + light that scales and fades out, then destroys itself. */
UCLASS()
class PROMPTMAGES_API APMSpellFX : public AActor
{
	GENERATED_BODY()

public:
	APMSpellFX();

	static APMSpellFX* SpawnBurst(UWorld* World, const FVector& Location, float Radius, const FLinearColor& Color, float Duration = 0.35f);
	static APMSpellFX* SpawnBeam(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color, float Duration = 0.3f);
	static APMSpellFX* SpawnCone(UWorld* World, const FVector& Origin, const FVector& Direction, float Length, float Radius, const FLinearColor& Color, float Duration = 0.3f);

	virtual void Tick(float DeltaSeconds) override;

private:
	void Setup(EPMFXShape InShape, const FLinearColor& Color, const FVector& InStartScale, const FVector& InEndScale, float InDuration, float LightRadius);

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Light;

	FVector StartScale = FVector::OneVector;
	FVector EndScale = FVector::OneVector;
	float Duration = 0.3f;
	float Age = 0.f;
	float StartIntensity = 0.f;
};
