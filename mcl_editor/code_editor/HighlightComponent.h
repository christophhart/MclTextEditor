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
class mcl::HighlightComponent : public juce::Component
{
public:
	HighlightComponent(const TextDocument& document);
	void setViewTransform(const juce::AffineTransform& transformToUse);
	void updateSelections();

	//==========================================================================
	void paintHighlight(juce::Graphics& g);

private:
	juce::Path getOutlinePath(const Selection& rectangles, Rectangle<float> clip) const;

	RectangleList<float> outlineRects;

	//==========================================================================
	bool useRoundedHighlight = true;
	const TextDocument& document;
	juce::AffineTransform transform;
	juce::Path outlinePath;
};

}