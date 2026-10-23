// Prompt-Mages: a physics object summoned from a prompt.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PMTypes.h"
#include "PMSpawnedObject.generated.h"

class UStaticMeshComponent;
class UPhysicalMaterial;
class UPointLightComponent;
class UMaterialInstanceDynamic;

/**
 * A summoned object. Built at runtime from a noun definition out of welded primitive parts,
 * with its physics driven by FPMTraits (mass, friction, restitution, buoyancy).
 */
UCLASS()
class PROMPTMAGES_API APMSpawnedObject : public AActor
{
	GENERATED_BODY()

public:
	APMSpawnedObject();

	/** Builds the meshes and physics. Call right after spawning. */
	void Setup(const FPMNounDef& Def, const FPMTraits& InTraits, float SizeScale, const FString& InDisplayName,
		const FString& InCasterName, bool bInMystery, const FString& InMisheardWord);

	UStaticMeshComponent* GetBody() const { return Body; }
	const FPMTraits& GetTraits() const { return Traits; }
	const FString& GetDisplayName() const { return DisplayName; }
	const FString& GetCasterName() const { return CasterName; }
	bool IsMystery() const { return bMystery; }

	/** Half the height of a noun's stand-in geometry at a given scale, in cm. */
	static float ComputeHalfHeight(const FPMNounDef& Def, float SizeScale);
	static float ComputeRadius(const FPMNounDef& Def, float SizeScale);

	/** Despawns with a "Repossessed" message (object cap). */
	void Repossess();

	/** Opens a Mystery Crate: replaces it with a random library object. */
	void RevealMystery();

	// --- Fire & frost (wand spells) ---

	/** Sets the object alight if it can burn (Flammability > 0). Frozen objects thaw instead. Returns true if it caught fire. */
	bool Ignite();
	void Extinguish();
	/** Freezes the object nearly in place for Seconds and puts out any fire. */
	void Freeze(float Seconds);
	bool IsOnFire() const { return bOnFire; }
	bool IsFrozen() const { return bFrozen; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	UFUNCTION()
	void OnBodyHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	void UpdateShadow();
	void Pop(const FString& Message);
	void UpdateFire(float DeltaSeconds);
	void UpdateFrost(float DeltaSeconds);
	void ApplyTint();
	void RestoreDamping();
	UMaterialInstanceDynamic* Paint(UStaticMeshComponent* Comp, const FLinearColor& Color);

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PartMaterials;
	TArray<FLinearColor> PartColors;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> FireLight;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Flames;

	bool bOnFire = false;
	float BurnDuration = 1.f;
	float BurnTimeLeft = 0.f;
	float SpreadTimer = 0.f;
	float TintTimer = 0.f;
	/** 0..1 how burnt the object looks. */
	float Char = 0.f;

	bool bFrozen = false;
	float FrozenTimeLeft = 0.f;
	float HalfHeight = 50.f;

	UPROPERTY(VisibleAnywhere, Category = "Prompt Mages")
	TObjectPtr<UStaticMeshComponent> Body;

	/** Landing shadow shown on the ground until the object first hits something (GDD 2.2 overhead warning). */
	UPROPERTY(VisibleAnywhere, Category = "Prompt Mages")
	TObjectPtr<UStaticMeshComponent> Shadow;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> ExtraParts;

	UPROPERTY()
	TObjectPtr<UPhysicalMaterial> PhysMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prompt Mages")
	FPMTraits Traits;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prompt Mages")
	FString DisplayName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prompt Mages")
	FString CasterName;

	bool bMystery = false;
	FString MisheardWord;

	int32 BounceCount = 0;
	double LastBounceTime = -1.0;
	bool bLanded = false;
	bool bDying = false;
	double SpawnTime = 0.0;
	float ShadowRadius = 50.f;
	float StartZ = 0.f;

	/** Bouncy objects pop after this many bounces (GDD 3.2). */
	static constexpr int32 MaxBounces = 10;
	/** Floating objects that drift this far up are removed. */
	static constexpr float DriftAwayHeight = 30000.f;
};
