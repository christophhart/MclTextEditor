/** ============================================================================
 *
 * MCL Text Editor JUCE module 
 *
 * Copyright (C) Jonathan Zrake, Christoph Hart
 *
 * You may use, distribute and modify this code under the terms of the GPL3
 * license.
 * =============================================================================
 */


#pragma once

namespace mcl
{
using namespace juce;



//==============================================================================
class mcl::GutterComponent : public juce::Component
{
public:
	GutterComponent(const TextDocument& document);
	void setViewTransform(const juce::AffineTransform& transformToUse);
	void updateSelections();

	float getGutterWidth() const
	{
		auto w = document.getCharacterRectangle().getWidth() * 6;

		return w * scaleFactor;
	}

	//==========================================================================
	void paint(juce::Graphics& g) override;

	void setScaleFactor(float newFactor)
	{
		scaleFactor = newFactor;
		repaint();
	}

	void setError(int lineNumber, const String& error)
	{
		errorLine = lineNumber;
		errorMessage = error;
		repaint();
	}

private:

	int errorLine;
	String errorMessage;

	float scaleFactor = 1.0f;

	juce::GlyphArrangement getLineNumberGlyphs(int row) const;
	//==========================================================================
	const TextDocument& document;
	juce::AffineTransform transform;
	Memoizer<int, juce::GlyphArrangement> memoizedGlyphArrangements;
};





}