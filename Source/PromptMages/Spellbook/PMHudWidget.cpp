// Prompt-Mages: the in-game HUD and the Spellbook parchment (GDD 2.3), built in C++.

#include "PMHudWidget.h"
#include "PMActiveRagdollComponent.h"
#include "PMPromptParser.h"
#include "PMSpellcasterComponent.h"
#include "PMSpellSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableText.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"

namespace PMHud
{
	const FLinearColor Ink(0.17f, 0.1f, 0.05f);
	const FLinearColor Faint(0.45f, 0.36f, 0.25f);
	const FLinearColor Danger(0.75f, 0.05f, 0.03f);
	const FLinearColor Good(0.05f, 0.35f, 0.15f);
	const FLinearColor Parchment(0.93f, 0.86f, 0.66f, 0.9f);
	const FLinearColor HudWhite(1.f, 1.f, 1.f, 0.95f);
}

void UPMHudWidget::InitHud(UPMSpellcasterComponent* InCaster)
{
	Caster = InCaster;
	if (UPMSpellSubsystem* Spells = GetWorld() ? GetWorld()->GetSubsystem<UPMSpellSubsystem>() : nullptr)
	{
		ToastHandle = Spells->OnToast.AddUObject(this, &UPMHudWidget::AddToast);
	}
}

void UPMHudWidget::NativeDestruct()
{
	if (UPMSpellSubsystem* Spells = GetWorld() ? GetWorld()->GetSubsystem<UPMSpellSubsystem>() : nullptr)
	{
		Spells->OnToast.Remove(ToastHandle);
	}
	Super::NativeDestruct();
}

TSharedRef<SWidget> UPMHudWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

UTextBlock* UPMHudWidget::MakeText(const FString& Text, int32 Size, const FLinearColor& Color)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Block->SetText(FText::FromString(Text));
	FSlateFontInfo Font = Block->GetFont();
	Font.Size = Size;
	Block->SetFont(Font);
	Block->SetColorAndOpacity(FSlateColor(Color));
	Block->SetAutoWrapText(true);
	Block->SetVisibility(ESlateVisibility::HitTestInvisible);
	return Block;
}

void UPMHudWidget::BuildTree()
{
	using namespace PMHud;

	Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;
	// The HUD must never eat mouse clicks meant for the game (grabbing). Only the Spellbook is interactive.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	auto Place = [this](UWidget* Widget, const FAnchors& Anchors, const FVector2D& Alignment, const FMargin& Offsets, bool bAutoSize)
	{
		UCanvasPanelSlot* CanvasSlot = Root->AddChildToCanvas(Widget);
		CanvasSlot->SetAnchors(Anchors);
		CanvasSlot->SetAlignment(Alignment);
		CanvasSlot->SetOffsets(Offsets);
		CanvasSlot->SetAutoSize(bAutoSize);
		return CanvasSlot;
	};

	// Crosshair.
	Crosshair = MakeText(TEXT("+"), 22, HudWhite);
	Crosshair->SetAutoWrapText(false);
	Crosshair->SetShadowOffset(FVector2D(1.f, 1.f));
	Place(Crosshair, FAnchors(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FMargin(0.f), true);

	// Cooldown + grip under the crosshair.
	CooldownText = MakeText(FString(), 14, HudWhite);
	CooldownText->SetAutoWrapText(false);
	CooldownText->SetShadowOffset(FVector2D(1.f, 1.f));
	Place(CooldownText, FAnchors(0.5f, 0.5f), FVector2D(0.5f, 0.f), FMargin(0.f, 24.f, 0.f, 0.f), true);

	// One grip-stamina bar per hand, either side of the crosshair (left hand = LMB, right hand = RMB).
	for (int32 i = 0; i < 2; ++i)
	{
		GripBars[i] = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
		GripBars[i]->SetFillColorAndOpacity(FLinearColor(0.95f, 0.75f, 0.2f));
		GripBars[i]->SetBarFillType(EProgressBarFillType::BottomToTop);
		GripBars[i]->SetVisibility(ESlateVisibility::Collapsed);
		const float X = i == 0 ? -46.f : 46.f;
		Place(GripBars[i], FAnchors(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FMargin(X, 0.f, 8.f, 56.f), false);
	}

	// What each hand is doing, under the crosshair.
	HandText = MakeText(FString(), 13, HudWhite);
	HandText->SetAutoWrapText(false);
	HandText->SetShadowOffset(FVector2D(1.f, 1.f));
	Place(HandText, FAnchors(0.5f, 0.5f), FVector2D(0.5f, 0.f), FMargin(0.f, 46.f, 0.f, 0.f), true);

	// Team status, top left.
	StatusText = MakeText(FString(), 18, HudWhite);
	StatusText->SetAutoWrapText(false);
	StatusText->SetShadowOffset(FVector2D(1.f, 1.f));
	Place(StatusText, FAnchors(0.f, 0.f), FVector2D(0.f, 0.f), FMargin(24.f, 20.f, 0.f, 0.f), true);

	// Controls hint, bottom left.
	HelpText = MakeText(TEXT("Tab  Spellbook     Hold LMB / RMB  Left / right hand     Both + look up  Climb     Wheel  Reach     Shift  Sprint"), 13, FLinearColor(1.f, 1.f, 1.f, 0.6f));
	HelpText->SetAutoWrapText(false);
	HelpText->SetShadowOffset(FVector2D(1.f, 1.f));
	Place(HelpText, FAnchors(0.f, 1.f), FVector2D(0.f, 1.f), FMargin(24.f, -16.f, 0.f, 0.f), true);

	// Messages, top centre.
	ToastText = MakeText(FString(), 17, FLinearColor(1.f, 0.93f, 0.7f));
	ToastText->SetAutoWrapText(false); // An auto-sized slot gives wrapping no width, so it would wrap every word.
	ToastText->SetJustification(ETextJustify::Center);
	ToastText->SetShadowOffset(FVector2D(1.f, 1.f));
	Place(ToastText, FAnchors(0.5f, 0.f), FVector2D(0.5f, 0.f), FMargin(0.f, 70.f, 900.f, 0.f), false)->SetAutoSize(true);

	// --- Spellbook parchment: lower 35% of the screen. ---
	Spellbook = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Spellbook"));
	Spellbook->SetBrushColor(Parchment);
	Spellbook->SetPadding(FMargin(28.f, 16.f));
	Spellbook->SetVisibility(ESlateVisibility::Collapsed);
	Place(Spellbook, FAnchors(0.12f, 0.65f, 0.88f, 1.f), FVector2D(0.f, 0.f), FMargin(0.f, 0.f, 0.f, 12.f), false);

	UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Spellbook->SetContent(Page);

	auto AddRow = [Page](UWidget* Widget, float TopPad)
	{
		UVerticalBoxSlot* RowSlot = Page->AddChildToVerticalBox(Widget);
		RowSlot->SetPadding(FMargin(0.f, TopPad, 0.f, 0.f));
		return RowSlot;
	};

	UTextBlock* Title = MakeText(TEXT("SPELLBOOK      Enter  cast     Esc / Tab  close     Up  last prompts     Right  accept suggestion"), 13, Faint);
	AddRow(Title, 0.f);

	// Prompt line + cost meter.
	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	AddRow(Line, 6.f);

	UBorder* LineBack = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	LineBack->SetBrushColor(FLinearColor(1.f, 0.97f, 0.88f, 0.75f));
	LineBack->SetPadding(FMargin(12.f, 6.f));
	UHorizontalBoxSlot* LineSlot = Line->AddChildToHorizontalBox(LineBack);
	LineSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LineSlot->SetVerticalAlignment(VAlign_Center);

	PromptText = WidgetTree->ConstructWidget<UEditableText>(UEditableText::StaticClass(), TEXT("PromptLine"));
	PromptText->SetHintText(FText::FromString(TEXT("a floating barrel...")));
	{
		FEditableTextStyle Style = PromptText->WidgetStyle;
		Style.SetColorAndOpacity(FSlateColor(Ink));
		Style.Font.Size = 28;
		PromptText->SetWidgetStyle(Style);
	}
	PromptText->OnTextChanged.AddDynamic(this, &UPMHudWidget::HandlePromptChanged);
	LineBack->SetContent(PromptText);

	CostText = MakeText(FString(), 22, Ink);
	CostText->SetAutoWrapText(false);
	UHorizontalBoxSlot* CostSlot = Line->AddChildToHorizontalBox(CostText);
	CostSlot->SetPadding(FMargin(18.f, 0.f, 0.f, 0.f));
	CostSlot->SetVerticalAlignment(VAlign_Center);

	SuggestionText = MakeText(FString(), 15, Faint);
	AddRow(SuggestionText, 4.f);

	ChipsText = MakeText(FString(), 16, Good);
	AddRow(ChipsText, 4.f);

	WarningText = MakeText(FString(), 16, Danger);
	AddRow(WarningText, 2.f);

	FeedText = MakeText(FString(), 14, Faint);
	AddRow(FeedText, 8.f);
}

TSharedPtr<SWidget> UPMHudWidget::GetPromptFocusWidget()
{
	return PromptText ? PromptText->GetCachedWidget() : nullptr;
}

void UPMHudWidget::ShowSpellbook(bool bShow)
{
	if (!Spellbook)
	{
		return;
	}
	Spellbook->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bShow)
	{
		SetPromptText(FString());
		if (PromptText)
		{
			PromptText->SetKeyboardFocus();
		}
	}
}

void UPMHudWidget::SetPromptText(const FString& Text)
{
	if (!PromptText)
	{
		return;
	}
	PromptText->SetText(FText::FromString(Text));
	// Put the cursor at the end after a programmatic change.
	if (TSharedPtr<SWidget> Slate = PromptText->GetCachedWidget())
	{
		FSlateApplication::Get().SetKeyboardFocus(Slate, EFocusCause::SetDirectly);
	}
	RefreshPreview();
}

void UPMHudWidget::HandlePromptChanged(const FText& Text)
{
	RefreshPreview();
}

FReply UPMHudWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	UPMSpellcasterComponent* C = Caster.Get();
	if (!C || !C->IsSpellbookOpen())
	{
		return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
	}

	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Tab)
	{
		C->CloseSpellbook();
		return FReply::Handled();
	}
	if (Key == EKeys::Enter)
	{
		C->CastPrompt(PromptText ? PromptText->GetText().ToString() : FString());
		return FReply::Handled();
	}
	if (Key == EKeys::Up || Key == EKeys::Down)
	{
		const FString Recalled = C->RecallPrompt(Key == EKeys::Up ? 1 : -1);
		SetPromptText(Recalled);
		return FReply::Handled();
	}
	if (Key == EKeys::Right && !CurrentSuggestion.IsEmpty() && PromptText)
	{
		SetPromptText(FPMPromptParser::AcceptSuggestion(PromptText->GetText().ToString(), CurrentSuggestion));
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

void UPMHudWidget::RefreshPreview()
{
	using namespace PMHud;

	UPMSpellcasterComponent* C = Caster.Get();
	UPMSpellSubsystem* Spells = GetWorld() ? GetWorld()->GetSubsystem<UPMSpellSubsystem>() : nullptr;
	if (!C || !Spells || !PromptText)
	{
		return;
	}

	const FString Text = PromptText->GetText().ToString();
	const FPMParsedPrompt Prompt = C->PreviewPrompt(Text);

	// Autocomplete.
	CurrentSuggestion = FPMPromptParser::Suggest(Text);
	SuggestionText->SetText(FText::FromString(CurrentSuggestion.IsEmpty() ? FString() : FString::Printf(TEXT("-> %s"), *CurrentSuggestion)));

	if (Prompt.IsEmpty())
	{
		CostText->SetText(FText::FromString(FString::Printf(TEXT("0 / %s Ink"), *FPMPromptParser::FormatInk(Spells->GetInk()))));
		CostText->SetColorAndOpacity(FSlateColor(Ink));
		ChipsText->SetText(FText::FromString(TEXT("Nouns: barrel, anvil, crate, turnip, plank, ball, bucket, chair, cow, hay bale.  Modifiers: bouncy, greasy, floating, heavy, light.  Sizes: tiny ... colossal.")));
		WarningText->SetText(FText::GetEmpty());
		return;
	}

	// Live cost meter against the shared pool.
	const float Cost = C->GetCurrentCost(Prompt);
	const bool bAffordable = Spells->CanAfford(Cost);
	FString CostLine = FString::Printf(TEXT("%s / %s Ink"), *FPMPromptParser::FormatInk(Cost),
		Spells->IsInfiniteInk() ? TEXT("inf") : *FPMPromptParser::FormatInk(Spells->GetInk()));
	if (Cost > Prompt.InkCost + KINDA_SMALL_NUMBER)
	{
		CostLine += TEXT("  (surge pricing)");
	}
	CostText->SetText(FText::FromString(CostLine));
	CostText->SetColorAndOpacity(FSlateColor(bAffordable ? Ink : Danger));

	// Physics preview chips.
	if (Prompt.Noun)
	{
		ChipsText->SetText(FText::FromString(FString::Printf(TEXT("%s:  %s"), *Prompt.DisplayName,
			*FPMPromptParser::DescribeTraits(Prompt.Traits, C->MaxSoloLiftKg))));
	}
	else
	{
		ChipsText->SetText(FText::FromString(TEXT("...needs a noun.")));
	}

	// Warnings.
	TArray<FString> Warnings;
	if (Prompt.BannedWords.Num() > 0)
	{
		Warnings.Add(FString::Printf(TEXT("BANNED: %s"), *FString::Join(Prompt.BannedWords, TEXT(", "))));
	}
	if (Prompt.bMystery)
	{
		Warnings.Add(FString::Printf(TEXT("Unknown word \"%s\" - you'll get a Mystery Crate (half price)."), *Prompt.MisheardWord));
	}
	if (Prompt.ExtraModifierWords.Num() > 0)
	{
		Warnings.Add(FString::Printf(TEXT("Max 3 modifiers - ignoring: %s"), *FString::Join(Prompt.ExtraModifierWords, TEXT(", "))));
	}
	if (Prompt.IgnoredWords.Num() > 0)
	{
		Warnings.Add(FString::Printf(TEXT("Not understood: %s"), *FString::Join(Prompt.IgnoredWords, TEXT(", "))));
	}
	if (!bAffordable)
	{
		Warnings.Add(TEXT("Not enough Ink."));
	}
	WarningText->SetText(FText::FromString(FString::Join(Warnings, TEXT("     "))));
}

void UPMHudWidget::AddToast(const FString& Message)
{
	FToast& T = Toasts.AddDefaulted_GetRef();
	T.Text = Message;
	T.Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Toasts.Num() > MaxToasts)
	{
		Toasts.RemoveAt(0);
	}
}

void UPMHudWidget::RefreshStatus()
{
	UPMSpellcasterComponent* C = Caster.Get();
	UPMSpellSubsystem* Spells = GetWorld() ? GetWorld()->GetSubsystem<UPMSpellSubsystem>() : nullptr;
	if (!C || !Spells)
	{
		return;
	}

	// Team Ink + who spent what (GDD 3.4).
	FString Status = Spells->IsInfiniteInk()
		? TEXT("INK  infinite")
		: FString::Printf(TEXT("INK  %s / %s"), *FPMPromptParser::FormatInk(Spells->GetInk()), *FPMPromptParser::FormatInk(Spells->GetMaxInk()));
	for (const TPair<FString, float>& Pair : Spells->GetInkByPlayer())
	{
		Status += FString::Printf(TEXT("\n   %s spent %s"), *Pair.Key, *FPMPromptParser::FormatInk(Pair.Value));
	}
	Status += FString::Printf(TEXT("\nOBJECTS  %d / %d"), Spells->GetLiveObjectCount(), Spells->GetMaxObjects());
	StatusText->SetText(FText::FromString(Status));

	const float Cooldown = C->GetCooldownRemaining();
	CooldownText->SetText(Cooldown > 0.f ? FText::FromString(FString::Printf(TEXT("quill %.1fs"), Cooldown)) : FText::GetEmpty());

	// Hands: grip stamina bars and a short status line.
	const UPMActiveRagdollComponent* Body = C->GetOwner()->FindComponentByClass<UPMActiveRagdollComponent>();
	TArray<FString> HandStates;
	for (int32 i = 0; i < 2; ++i)
	{
		const EPMArm Arm = i == 0 ? EPMArm::Left : EPMArm::Right;
		const bool bGripping = Body && Body->IsGripping(Arm);
		const float Stamina = Body ? Body->GetGripStamina(Arm) : 1.f;
		const bool bShow = bGripping || Stamina < 0.999f;
		GripBars[i]->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		GripBars[i]->SetPercent(Stamina);
		GripBars[i]->SetFillColorAndOpacity(Stamina < 0.3f ? FLinearColor(0.9f, 0.15f, 0.1f) : FLinearColor(0.95f, 0.75f, 0.2f));
		if (bGripping)
		{
			HandStates.Add(FString::Printf(TEXT("%s: holding%s"), i == 0 ? TEXT("L") : TEXT("R"),
				Body->GetLoad(Arm) > 1.f ? TEXT(" (too heavy!)") : TEXT("")));
		}
	}
	if (Body && Body->IsClimbing())
	{
		HandStates.Add(TEXT("climbing"));
	}
	HandText->SetText(FText::FromString(FString::Join(HandStates, TEXT("   "))));

	// Messages fade out after a few seconds.
	const double Now = GetWorld()->GetTimeSeconds();
	Toasts.RemoveAll([Now](const FToast& T) { return Now - T.Time > ToastSeconds; });
	TArray<FString> Lines;
	for (const FToast& T : Toasts)
	{
		Lines.Add(T.Text);
	}
	ToastText->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));

	// Team feed: the last three casts.
	if (C->IsSpellbookOpen())
	{
		TArray<FString> Feed;
		for (const FPMCastRecord& R : Spells->GetFeed())
		{
			Feed.Add(FString::Printf(TEXT("%s: %s (%s Ink)"), *R.CasterName, *R.Description, *FPMPromptParser::FormatInk(R.Ink)));
		}
		FeedText->SetText(FText::FromString(Feed.Num() > 0 ? TEXT("Recent casts:   ") + FString::Join(Feed, TEXT("   |   ")) : FString()));
	}
}

void UPMHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshStatus();

	// Ink can change from other casts while the book is open.
	if (Caster.IsValid() && Caster->IsSpellbookOpen())
	{
		RefreshPreview();
	}
}
