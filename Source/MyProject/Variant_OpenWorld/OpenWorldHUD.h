// Canvas HUD for the random open world game.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "OpenWorldHUD.generated.h"

class UFont;

/**
 *  Draws the crystal counter, timer, seed, status messages,
 *  a compass hint to the nearest crystal and a minimap.
 */
UCLASS()
class AOpenWorldHUD : public AHUD
{
	GENERATED_BODY()

public:

	virtual void DrawHUD() override;

protected:

	void DrawShadowedText(const FString& Text, float X, float Y, const FLinearColor& Color, UFont* Font, float Scale);
	void DrawCenteredText(const FString& Text, float Y, const FLinearColor& Color, UFont* Font, float Scale);
	void DrawMinimap(float UIScale);
	void DrawVitals(float UIScale);
	void DrawCrosshair(float UIScale);
};
