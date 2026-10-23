// Prompt-Mages: turns typed text into one noun + up to three modifiers (GDD 3.1).

#include "PMPromptParser.h"
#include "PMObjectLibrary.h"

namespace PMParse
{
	/** Very small plural stripper: "barrels" -> "barrel", "boxes" -> "box". */
	FString Singular(const FString& Word)
	{
		if (Word.Len() > 3 && Word.EndsWith(TEXT("es")))
		{
			const FString Short = Word.LeftChop(2);
			if (FPMObjectLibrary::Get().FindNoun(Short))
			{
				return Short;
			}
		}
		if (Word.Len() > 2 && Word.EndsWith(TEXT("s")))
		{
			return Word.LeftChop(1);
		}
		return Word;
	}
}

TArray<FString> FPMPromptParser::Tokenize(const FString& Text)
{
	FString Clean = Text.ToLower();
	// GDD 3.1 lists "big ol'" as filler; drop the phrase before "big" can read as a size word.
	Clean.ReplaceInline(TEXT("big ol'"), TEXT(" "));
	Clean.ReplaceInline(TEXT("big ol"), TEXT(" "));

	for (TCHAR& C : Clean)
	{
		if (!FChar::IsAlnum(C))
		{
			C = TEXT(' ');
		}
	}

	TArray<FString> Tokens;
	Clean.ParseIntoArray(Tokens, TEXT(" "), true);
	return Tokens;
}

FPMParsedPrompt FPMPromptParser::Parse(const FString& Text, const TArray<FString>& BannedWords)
{
	const FPMObjectLibrary& Lib = FPMObjectLibrary::Get();

	FPMParsedPrompt Out;
	Out.RawText = Text;

	TArray<const FPMModifierDef*> ModifierOrder;
	TArray<FString> ModifierWords;
	TArray<FString> Unknown;
	int32 ExplicitQuantity = 0;
	bool bPlural = false;

	for (const FString& Token : Tokenize(Text))
	{
		const FString Single = PMParse::Singular(Token);

		if (BannedWords.Contains(Token) || BannedWords.Contains(Single))
		{
			Out.BannedWords.AddUnique(Token);
			continue;
		}
		if (Lib.IsFiller(Token))
		{
			continue;
		}
		if (const int32 Q = Lib.FindQuantity(Token))
		{
			ExplicitQuantity = Q;
			continue;
		}
		if (const FPMNounDef* Noun = Lib.FindNoun(Token))
		{
			// The last recognised noun wins (GDD 3.1 step 2).
			Out.Noun = Noun;
			bPlural = false;
			continue;
		}
		if (Single != Token)
		{
			if (const FPMNounDef* Noun = Lib.FindNoun(Single))
			{
				Out.Noun = Noun;
				bPlural = true;
				continue;
			}
		}
		if (const FPMModifierDef* Mod = Lib.FindModifier(Token))
		{
			ModifierOrder.Add(Mod);
			ModifierWords.Add(Token);
			continue;
		}
		if (const FPMSizeDef* Size = Lib.FindSize(Token))
		{
			Out.Size = Size;
			continue;
		}
		Unknown.Add(Token);
	}

	// Unknown nouns spawn a Mystery Crate (GDD 3.5).
	if (!Out.Noun && Unknown.Num() > 0)
	{
		Out.bMystery = true;
		Out.MisheardWord = Unknown.Pop();
		Out.Noun = &Lib.GetMysteryCrate();
	}
	Out.IgnoredWords = Unknown;

	for (int32 i = 0; i < ModifierOrder.Num(); ++i)
	{
		if (i < MaxModifiers)
		{
			Out.Modifiers.Add(ModifierOrder[i]);
		}
		else
		{
			Out.ExtraModifierWords.Add(ModifierWords[i]);
		}
	}

	Out.Quantity = FMath::Clamp(ExplicitQuantity > 0 ? ExplicitQuantity : (bPlural ? DefaultPluralQuantity : 1), 1, MaxQuantity);

	if (!Out.Noun)
	{
		return Out;
	}

	// Traits: base noun, then modifiers left to right (last one wins conflicts), then size.
	Out.Traits = Out.Noun->Traits;
	for (const FPMModifierDef* Mod : Out.Modifiers)
	{
		Mod->Apply(Out.Traits);
	}
	if (Out.Size)
	{
		Out.SizeScale = Out.Size->Scale;
		Out.Traits.WeightKg *= FMath::Pow(Out.Size->Scale, 3.f);
	}
	Out.Traits.Clamp();

	// Ink: base + modifiers; the third modifier doubles the total (GDD 3.3 stacking rules).
	float Ink = Out.Noun->InkCost;
	for (const FPMModifierDef* Mod : Out.Modifiers)
	{
		Ink += Mod->InkCost;
	}
	if (Out.Modifiers.Num() >= MaxModifiers)
	{
		Ink *= 2.f;
	}
	if (Out.Size)
	{
		Ink *= Out.Size->InkMultiplier;
	}
	Ink *= Out.Quantity;
	if (Out.bMystery)
	{
		Ink *= 0.5f; // Mystery Crates refund half.
	}
	Out.InkCost = FMath::RoundToFloat(Ink * 10.f) / 10.f;

	// Display name and canonical key.
	TArray<FString> Parts;
	TArray<FString> KeyParts;
	if (Out.Size)
	{
		Parts.Add(Out.Size->DisplayName);
		KeyParts.Add(Out.Size->DisplayName);
	}
	for (const FPMModifierDef* Mod : Out.Modifiers)
	{
		Parts.Add(Mod->DisplayName);
		KeyParts.Add(Mod->Id.ToString());
	}
	Parts.Add(Out.Noun->DisplayName);
	KeyParts.Add(Out.bMystery ? Out.MisheardWord : Out.Noun->Id.ToString());
	KeyParts.Add(FString::FromInt(Out.Quantity));

	Out.DisplayName = FString::Join(Parts, TEXT(" "));
	if (Out.bMystery)
	{
		Out.DisplayName += FString::Printf(TEXT(" (\"%s\")"), *Out.MisheardWord);
	}
	if (Out.Quantity > 1)
	{
		Out.DisplayName = FString::Printf(TEXT("%d x %s"), Out.Quantity, *Out.DisplayName);
	}
	Out.Key = FString::Join(KeyParts, TEXT("|")).ToLower();

	return Out;
}

FString FPMPromptParser::Suggest(const FString& Text)
{
	if (Text.IsEmpty() || !FChar::IsAlpha(Text[Text.Len() - 1]))
	{
		return FString();
	}

	int32 Start = Text.Len() - 1;
	while (Start > 0 && FChar::IsAlpha(Text[Start - 1]))
	{
		--Start;
	}
	const FString Partial = Text.Mid(Start).ToLower();
	if (Partial.Len() < 2)
	{
		return FString();
	}

	for (const FString& Word : FPMObjectLibrary::Get().GetVocabulary())
	{
		if (Word.Len() > Partial.Len() && Word.StartsWith(Partial))
		{
			return Word;
		}
	}
	return FString();
}

FString FPMPromptParser::AcceptSuggestion(const FString& Text, const FString& Suggestion)
{
	int32 Start = Text.Len();
	while (Start > 0 && FChar::IsAlpha(Text[Start - 1]))
	{
		--Start;
	}
	return Text.Left(Start) + Suggestion + TEXT(" ");
}

FString FPMPromptParser::FormatInk(float Ink)
{
	return FMath::IsNearlyEqual(Ink, FMath::RoundToFloat(Ink)) ? FString::FromInt(FMath::RoundToInt(Ink)) : FString::Printf(TEXT("%.1f"), Ink);
}

FString FPMPromptParser::FormatWeight(float Kg)
{
	if (Kg >= 1000.f)
	{
		return FString::Printf(TEXT("%.1f t"), Kg / 1000.f);
	}
	if (Kg < 1.f)
	{
		return FString::Printf(TEXT("%.1f kg"), Kg);
	}
	return FString::Printf(TEXT("%d kg"), FMath::RoundToInt(Kg));
}

FString FPMPromptParser::DescribeTraits(const FPMTraits& T, float MaxSoloLiftKg)
{
	TArray<FString> Chips;
	Chips.Add(FormatWeight(T.WeightKg));
	if (T.WeightKg > 500.f)
	{
		Chips.Add(TEXT("CRUSHES"));
	}
	else if (T.WeightKg > MaxSoloLiftKg)
	{
		Chips.Add(TEXT("too heavy to lift alone"));
	}
	Chips.Add(T.Elasticity > 1.f ? FString::Printf(TEXT("BOUNCY %.2f"), T.Elasticity) : FString::Printf(TEXT("bounce %.2f"), T.Elasticity));
	Chips.Add(T.FloatsInAir() ? FString::Printf(TEXT("FLOATS (buoyancy %.1f)"), T.Buoyancy) : FString::Printf(TEXT("buoyancy %.1f"), T.Buoyancy));
	if (T.bGreasy)
	{
		Chips.Add(TEXT("GREASY"));
	}
	if (T.bMetal)
	{
		Chips.Add(TEXT("metal"));
	}
	if (T.Flammability >= 2.f)
	{
		Chips.Add(TEXT("flammable"));
	}
	return FString::Join(Chips, TEXT("  |  "));
}
