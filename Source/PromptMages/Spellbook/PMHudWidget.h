// Prompt-Mages: the in-game HUD and the Spellbook parchment (GDD 2.3), built in C++.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PMHudWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UEditableText;
class UProgressBar;
class UTextBlock;
class UPMSpellcasterComponent;

/**
 * Always-on HUD (crosshair, team Ink, object count, cooldown, grip, messages) plus the Spellbook:
 * a parchment over the lower 35% of the screen with the prompt line, live cost meter,
 * physics preview chips, warnings, autocomplete and the team feed.
 */
UCLASS()
class PROMPTMAGES_API UPMHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitHud(UPMSpellcasterComponent* InCaster);
	void ShowSpellbook(bool bShow);
	TSharedPtr<SWidget> GetPromptFocusWidget();
	void AddToast(const FString& Message);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandlePromptChanged(const FText& Text);

	void BuildTree();
	void RefreshPreview();
	void RefreshStatus();
	void SetPromptText(const FString& Text);
	UTextBlock* MakeText(const FString& Text, int32 Size, const FLinearColor& Color);

	UPROPERTY()
	TObjectPtr<UCanvasPanel> Root;

	UPROPERTY()
	TObjectPtr<UTextBlock> Crosshair;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY()
	TObjectPtr<UTextBlock> CooldownText;

	UPROPERTY()
	TObjectPtr<UTextBlock> HandText;

	UPROPERTY()
	TObjectPtr<UProgressBar> ManaBar;

	UPROPERTY()
	TObjectPtr<UTextBlock> SpellText;

	UPROPERTY()
	TObjectPtr<UTextBlock> ToastText;

	UPROPERTY()
	TObjectPtr<UTextBlock> HelpText;

	UPROPERTY()
	TObjectPtr<UBorder> Spellbook;

	UPROPERTY()
	TObjectPtr<UEditableText> PromptText;

	UPROPERTY()
	TObjectPtr<UTextBlock> CostText;

	UPROPERTY()
	TObjectPtr<UTextBlock> SuggestionText;

	UPROPERTY()
	TObjectPtr<UTextBlock> ChipsText;

	UPROPERTY()
	TObjectPtr<UTextBlock> WarningText;

	UPROPERTY()
	TObjectPtr<UTextBlock> FeedText;

	TWeakObjectPtr<UPMSpellcasterComponent> Caster;
	FDelegateHandle ToastHandle;

	struct FToast
	{
		FString Text;
		double Time = 0.0;
	};
	TArray<FToast> Toasts;
	FString CurrentSuggestion;

	static constexpr double ToastSeconds = 5.0;
	static constexpr int32 MaxToasts = 5;
};
