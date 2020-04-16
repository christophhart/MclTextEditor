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
mcl::GutterComponent::GutterComponent(const TextDocument& document)
	: document(document)
	, memoizedGlyphArrangements([this](int row) { return getLineNumberGlyphs(row); })
{
	setInterceptsMouseClicks(false, false);
}

void mcl::GutterComponent::setViewTransform(const AffineTransform& transformToUse)
{
	transform = transformToUse;
	repaint();
}

void mcl::GutterComponent::updateSelections()
{
	repaint();
}

void mcl::GutterComponent::paint(Graphics& g)
{
#if PROFILE_PAINTS
	auto start = Time::getMillisecondCounterHiRes();
#endif

	/*
	 Draw the gutter background, shadow, and outline
	 ------------------------------------------------------------------
	 */
	auto bg = getParentComponent()->findColour(CodeEditorComponent::backgroundColourId);
	auto ln = bg.overlaidWith(getParentComponent()->findColour(CodeEditorComponent::lineNumberBackgroundId));

	auto GUTTER_WIDTH = getGutterWidth();

	g.setColour(ln);
	g.fillRect(getLocalBounds().removeFromLeft(GUTTER_WIDTH));

	if (transform.getTranslationX() < GUTTER_WIDTH)
	{
		auto shadowRect = getLocalBounds().withLeft(GUTTER_WIDTH).withWidth(12);

		auto gradient = ColourGradient::horizontal(ln.contrasting().withAlpha(0.3f),
			Colours::transparentBlack, shadowRect);
		g.setFillType(gradient);
		g.fillRect(shadowRect);
	}
	else
	{
		g.setColour(ln.darker(0.2f));
		g.drawVerticalLine(GUTTER_WIDTH - 1.f, 0.f, getHeight());
	}

	/*
	 Draw the line numbers and selected rows
	 ------------------------------------------------------------------
	 */
	auto area = g.getClipBounds().toFloat().transformedBy(transform.inverted());
	auto rowData = document.findRowsIntersecting(area);
	auto verticalTransform = transform.withAbsoluteTranslation(0.f, transform.getTranslationY());

	g.setColour(ln.contrasting(0.1f));

	for (const auto& r : rowData)
	{
		bool isErrorLine = r.rowNumber == errorLine;

		if (r.isRowSelected || isErrorLine)
		{
			g.setColour(ln.contrasting(0.1f));
			auto A = r.bounds.getRectangle(0)
				.transformedBy(transform)
				.withX(0)
				.withWidth(GUTTER_WIDTH);

			

			g.fillRect(A);

			if (isErrorLine)
			{
				auto c = Colour(0xff8f1a1a);
				g.setColour(c);
				g.fillRect(A.removeFromRight(3.0f));
			}
				
		}
	}

	g.setColour(getParentComponent()->findColour(CodeEditorComponent::lineNumberTextId));

	for (const auto& r : rowData)
	{
		auto A = r.bounds.getRectangle(0)
			.transformedBy(transform)
			.withX(0)
			.withWidth(GUTTER_WIDTH);

		auto f = document.getFont();

		auto gap = (document.getRowHeight() - f.getHeight() * 0.8f) / 2.0f * transform.getScaleFactor();

		f.setHeight(f.getHeight() * transform.getScaleFactor() * 0.8f);
		g.setFont(f);






		g.drawText(String(r.rowNumber + 1), A.reduced(5.0f, gap), Justification::topRight, false);

		//memoizedGlyphArrangements (r.rowNumber).draw(g, verticalTransform);
	}

#if PROFILE_PAINTS
	std::cout << "[GutterComponent::paint] " << Time::getMillisecondCounterHiRes() - start << std::endl;
#endif
}

GlyphArrangement mcl::GutterComponent::getLineNumberGlyphs(int row) const
{
	GlyphArrangement glyphs;
	glyphs.addLineOfText(document.getFont().withHeight(12.f),
		String(row + 1),
		8.f, document.getVerticalPosition(row, TextDocument::Metric::baseline));
	return glyphs;
}





}