// Prompt-Mages: a physics object summoned from a prompt.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PMTypes.h"
#include "PMSpawnedObject.generated.h"

class UStaticMeshComponent;
class UPhysicalMaterial;

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

	virtual void Tick(float DeltaSeconds) override;

protected:
	UFUNCTION()
	void OnBodyHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	void UpdateShadow();
	void Pop(const FString& Message);

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
