// Prompt-Mages: the apprentice wand - Fireball, Gust, Tether and Frost bolt, paid for with personal mana.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PMSpellTypes.h"
#include "PMWandComponent.generated.h"

class APlayerController;
class UInputComponent;
class USceneComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * Q draws the wand into the right hand (the right arm then points it at the crosshair instead of grabbing).
 * While drawn: RMB casts the selected spell, 1-4 select a spell. Spells cost personal mana that refills,
 * so they never touch the team's shared Ink.
 */
UCLASS(ClassGroup = (PromptMages), meta = (BlueprintSpawnableComponent))
class PROMPTMAGES_API UPMWandComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPMWandComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	bool IsWandOut() const { return bWandOut; }
	EPMSpell GetSelectedSpell() const { return Selected; }
	float GetMana() const { return Mana; }
	float GetMaxMana() const { return MaxMana; }
	float GetCooldownRemaining(EPMSpell Spell) const;

	void SetWandOut(bool bOut);
	void SelectSpell(EPMSpell Spell);
	/** Casts the selected spell from the wand tip toward the crosshair. */
	bool CastSpell();

	/** Wand length, cm. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Wand Look")
	float WandLength = 34.f;

	/** Wand thickness at the base of the shaft, cm. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Wand Look")
	float WandThickness = 2.f;

	/**
	 * Tilts the wand upward from the line to the crosshair, in degrees, so you see more of it in first person.
	 * Spells still fly from the crystal straight to the crosshair.
	 */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Wand Look")
	float WandTiltUp = 15.f;

	/** Moves the whole wand away from where the fist holds it, in cm. X = forward, Y = right, Z = up (as you look). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Wand Look")
	FVector WandGripOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Wand")
	float MaxMana = 100.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Wand")
	float ManaRegenPerSecond = 14.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Wand")
	float GustRange = 700.f;

	/** Half-angle of the gust cone in degrees. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Wand")
	float GustAngle = 35.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Wand")
	float TetherRange = 2500.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	APlayerController* GetLocalPC() const;
	void TryBindInput();
	/** Where the fist holds the wand, where its tip is, which way it points, what it points at, and the view. */
	bool GetAim(FVector& OutTip, FVector& OutDirection, FVector& OutAimPoint, FVector* OutGrip = nullptr, FRotator* OutView = nullptr) const;
	void UpdateWandVisual();
	void Toast(const FString& Message) const;

	void OnToggleWand() { SetWandOut(!bWandOut); }
	void OnCastPressed();
	void OnSelect1() { SelectSpell(EPMSpell::Fireball); }
	void OnSelect2() { SelectSpell(EPMSpell::Gust); }
	void OnSelect3() { SelectSpell(EPMSpell::Tether); }
	void OnSelect4() { SelectSpell(EPMSpell::Frost); }

	void CastGust(const FVector& Tip, const FVector& Direction);
	void CastTether(const FVector& Tip, const FVector& Direction, const FVector& AimPoint);

	UPROPERTY()
	TObjectPtr<UInputComponent> WandInput;

	/** The wand model (grip, shaft, rings, crystal), placed with its grip in the right fist. */
	UPROPERTY()
	TObjectPtr<USceneComponent> WandRoot;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> WandParts;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> TipMaterial;

	TWeakObjectPtr<APlayerController> BoundPC;

	bool bWandOut = false;
	EPMSpell Selected = EPMSpell::Fireball;
	float Mana = 100.f;
	double LastCastTime[static_cast<int32>(EPMSpell::Count)] = { -100.0, -100.0, -100.0, -100.0 };
};
