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

namespace mcl
{
using namespace juce;


//==========================================================================
mcl::HighlightComponent::HighlightComponent(TextDocument& document) : document(document)
{
	document.addFoldListener(this);
	setInterceptsMouseClicks(false, false);
}

void mcl::HighlightComponent::setViewTransform(const AffineTransform& transformToUse)
{
	transform = transformToUse;

	outlinePath.clear();
	auto clip = getLocalBounds().toFloat().transformedBy(transform.inverted());

	for (const auto& s : document.getSelections())
	{
		outlinePath.addPath(getOutlinePath(s, clip));
	}
	repaint(outlinePath.getBounds().getSmallestIntegerContainer());
}

void mcl::HighlightComponent::updateSelections()
{
	outlinePath.clear();
	auto clip = getLocalBounds().toFloat().transformedBy(transform.inverted());

	for (const auto& s : document.getSelections())
	{
		outlinePath.addPath(getOutlinePath(s.oriented(), clip));
	}
	repaint(outlinePath.getBounds().getSmallestIntegerContainer());
}

void mcl::HighlightComponent::paintHighlight(Graphics& g)
{
	//g.addTransform(transform);
	auto highlight = getParentComponent()->findColour(CodeEditorComponent::highlightColourId);
	g.setColour(highlight);

	DropShadow sh;
	sh.colour = Colours::black.withAlpha(0.4f);
	sh.radius = 8;
	sh.offset = { 0, 3 };
	//sh.drawForPath(g, outlinePath);

	auto c = highlight.withAlpha(1.0f);

	auto b = outlinePath.getBounds();

	g.setGradientFill(ColourGradient(c, 0.0f, b.getY(), c.darker(0.05f), 0.0f, b.getBottom(), false));
	g.fillPath(outlinePath);

	g.setColour(Colour(0xff959595));
	g.strokePath(outlinePath, PathStrokeType(1.f));

	for (auto sr : document.getSearchResults())
	{
		auto r = document.getSelectionRegion(sr);

		for (auto h : r)
		{
			h.removeFromBottom(h.getHeight() * 0.15f);

			h = h.translated(0.0f, h.getHeight() * 0.05f).expanded(2.0f);

			g.setColour(Colours::white.withAlpha(0.2f));
			g.fillRoundedRectangle(h, 2.0f);
			g.setColour(Colours::red.withAlpha(0.4f));
			g.drawRoundedRectangle(h, 2.0f, 1.0f);
		}
	}
}

Path mcl::HighlightComponent::getOutlinePath(const Selection& s, Rectangle<float> clip) const
{
	RectangleList<float> list;
	auto top = document.getUnderlines(s, TextDocument::Metric::top);
	auto bottom = document.getUnderlines(s, TextDocument::Metric::baseline);
	Path p;

	if (top.isEmpty())
		return p;

	float currentPos = 0.0f;

	auto pushed = [&currentPos](Point<float> p, bool down)
	{
		if (down)
			p.y = jmax(currentPos, p.y);
		else
			p.y = jmin(currentPos, p.y);

		currentPos = p.y;
		return p;
	};

	float deltaY = -1.0f;

	p.startNewSubPath(pushed(top.getFirst().getEnd().translated(0.0f, deltaY), true));
	p.lineTo(pushed(bottom.getFirst().getEnd().translated(0.0f, deltaY), true));

	for (int i = 1; i < top.size(); i++)
	{
		p.lineTo(pushed(top[i].getEnd().translated(0.0f, deltaY), true));
		auto b = pushed(bottom[i].getEnd().translated(0.0f, deltaY), true);
		p.lineTo(b);
	}

	for (int i = top.size() - 1; i >= 0; i--)
	{
		p.lineTo(pushed(bottom[i].getStart().translated(0.0f, deltaY), false));
		p.lineTo(pushed(top[i].getStart().translated(0.0f, deltaY), false));
	}

	p.closeSubPath();
	return p.createPathWithRoundedCorners(1.0f);
}

mcl::HighlightComponent::~HighlightComponent()
{
	document.removeFoldListener(this);
}

void SearchBoxComponent::Blaf::drawButtonText(Graphics& g, TextButton& b, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
	g.setFont(Font("Oxygen", 13.0f, Font::bold));
	g.setColour(Colours::black);
	g.drawText(b.getButtonText(), b.getLocalBounds().toFloat(), Justification::centred);
}

}