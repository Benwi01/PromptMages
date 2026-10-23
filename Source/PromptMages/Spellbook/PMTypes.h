// Prompt-Mages: shared types for the prompt-to-object engine (GDD section 3).

#pragma once

#include "CoreMinimal.h"
#include "PMTypes.generated.h"

class UStaticMesh;
class UMaterialInterface;

/** Engine basic shapes used as stand-in geometry until real meshes exist. */
UENUM(BlueprintType)
enum class EPMShape : uint8
{
	Cube,
	Sphere,
	Cylinder,
	Cone
};

/** The five physical traits every summoned object carries (GDD 3.2), plus the secondary tags this slice uses. */
USTRUCT(BlueprintType)
struct PROMPTMAGES_API FPMTraits
{
	GENERATED_BODY()

	/** Rigidbody mass. 0.1 - 5000 kg. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traits")
	float WeightKg = 10.f;

	/** -1 sinks fast, +2 floats high. Above 1.5 the object floats in air. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traits")
	float Buoyancy = 0.5f;

	/** 0 fireproof, 3 ignites instantly. Stored only in this slice. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traits")
	float Flammability = 1.f;

	/** -2 repels metal, +2 attracts metal. Stored only in this slice. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traits")
	float Magnetism = 0.f;

	/** Physics restitution. Above 1.0 the object gains speed on every bounce. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traits")
	float Elasticity = 0.3f;

	/** Surface friction (the Greasy tag drops it to near zero). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traits")
	float Friction = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traits")
	bool bMetal = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traits")
	bool bGreasy = false;

	bool FloatsInAir() const { return Buoyancy > 1.5f; }

	void Clamp()
	{
		WeightKg = FMath::Clamp(WeightKg, 0.1f, 5000.f);
		Buoyancy = FMath::Clamp(Buoyancy, -1.f, 2.f);
		Flammability = FMath::Clamp(Flammability, 0.f, 3.f);
		Magnetism = FMath::Clamp(Magnetism, -2.f, 2.f);
		Elasticity = FMath::Clamp(Elasticity, 0.f, 1.2f);
		Friction = FMath::Clamp(Friction, 0.f, 2.f);
	}
};

/** One primitive piece of a stand-in object. Sizes and offsets are in cm at scale 1. */
struct FPMShapePart
{
	EPMShape Shape = EPMShape::Cube;
	FVector Size = FVector(100.f);
	FVector Offset = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FLinearColor Color = FLinearColor::White;
};

/** A hand-authored base object from the noun library. Parts[0] is the physics root and sits at the origin. */
struct FPMNounDef
{
	FName Id;
	FString DisplayName;
	TArray<FString> Words;
	float InkCost = 1.f;
	FPMTraits Traits;
	TArray<FPMShapePart> Parts;
};

/** A reusable trait transform (GDD 3.3). */
struct FPMModifierDef
{
	FName Id;
	FString DisplayName;
	TArray<FString> Words;
	float InkCost = 1.f;
	TFunction<void(FPMTraits&)> Apply;
};

/** A size word: scales the object and its Ink price. Weight scales with volume. */
struct FPMSizeDef
{
	FString DisplayName;
	TArray<FString> Words;
	float Scale = 1.f;
	float InkMultiplier = 1.f;
};
