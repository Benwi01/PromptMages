// Prompt-Mages: the apprentice wand - Fireball, Gust, Tether and Frost bolt, paid for with personal mana.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PMSpellTypes.h"
#include "PMWandComponent.generated.h"

class APlayerController;
class UInputComponent;
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
	bool GetAim(FVector& OutTip, FVector& OutDirection, FVector& OutAimPoint) const;
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

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> WandStick;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> WandTip;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> TipMaterial;

	TWeakObjectPtr<APlayerController> BoundPC;

	bool bWandOut = false;
	EPMSpell Selected = EPMSpell::Fireball;
	float Mana = 100.f;
	double LastCastTime[static_cast<int32>(EPMSpell::Count)] = { -100.0, -100.0, -100.0, -100.0 };
};
