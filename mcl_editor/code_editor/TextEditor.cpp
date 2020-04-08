/** ============================================================================
 *
 * TextEditor.cpp
 *
 * Copyright (C) Jonathan Zrake
 *
 * You may use, distribute and modify this code under the terms of the GPL3
 * license.
 * =============================================================================
 */

#include "TextEditor.hpp"
using namespace juce;




#define CURSOR_WIDTH 1.5f
#define TEXT_INDENT 6.f
#define TEST_MULTI_CARET_EDITING true
#define TEST_SYNTAX_SUPPORT true

#define PROFILE_PAINTS true
static bool DEBUG_TOKENS = false;




//==============================================================================
mcl::CaretComponent::CaretComponent (const TextDocument& document) : document (document)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (20);
}

void mcl::CaretComponent::setViewTransform (const AffineTransform& transformToUse)
{
    transform = transformToUse;
    repaint();
}

void mcl::CaretComponent::updateSelections()
{
    phase = 0.f;
    repaint();
}

void mcl::CaretComponent::paint (Graphics& g)
{
#if PROFILE_PAINTS
    auto start = Time::getMillisecondCounterHiRes();
#endif

	auto colour = getParentComponent()->findColour(juce::CaretComponent::caretColourId);
	auto outline = colour.contrasting();

	UnblurryGraphics ug(g, *this);

	bool drawCaretLine = document.getNumSelections() == 1 && document.getSelections().getFirst().isSingular();

	for (const auto &r : getCaretRectangles())
	{
		g.setColour(colour.withAlpha(squareWave(phase)));

		auto rf = ug.getRectangleWithFixedPixelWidth(r, 2);
		g.fillRect(rf);

		

		if (drawCaretLine)
		{
			g.setColour(Colours::white.withAlpha(0.08f));
			g.fillRect(r.withX(0.0f).withWidth(getWidth()));
		}
	}

#if PROFILE_PAINTS
    std::cout << "[CaretComponent::paint] " << Time::getMillisecondCounterHiRes() - start << std::endl;
#endif
}

float mcl::CaretComponent::squareWave (float wt) const
{
    const float delta = 0.222f;
    const float A = 1.0;
    return 0.5f + A / 3.14159f * std::atanf (std::cosf (wt) / delta);
}

void mcl::CaretComponent::timerCallback()
{
    phase += 3.2e-1;

    for (const auto &r : getCaretRectangles())
        repaint (r.getSmallestIntegerContainer());
}

Array<Rectangle<float>> mcl::CaretComponent::getCaretRectangles() const
{
    Array<Rectangle<float>> rectangles;

    for (const auto& selection : document.getSelections())
    {
		auto b = document.getGlyphBounds(selection.head, GlyphArrangementArray::ReturnBeyondLastCharacter);

		b = b.removeFromLeft(CURSOR_WIDTH).withSizeKeepingCentre(CURSOR_WIDTH, document.getRowHeight());

		

        rectangles.add (b.translated (selection.head.y == 0 ? 0 : -0.5f * CURSOR_WIDTH, 0.f)
                        .transformedBy (transform)
                        .expanded (0.f, 1.f));
    }
    return rectangles;
}




//==========================================================================
mcl::GutterComponent::GutterComponent (const TextDocument& document)
: document (document)
, memoizedGlyphArrangements ([this] (int row) { return getLineNumberGlyphs (row); })
{
    setInterceptsMouseClicks (false, false);
}

void mcl::GutterComponent::setViewTransform (const AffineTransform& transformToUse)
{
    transform = transformToUse;
    repaint();
}

void mcl::GutterComponent::updateSelections()
{
    repaint();
}

void mcl::GutterComponent::paint (Graphics& g)
{
#if PROFILE_PAINTS
    auto start = Time::getMillisecondCounterHiRes();
#endif

    /*
     Draw the gutter background, shadow, and outline
     ------------------------------------------------------------------
     */
    auto bg = getParentComponent()->findColour (CodeEditorComponent::backgroundColourId);
    auto ln = bg.overlaidWith (getParentComponent()->findColour (CodeEditorComponent::lineNumberBackgroundId));

	auto GUTTER_WIDTH = getGutterWidth();

    g.setColour (ln);
    g.fillRect (getLocalBounds().removeFromLeft (GUTTER_WIDTH));

    if (transform.getTranslationX() < GUTTER_WIDTH)
    {
        auto shadowRect = getLocalBounds().withLeft (GUTTER_WIDTH).withWidth (12);

        auto gradient = ColourGradient::horizontal (ln.contrasting().withAlpha (0.3f),
                                                    Colours::transparentBlack, shadowRect);
        g.setFillType (gradient);
        g.fillRect (shadowRect);
    }
    else
    {
        g.setColour (ln.darker (0.2f));
        g.drawVerticalLine (GUTTER_WIDTH - 1.f, 0.f, getHeight());
    }

    /*
     Draw the line numbers and selected rows
     ------------------------------------------------------------------
     */
    auto area = g.getClipBounds().toFloat().transformedBy (transform.inverted());
    auto rowData = document.findRowsIntersecting (area);
    auto verticalTransform = transform.withAbsoluteTranslation (0.f, transform.getTranslationY());

    g.setColour (ln.contrasting (0.1f));

    for (const auto& r : rowData)
    {
        if (r.isRowSelected)
        {
			g.setColour(ln.contrasting(0.1f));
            auto A = r.bounds.getRectangle(0)
                .transformedBy (transform)
                .withX (0)
                .withWidth (GUTTER_WIDTH);

            g.fillRect (A);

			
        }
    }

    g.setColour (getParentComponent()->findColour (CodeEditorComponent::lineNumberTextId));

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

		
		

		

		g.drawText(String(r.rowNumber+1), A.reduced(5.0f, gap), Justification::topRight, false);

        //memoizedGlyphArrangements (r.rowNumber).draw(g, verticalTransform);
    }

#if PROFILE_PAINTS
    std::cout << "[GutterComponent::paint] " << Time::getMillisecondCounterHiRes() - start << std::endl;
#endif
}

GlyphArrangement mcl::GutterComponent::getLineNumberGlyphs (int row) const
{
    GlyphArrangement glyphs;
    glyphs.addLineOfText (document.getFont().withHeight (12.f),
                          String (row + 1),
                          8.f, document.getVerticalPosition (row, TextDocument::Metric::baseline));
    return glyphs;
}




//==========================================================================
mcl::HighlightComponent::HighlightComponent (const TextDocument& document) : document (document)
{
    setInterceptsMouseClicks (false, false);
}

void mcl::HighlightComponent::setViewTransform (const AffineTransform& transformToUse)
{
    transform = transformToUse;

    outlinePath.clear();
    auto clip = getLocalBounds().toFloat().transformedBy (transform.inverted());
    
    for (const auto& s : document.getSelections())
    {
        outlinePath.addPath (getOutlinePath (s, clip));
    }
    repaint (outlinePath.getBounds().getSmallestIntegerContainer());
}

void mcl::HighlightComponent::updateSelections()
{
    outlinePath.clear();
    auto clip = getLocalBounds().toFloat().transformedBy (transform.inverted());

    for (const auto& s : document.getSelections())
    {
        outlinePath.addPath (getOutlinePath (s.oriented(), clip));
    }
    repaint (outlinePath.getBounds().getSmallestIntegerContainer());
}

void mcl::HighlightComponent::paint (Graphics& g)
{
#if PROFILE_PAINTS
    auto start = Time::getMillisecondCounterHiRes();
#endif


    g.addTransform (transform);
    auto highlight = getParentComponent()->findColour (CodeEditorComponent::highlightColourId);

	g.setColour(Colours::red.withAlpha(0.1f));
	g.fillRectList(outlineRects);

	g.setColour(Colours::white.withAlpha(0.4f));
	g.strokePath(outlinePath, PathStrokeType(1.f));

    g.setColour (highlight);

	

#if 0

	UnblurryGraphics ug(g, *this);
	for (auto& r : outlineRects)
	{
		ug.fillUnblurryRect(r);
	}
#endif

	

    g.fillPath (outlinePath);

    

#if PROFILE_PAINTS
    std::cout << "[HighlightComponent::paint] " << Time::getMillisecondCounterHiRes() - start << std::endl;
#endif
}

Path mcl::HighlightComponent::getOutlinePath (const Selection& s, Rectangle<float> clip) const
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

	p.startNewSubPath(pushed(top.getFirst().getEnd(), true));
	p.lineTo(pushed(bottom.getFirst().getEnd(), true));

	

	for (int i = 1; i < top.size(); i++)
	{
		p.lineTo(pushed(top[i].getEnd(), true));
		auto b = pushed(bottom[i].getEnd(), true);
		p.lineTo(b);
	}

	for (int i = top.size() - 1; i >= 0; i--)
	{
		p.lineTo(pushed(bottom[i].getStart(), false));
		p.lineTo(pushed(top[i].getStart(), false));
	}

	p.closeSubPath();

	return p.createPathWithRoundedCorners(2.0f);

	
	




#if 0
	Array<Rectangle<float>> list;

	for (auto& r : rectangles)
	{
		if (r.intersects(clip))
			list.add(r);
	}

	struct Sorter
	{
		static int compareElements(const Rectangle<float>& first, const Rectangle<float>& second)
		{
			if (first.getY() > second.getY())
				return 1;
			if (first.getY() < second.getY())
				return -1;
			
			if (first.getX() > second.getX())
				return 1;
			if (first.getX() < second.getX())
				return -1;

			return 0;
		}
	};

	Sorter s;
	list.sort(s);
	
	Array<Point<float>> leftBounds, rightBounds;

	
	float lastPos = 0.0f;

	for (auto& r : list)
	{
		auto thisPos = r.getY();

		//jassert(thisPos != lastPos);

		auto newPoint = r.getTopRight();

		if(!rightBounds.isEmpty())
			newPoint.setY(jmax(newPoint.getY(), rightBounds.getLast().getY()));

		rightBounds.add(newPoint);

		newPoint = r.getBottomRight();
		newPoint.setY(jmax<float>(newPoint.getY(), rightBounds.getLast().getY()));
		rightBounds.add(newPoint);

		lastPos = thisPos;
	}

	for (int i = list.size() - 1; i >= 0; i--)
	{
		auto r = list[i];

		auto newPoint = r.getBottomLeft();
		
		if(!leftBounds.isEmpty())
			newPoint.setY(jmin(newPoint.getY(), leftBounds.getLast().getY()));

		leftBounds.add(newPoint);

		newPoint = r.getTopLeft();
		newPoint.setY(jmin(newPoint.getY(), leftBounds.getLast().getY()));
		leftBounds.add(newPoint);
	}
	
	Path p;

	p.startNewSubPath(rightBounds.getFirst());

	for (int i = 1; i < rightBounds.size(); i++)
	{
		p.lineTo(rightBounds[i]);
	}

	for (int i = 0; i < leftBounds.size(); i++)
	{
		p.lineTo(leftBounds[i]);
	}

	p.closeSubPath();

	return p.createPathWithRoundedCorners(2.0f);
#endif
}




//==============================================================================
mcl::Selection::Selection (const String& content)
{
    int rowSpan = 0;
    int n = 0, lastLineStart = 0;
    auto c = content.getCharPointer();

    while (*c != '\0')
    {
        if (*c == '\n')
        {
            ++rowSpan;
            lastLineStart = n + 1;
        }
        ++c; ++n;
    }

    head = { 0, 0 };
    tail = { rowSpan, content.length() - lastLineStart };
}

bool mcl::Selection::isOriented() const
{
    return ! (head.x > tail.x || (head.x == tail.x && head.y > tail.y));
}

mcl::Selection mcl::Selection::oriented() const
{
    if (! isOriented())
        return swapped();

    return *this;
}

mcl::Selection mcl::Selection::swapped() const
{
    Selection s = *this;
    std::swap (s.head, s.tail);
    return s;
}

mcl::Selection mcl::Selection::horizontallyMaximized (const TextDocument& document) const
{
    Selection s = *this;

    if (isOriented())
    {
        s.head.y = 0;
        s.tail.y = document.getNumColumns (s.tail.x);
    }
    else
    {
        s.head.y = document.getNumColumns (s.head.x);
        s.tail.y = 0;
    }
    return s;
}

mcl::Selection mcl::Selection::measuring (const String& content) const
{
    Selection s (content);

    if (isOriented())
    {
        return Selection (content).startingFrom (head);
    }
    else
    {
        return Selection (content).startingFrom (tail).swapped();
    }
}

mcl::Selection mcl::Selection::startingFrom (Point<int> index) const
{
    Selection s = *this;

    /*
     Pull the whole selection back to the origin.
     */
    s.pullBy (Selection ({}, isOriented() ? head : tail));

    /*
     Then push it forward to the given index.
     */
    s.pushBy (Selection ({}, index));

    return s;
}

void mcl::Selection::pullBy (Selection disappearingSelection)
{
    disappearingSelection.pull (head);
    disappearingSelection.pull (tail);
}

void mcl::Selection::pushBy (Selection appearingSelection)
{
    appearingSelection.push (head);
    appearingSelection.push (tail);
}

void mcl::Selection::pull (Point<int>& index) const
{
    const auto S = oriented();

    /*
     If the selection tail is on index's row, then shift its column back,
     either by the difference between our head and tail column indexes if
     our head and tail are on the same row, or otherwise by our tail's
     column index.
     */
    if (S.tail.x == index.x && S.head.y <= index.y)
    {
        if (S.head.x == S.tail.x)
        {
            index.y -= S.tail.y - S.head.y;
        }
        else
        {
            index.y -= S.tail.y;
        }
    }

    /*
     If this selection starts on the same row or an earlier one,
     then shift the row index back by our row span.
     */
    if (S.head.x <= index.x)
    {
        index.x -= S.tail.x - S.head.x;
    }
}

void mcl::Selection::push (Point<int>& index) const
{
    const auto S = oriented();

    /*
     If our head is on index's row, then shift its column forward, either
     by our head to tail distance if our head and tail are on the
     same row, or otherwise by our tail's column index.
     */
    if (S.head.x == index.x && S.head.y <= index.y)
    {
        if (S.head.x == S.tail.x)
        {
            index.y += S.tail.y - S.head.y;
        }
        else
        {
            index.y += S.tail.y;
        }
    }

    /*
     If this selection starts on the same row or an earlier one,
     then shift the row index forward by our row span.
     */
    if (S.head.x <= index.x)
    {
        index.x += S.tail.x - S.head.x;
    }
}



struct ActionHelpers
{
	static bool isLeftClosure(juce_wchar c)
	{
		return String("\"({[").containsChar(c);
	};

	static bool  isRightClosure(juce_wchar c)
	{
		return String("\")}]").containsChar(c);
	};

	static bool  isMatchingClosure(juce_wchar l, juce_wchar r)
	{
		return l == '"' && r == '"' ||
			l == '[' && r == ']' ||
			l == '(' && r == ')' ||
			l == '{' && r == '}';
	};
};

//==============================================================================
const String& mcl::GlyphArrangementArray::operator[] (int index) const
{
    if (isPositiveAndBelow (index, lines.size()))
    {
        return lines[index]->string;
    }

    static String empty;
    return empty;
}

int mcl::GlyphArrangementArray::getToken (int row, int col, int defaultIfOutOfBounds) const
{
    if (! isPositiveAndBelow (row, lines.size()))
    {
        return defaultIfOutOfBounds;
    }
    return lines[row]->tokens[col];
}

void mcl::GlyphArrangementArray::clearTokens (int index)
{
    if (! isPositiveAndBelow (index, lines.size()))
        return;

    auto entry = lines[index];

    ensureValid (index);

    for (int col = 0; col < entry->tokens.size(); ++col)
    {
        entry->tokens.setUnchecked (col, 0);
    }
}

void mcl::GlyphArrangementArray::applyTokens (int index, Selection zone)
{
    if (! isPositiveAndBelow (index, lines.size()))
        return;

	auto entry = lines[index];
    auto range = zone.getColumnRangeOnRow (index, entry->tokens.size());

    ensureValid (index);

    for (int col = range.getStart(); col < range.getEnd(); ++col)
    {
        entry->tokens.setUnchecked (col, zone.token);
    }

	entry->tokensAreDirty = false;
}

GlyphArrangement mcl::GlyphArrangementArray::getGlyphs (int index,
                                                        float baseline,
                                                        int token,
                                                        bool withTrailingSpace) const
{
    if (! isPositiveAndBelow (index, lines.size()))
    {
        GlyphArrangement glyphs;

        if (withTrailingSpace)
        {
            glyphs.addLineOfText (font, " ", TEXT_INDENT, baseline);
        }
        return glyphs;
    }
    ensureValid (index);

	auto entry = lines[index];
    auto glyphSource = withTrailingSpace ? entry->glyphsWithTrailingSpace : entry->glyphs;
    auto glyphs = GlyphArrangement();

	

    if (DEBUG_TOKENS)
    {
        String line;
        String hex ("0123456789abcdefg");

        for (auto token : entry->tokens)
            line << hex[token % 16];

        if (withTrailingSpace)
            line << " ";

        glyphSource.clear();
        glyphSource.addLineOfText (font, line, 0.f, 0.f);
    }

	

    for (int n = 0; n < glyphSource.getNumGlyphs(); ++n)
    {
        if (token == -1 || entry->tokens.getUnchecked (n) == token)
        {
            auto glyph = glyphSource.getGlyph (n);
			
			

	        glyph.moveBy (TEXT_INDENT, baseline);

            glyphs.addGlyph (glyph);
        }
    }

    return glyphs;
}



void mcl::GlyphArrangementArray::ensureValid (int index) const
{
    if (! isPositiveAndBelow (index, lines.size()))
        return;

	auto entry = lines[index];

    if (entry->glyphsAreDirty)
    {
		//entry.string = Helpers::replaceTabsWithSpaces(entry.string, 4);

		auto toDraw = entry->string;// ;

        entry->tokens.resize (toDraw.length());
		entry->glyphs.clear();
		entry->glyphsWithTrailingSpace.clear();

		

		if (maxLineWidth != -1)
		{
			entry->glyphs.addJustifiedText(font, toDraw, 0.f, 0.f, maxLineWidth, Justification::centredLeft);
			entry->glyphsWithTrailingSpace.addJustifiedText(font, toDraw + " ", 0.f, 0.f, maxLineWidth, Justification::centredLeft);
		}
		else
		{
			entry->glyphs.addLineOfText (font, toDraw, 0.f, 0.f);
			entry->glyphsWithTrailingSpace.addLineOfText (font, toDraw, 0.f, 0.f);
		}



		entry->positions.clearQuick();
		entry->positions.ensureStorageAllocated(entry->string.length());
		entry->characterBounds = characterRectangle;
		auto n = entry->glyphs.getNumGlyphs();
		auto first = entry->glyphsWithTrailingSpace.getBoundingBox(0, 1, true);

		
		for (int i = 0; i < n; i++)
		{
			auto box = entry->glyphs.getBoundingBox(i, 1, true);
			box = box.translated(-first.getX(), -first.getY());

			float x = box.getY() / characterRectangle.getHeight();
			float y = box.getX() / characterRectangle.getWidth();

			entry->positions.add({ roundToInt(x), roundToInt(y) });
		}

		entry->charactersPerLine.clear();

		int index = 0;

		for (const auto& p : entry->positions)
		{
			auto l = p.x;
			auto characterIsTab = entry->string[index++] == '\t';
			auto c = p.y + 1;

			if (isPositiveAndBelow(l, entry->charactersPerLine.size()))
			{
				auto& thisC = entry->charactersPerLine.getReference(l);
				thisC = jmax(thisC, c);
			}
			else
			{
				entry->charactersPerLine.set(l, c);
			}
		}

		if (entry->charactersPerLine.isEmpty())
			entry->charactersPerLine.add(0);

		entry->glyphsAreDirty = !cacheGlyphArrangement;
		entry->height = font.getHeight() * (float)entry->charactersPerLine.size();
    }
}


void mcl::GlyphArrangementArray::invalidate(Range<int> lineRange)
{
	if (lineRange.isEmpty())
	{
		lineRange = { 0, lines.size() };
	}

	for (int i = lineRange.getStart(); i < lineRange.getEnd() + 1; i++)
	{
		if (isPositiveAndBelow(i, lines.size()))
		{
			lines[i]->tokensAreDirty = true;
			lines[i]->glyphsAreDirty = true;
		}
	}

	for (int i = 0; i < lines.size(); i++)
		ensureValid(i);
}



//==============================================================================
void mcl::TextDocument::replaceAll (const String& content)
{
    lines.clear();

    for (const auto& line : StringArray::fromLines (content))
    {
        lines.add (line);
    }
}

int mcl::TextDocument::getNumRows() const
{
    return lines.size();
}

int mcl::TextDocument::getNumColumns (int row) const
{
    return lines[row].length();
}

float mcl::TextDocument::getVerticalPosition (int row, Metric metric) const
{
	row = jmin(row, lines.size());
	float pos = rowPositions[row];

	
	float gap = font.getHeight() * (lineSpacing - 1.f) * 0.5f;

	
	float lineHeight = getCharacterRectangle().getHeight() + gap;

	if(isPositiveAndBelow(row, lines.size()))
		lineHeight = lines.lines[row]->height + gap;

    switch (metric)
    {
        case Metric::top     : return pos;
		case Metric::ascent: return pos + gap;
		case Metric::baseline: return pos + gap + font.getAscent();
		case Metric::bottom: return pos + lineHeight;
    }
}

Point<float> mcl::TextDocument::getPosition (Point<int> index, Metric metric) const
{
    return Point<float> (getGlyphBounds (index, GlyphArrangementArray::ReturnBeyondLastCharacter).getX(), getVerticalPosition (index.x, metric));
}

RectangleList<float> mcl::TextDocument::getSelectionRegion (Selection selection, Rectangle<float> clip) const
{
    RectangleList<float> patches;
    Selection s = selection.oriented();

	auto m = GlyphArrangementArray::ReturnBeyondLastCharacter;

    if (s.head.x == s.tail.x)
    {
        int c0 = s.head.y;
        int c1 = s.tail.y;

		auto b = getBoundsOnRow(s.head.x, Range<int>(c0, c1), m);

		patches.add(b);
			
    }
    else
    {
        int r0 = s.head.x;
        int c0 = s.head.y;
        int r1 = s.tail.x;
        int c1 = s.tail.y;

        for (int n = r0; n <= r1; ++n)
        {
            if (! clip.isEmpty() &&
                ! clip.getVerticalRange().intersects (
            {
                getVerticalPosition (n, Metric::top),
                getVerticalPosition (n, Metric::bottom)
            })) continue;

            if      (n == r1 && c1 == 0) continue;
            else if (n == r0) patches.add(getBoundsOnRow (r0, Range<int> (c0, getNumColumns (r0) + 1), m));
            else if (n == r1) patches.add(getBoundsOnRow (r1, Range<int> (0, c1), m));
            else              patches.add(getBoundsOnRow (n,  Range<int> (0, getNumColumns (n) + 1), m));
        }
    }
    return patches;
}

Rectangle<float> mcl::TextDocument::getBounds() const
{
    if (cachedBounds.isEmpty())
    {
		int maxX = 0;

		for (auto l : lines.lines)
		{
			for (int i = 0; i < l->charactersPerLine.size(); i++)
			{
				maxX = jmax(maxX, l->charactersPerLine[i]);
			}

		}

		auto bottom = getVerticalPosition(lines.size() - 1, Metric::bottom);
		auto right = maxX * getCharacterRectangle().getWidth() + TEXT_INDENT;

		Rectangle<float> newBounds(0.0f, 0.0f, (float)right, bottom);

#if 0
		RectangleList<float> bounds;

        for (int n = 0; n < getNumRows(); ++n)
        {
			auto b = getBoundsOnRow(n, Range<int>(0, getNumColumns(n)), GlyphArrangementArray::ReturnBeyondLastCharacter);

			if (b.isEmpty())
				bounds.add(getCharacterRectangle().translated(0.0f, getVerticalPosition(n, Metric::top)));
			else
				bounds.add(b);
        }

		auto newBounds = bounds.getBounds();

		

		newBounds = newBounds.withHeight(newBounds.getHeight() + gap/2.0f);
#endif

        return cachedBounds = newBounds;
    }
    return cachedBounds;
}

RectangleList<float> mcl::TextDocument::getBoundsOnRow (int row, Range<int> columns, GlyphArrangementArray::OutOfBoundsMode m) const
{
	RectangleList<float> b;

	if (isPositiveAndBelow(row, getNumRows()))
	{
		columns.setStart(jmax(columns.getStart(), 0));
		auto l = lines.lines[row];

		auto boundsToUse = l->characterBounds;

		if (boundsToUse.isEmpty())
			boundsToUse = { 0.0f, 0.0f, font.getStringWidthFloat(" "), font.getHeight() };

		float yPos = getVerticalPosition(row, Metric::top);
		float xPos = TEXT_INDENT;
		float gap = lineSpacing * font.getHeight() - font.getHeight();

		for (int i = columns.getStart(); i < columns.getEnd(); i++)
		{
			auto p = l->getPositionInLine(i, m);
			auto cBound = boundsToUse.translated(xPos + p.y * boundsToUse.getWidth(), yPos + p.x * boundsToUse.getHeight());

			if (p.x == l->charactersPerLine.size() - 1)
				cBound = cBound.withHeight(cBound.getHeight() + gap);

			bool isTab = l->string[i] == '\t';

			if (isTab)
			{
				int tabLength = 4 - p.y % 4;

				cBound.setWidth((float)tabLength * boundsToUse.getWidth());
			}

			b.add(cBound);
		}

		b.consolidate();
	}

	return b;
}

Rectangle<float> mcl::TextDocument::getGlyphBounds (Point<int> index, GlyphArrangementArray::OutOfBoundsMode m) const
{
#if 0

	auto topOfRow = getVerticalPosition(index.x, Metric::top);

	auto firstBounds = lines.lines[index.x].characterBounds;

	auto b = getGlyphsForRow(index.x, -1, true).getBoundingBox(index.y, 1, true);

	if (index.y == getNumColumns(index.x))
	{
		b = getGlyphsForRow(index.x, -1, true).getBoundingBox(jmax(0, index.y - 1), 1, true);
		b = b.translated(b.getWidth(), 0.0f);
	}

	if (getNumColumns(index.x) == 0)
	{
		b = { (float)TEXT_INDENT, topOfRow, font.getStringWidthFloat(" "), font.getHeight() * lineSpacing };
	}

	//auto b = lines.lines[index.x].glyphs.getBoundingBox(index.y-1, 1, true);

	//b = b.translated(0, topOfRow);

	b = b.withSizeKeepingCentre(b.getWidth(), font.getHeight() * lineSpacing);

	return b;
#endif

	index.x = jmax(0, jmin(lines.size()-1, index.x));

	index.y = jlimit(0, getNumColumns(index.x), index.y);


	auto numColumns = getNumColumns(index.x);

	auto first = jlimit(0, numColumns, index.y);
	

	return getBoundsOnRow(index.x, Range<int>(first, first+1), m).getRectangle(0);
}

GlyphArrangement mcl::TextDocument::getGlyphsForRow (int row, int token, bool withTrailingSpace) const
{
    return lines.getGlyphs (row,
                            getVerticalPosition (row, Metric::baseline),
                            token,
                            withTrailingSpace);
}

GlyphArrangement mcl::TextDocument::findGlyphsIntersecting (Rectangle<float> area, int token) const
{
    auto range = getRangeOfRowsIntersecting (area);
    auto rows = Array<RowData>();
    auto glyphs = GlyphArrangement();

    for (int n = range.getStart(); n < range.getEnd(); ++n)
    {
        glyphs.addGlyphArrangement (getGlyphsForRow (n, token));
    }
    return glyphs;
}

juce::Range<int> mcl::TextDocument::getRangeOfRowsIntersecting (juce::Rectangle<float> area) const
{
	if (rowPositions.isEmpty())
		return { 0, 1 };

	Range<float> yRange = { area.getY() - getRowHeight(), area.getBottom() + getRowHeight()};

	int min = INT_MAX;
	int max = 0;

	for (int i = 0; i < rowPositions.size(); i++)
	{
		if (yRange.contains(rowPositions[i]))
		{
			min = jmin(min, i);
			max = jmax(max, i);
		}
	}
	
	return { min, max+1 };


	int start = INT_MAX;
	int end = -1;

	float gap = font.getHeight() * (lineSpacing - 1.f) * 0.5f;


	float lineHeight = getCharacterRectangle().getHeight() + gap;

	

	for (int i = 0; i < getNumRows(); i++)
	{
		auto top = rowPositions[i];

		if (isPositiveAndBelow(i, lines.size()))
			lineHeight = lines.lines[i]->height + gap;

		auto bottom = top + lineHeight;

		Rectangle<float> d(0.0f, top, 1.0f, bottom - top);

		if (area.intersects(d))
		{
			start = jmin(start, i);
			end = jmax(end, i);
		}
	}

	if (end == -1)
		return {};

	return { start, end+1};
}

Array<mcl::TextDocument::RowData> mcl::TextDocument::findRowsIntersecting (Rectangle<float> area,
                                                                           bool computeHorizontalExtent) const
{
    auto range = getRangeOfRowsIntersecting (area);
    auto rows = Array<RowData>();

    for (int n = range.getStart(); n < range.getEnd(); ++n)
    {
        RowData data;
        data.rowNumber = n;

		data.bounds = getBoundsOnRow(n, Range<int>(0, getNumColumns(n)), GlyphArrangementArray::ReturnBeyondLastCharacter);

		if (data.bounds.isEmpty())
		{
			data.bounds.add(0.0f, getVerticalPosition(n, Metric::top), 1.0f, font.getHeight() * lineSpacing);
		}

        for (const auto& s : selections)
        {
            if (s.intersectsRow (n))
            {
                data.isRowSelected = true;
                break;
            }
        }
        rows.add (data);
    }
    return rows;
}

Point<int> mcl::TextDocument::findIndexNearestPosition (Point<float> position) const
{
	position = position.translated(getCharacterRectangle().getWidth() * 0.5f, 0.0f);

	auto gap = font.getHeight() * lineSpacing - font.getHeight();
	float yPos = gap/2.0f;

	for (int l = 0; l < getNumRows(); l++)
	{
		auto& line = lines.lines[l];

		Range<float> p(yPos-gap/2.0f, yPos + line->height + gap/2.0f);

		if (p.contains(position.y))
		{
			auto glyphs = getGlyphsForRow(l, -1, true);

			int numGlyphs = glyphs.getNumGlyphs();
			auto col = numGlyphs;

			for (int n = 0; n < numGlyphs; ++n)
			{
				auto b = glyphs.getBoundingBox(n, 1, true).expanded(0.0f, gap/2.f);

				if (b.contains(position))
				{
					col = n;
					break;
				}
			}

			return { l, col };
		}

		yPos = p.getEnd();
	}

	return {0, 0};

	jassertfalse;

    auto lineHeight = font.getHeight() * lineSpacing;
    auto row = jlimit (0, jmax (getNumRows() - 1, 0), int (position.y / lineHeight));
    auto col = 0;
    auto glyphs = getGlyphsForRow (row);

    if (position.x > 0.f)
    {
        col = glyphs.getNumGlyphs();

        for (int n = 0; n < glyphs.getNumGlyphs(); ++n)
        {
            if (glyphs.getBoundingBox (n, 1, true).getHorizontalRange().contains (position.x))
            {
                col = n;
                break;
            }
        }
    }
    return { row, col };
}

Point<int> mcl::TextDocument::getEnd() const
{
    return { getNumRows(), 0 };
}

bool mcl::TextDocument::next (Point<int>& index) const
{
    if (index.y < getNumColumns (index.x))
    {
        index.y += 1;
        return true;
    }
    else if (index.x < getNumRows())
    {
        index.x += 1;
        index.y = 0;
        return true;
    }
    return false;
}

bool mcl::TextDocument::prev (Point<int>& index) const
{
    if (index.y > 0)
    {
        index.y -= 1;
        return true;
    }
    else if (index.x > 0)
    {
        index.x -= 1;
        index.y = getNumColumns (index.x);
        return true;
    }
    return false;
}

bool mcl::TextDocument::nextRow (Point<int>& index) const
{
    if (index.x < getNumRows()-1)
    {
        index.x += 1;
        index.y = jmin (index.y, getNumColumns (index.x));
        return true;
    }
    return false;
}

bool mcl::TextDocument::prevRow (Point<int>& index) const
{
    if (index.x > 0)
    {
        index.x -= 1;
        index.y = jmin (index.y, getNumColumns (index.x));
        return true;
    }
    return false;
}

void mcl::TextDocument::navigate (juce::Point<int>& i, Target target, Direction direction) const
{
    std::function<bool(Point<int>&)> advance;
    std::function<juce_wchar(Point<int>&)> get;

    using CF = CharacterFunctions;
    static String punctuation = "{}<>()[],.;:";

    switch (direction)
    {
        case Direction::forwardRow:
            advance = [this] (Point<int>& i) { return nextRow (i); };
            get     = [this] (Point<int> i) { return getCharacter (i); };
            break;
        case Direction::backwardRow:
            advance = [this] (Point<int>& i) { return prevRow (i); };
            get     = [this] (Point<int> i) { prev (i); return getCharacter (i); };
            break;
        case Direction::forwardCol:
            advance = [this] (Point<int>& i) { return next (i); };
            get     = [this] (Point<int> i) { return getCharacter (i); };
            break;
        case Direction::backwardCol:
            advance = [this] (Point<int>& i) { return prev (i); };
            get     = [this] (Point<int> i) { prev (i); return getCharacter (i); };
            break;
    }

    switch (target)
    {
        case Target::whitespace : while (! CF::isWhitespace (get (i)) && advance (i)) { } break;
        case Target::punctuation: while (! punctuation.containsChar (get (i)) && advance (i)) { } break;
        case Target::character  : advance (i); break;
		case Target::firstnonwhitespace:
		{
			jassert(direction == Direction::backwardCol);

			bool skipTofirstNonWhiteCharacter = false;

			while (get(i) != '\n' && prev(i)) 
				skipTofirstNonWhiteCharacter |= !CF::isWhitespace(get(i));

			while (skipTofirstNonWhiteCharacter && CF::isWhitespace(get(i)))
				next(i);

			if (skipTofirstNonWhiteCharacter)
				prev(i);

			break;
		}
        case Target::subword    : jassertfalse; break; // IMPLEMENT ME
        case Target::word       : while (CF::isWhitespace (get (i)) && advance (i)) { } break;
        case Target::token:
        {
            int s = lines.getToken (i.x, i.y, -1);
            int t = s;

            while (s == t && advance (i))
            {
                if (getNumColumns (i.x) > 0)
                {
                    //s = t;
                    t = lines.getToken (i.x, i.y, s);
                }
            }
            break;
        }
        case Target::line       : while (get (i) != '\n' && advance (i)) { } break;
        case Target::paragraph  : while (getNumColumns (i.x) > 0 && advance (i)) {} break;
        case Target::scope      : jassertfalse; break; // IMPLEMENT ME
        case Target::document   : while (advance (i)) { } break;
    }
}

void mcl::TextDocument::navigateSelections (Target target, Direction direction, Selection::Part part)
{
    for (auto& selection : selections)
    {
        switch (part)
        {
            case Selection::Part::head: navigate (selection.head, target, direction); break;
            case Selection::Part::tail: navigate (selection.tail, target, direction); break;
            case Selection::Part::both: navigate (selection.head, target, direction); selection.tail = selection.head; break;
        }
    }
}

mcl::Selection mcl::TextDocument::search (juce::Point<int> start, const juce::String& target) const
{
    while (start != getEnd())
    {
        auto y = lines[start.x].indexOf (start.y, target);

        if (y != -1)
            return Selection (start.x, y, start.x, y + target.length());

        start.y = 0;
        start.x += 1;
    }
    return Selection();
}

juce_wchar mcl::TextDocument::getCharacter (Point<int> index) const
{
	if (index.x < 0 || index.y < 0)
		return 0;

    jassert (0 <= index.x && index.x <= lines.size());
    jassert (0 <= index.y && index.y <= lines[index.x].length());

    if (index == getEnd() || index.y == lines[index.x].length())
    {
        return '\n';
    }
    return lines[index.x].getCharPointer()[index.y];
}

const mcl::Selection& mcl::TextDocument::getSelection (int index) const
{
    return selections.getReference (index);
}

const Array<mcl::Selection>& mcl::TextDocument::getSelections() const
{
    return selections;
}

String mcl::TextDocument::getSelectionContent (Selection s) const
{
    s = s.oriented();

    if (s.isSingleLine())
    {
        return lines[s.head.x].substring (s.head.y, s.tail.y);
    }
    else
    {
        String content = lines[s.head.x].substring (s.head.y) + "\n";

        for (int row = s.head.x + 1; row < s.tail.x; ++row)
        {
            content += lines[row] + "\n";
        }
        content += lines[s.tail.x].substring (0, s.tail.y);
        return content;
    }
}

mcl::Transaction mcl::TextDocument::fulfill (const Transaction& transaction)
{
    cachedBounds = {}; // invalidate the bounds

    const auto t = transaction.accountingForSpecialCharacters (*this);
    const auto s = t.selection.oriented();
    const auto L = getSelectionContent (s.horizontallyMaximized (*this));
    const auto i = s.head.y;
    const auto j = L.lastIndexOf ("\n") + s.tail.y + 1;
    const auto M = L.substring (0, i) + t.content + L.substring (j);

    for (auto& existingSelection : selections)
    {
        existingSelection.pullBy (s);
        existingSelection.pushBy (Selection (t.content).startingFrom (s.head));
    }

	auto sPos = CodeDocument::Position(doc, s.head.x, s.head.y);
	auto ePos = CodeDocument::Position(doc, s.tail.x, s.tail.y);

	shouldBe = M;

	ScopedValueSetter<bool> svs(checkThis, true);

	doc.replaceSection(sPos.getPosition(), ePos.getPosition(), t.content);

	

	
	

#if 0
	

	
#endif

	
	

    using D = Transaction::Direction;
    auto inf = std::numeric_limits<float>::max();

    Transaction r;
    r.selection = Selection (t.content).startingFrom (s.head);
    r.content = L.substring (i, j);
    r.affectedArea = Rectangle<float> (0, 0, inf, inf);
    r.direction = t.direction == D::forward ? D::reverse : D::forward;

    return r;
}

void mcl::TextDocument::clearTokens (juce::Range<int> rows)
{
    for (int n = rows.getStart(); n < rows.getEnd(); ++n)
    {
        lines.clearTokens (n);
    }
}

void mcl::TextDocument::applyTokens (juce::Range<int> rows, const juce::Array<Selection>& zones)
{
    for (int n = rows.getStart(); n < rows.getEnd(); ++n)
    {
        for (const auto& zone : zones)
        {
            if (zone.intersectsRow (n))
            {
                lines.applyTokens (n, zone);
            }
        }
    }
}

juce::Array<juce::Line<float>> mcl::TextDocument::getUnderlines(const Selection& s, Metric m) const
{
	auto o = s.oriented();

	Range<int> lineRange = { o.head.x, o.tail.x + 1 };
	Array<Line<float>> underlines;

	for (int l = lineRange.getStart(); l < lineRange.getEnd(); l++)
	{
		if (isPositiveAndBelow(l, getNumRows()))
		{
			int left = 0;
			int right = getNumColumns(l);

			if (l == lineRange.getStart())
				left = o.head.y;

			if (l == lineRange.getEnd() - 1)
				right = o.tail.y;

			auto ul = lines.lines[l]->getUnderlines({ left, right }, !s.isSingular());

			float delta = 0.0f;

			switch (m)
			{
			case Metric::top:		delta = 0.0f; break;
			case Metric::ascent:
			case Metric::baseline:  delta = (getRowHeight() + getFontHeight()) / 2.0f + 2.0f; break;
			case Metric::bottom:	delta = getRowHeight();
			default:
				break;
			}

			auto t = AffineTransform::translation(TEXT_INDENT, getVerticalPosition(l, Metric::top) + delta);
			for (auto& u : ul)
			{
				u.applyTransform(t);
			}

			underlines.addArray(ul);
		}
	}

	return underlines;
}



//==============================================================================
class mcl::Transaction::Undoable : public UndoableAction
{
public:
    Undoable (TextDocument& document, Callback callback, Transaction forward)
    : document (document)
    , callback (callback)
    , forward (forward) {}

    bool perform() override
    {
        callback (reverse = document.fulfill (forward));
        return true;
    }

    bool undo() override
    {
        callback (forward = document.fulfill (reverse));
        return true;
    }

    TextDocument& document;
    Callback callback;
    Transaction forward;
    Transaction reverse;
};




//==============================================================================
mcl::Transaction mcl::Transaction::accountingForSpecialCharacters (const TextDocument& document) const
{
    Transaction t = *this;
    auto& s = t.selection;

    
    if (content.getLastCharacter() == KeyPress::backspaceKey)
    {
        if (s.head.y == s.tail.y)
        {
            document.prev (s.head);
        }
        t.content.clear();
    }
    else if (content.getLastCharacter() == KeyPress::deleteKey)
    {
        if (s.head.y == s.tail.y)
        {
            document.next (s.head);
        }
        t.content.clear();
    }
    return t;
}

UndoableAction* mcl::Transaction::on (TextDocument& document, Callback callback)
{
    return new Undoable (document, callback, *this);
}




//==============================================================================
mcl::TextEditor::TextEditor(CodeDocument& codeDoc)
: document(codeDoc)
, caret (document)
, gutter (document)
, linebreakDisplay(document)
, map(document, new CPlusPlusCodeTokeniser())
, highlight (document)
, docRef(codeDoc)
{
    lastTransactionTime = Time::getApproximateMillisecondCounter();
    document.setSelections ({ Selection() });
	docRef.addListener(this);

	
	setFont(Font("Consolas", 16.0f, Font::plain));
    //setFont (Font(Font::getDefaultMonospacedFontName(), 16.0f, Font::plain));

    translateView (gutter.getGutterWidth(), 0);
    setWantsKeyboardFocus (true);

	addAndMakeVisible(linebreakDisplay);
    addAndMakeVisible (highlight);
    addAndMakeVisible (caret);
    addAndMakeVisible (gutter);
	addAndMakeVisible(map);


	struct Type
	{
		String name;
		uint32 colour;
	};

	const Type types[] =
	{
		{ "Error", 0xffBB3333 },
		{ "Comment", 0xff77CC77 },
		{ "Keyword", 0xffbbbbff },
		{ "Operator", 0xffCCCCCC },
		{ "Identifier", 0xffDDDDFF },
		{ "Integer", 0xffDDAADD },
		{ "Float", 0xffEEAA00 },
		{ "String", 0xffDDAAAA },
		{ "Bracket", 0xffFFFFFF },
		{ "Punctuation", 0xffCCCCCC },
		{ "Preprocessor Text", 0xffCC7777 }
	};

	for (unsigned int i = 0; i < sizeof(types) / sizeof(types[0]); ++i)  // (NB: numElementsInArray doesn't work here in GCC4.2)
		colourScheme.set(types[i].name, Colour(types[i].colour));


	map.colourScheme = colourScheme;
}

mcl::TextEditor::~TextEditor()
{
	docRef.removeListener(this);

#if MCL_ENABLE_OPEN_GL
    context.detach();
#endif
}

void mcl::TextEditor::setFont (Font font)
{
    document.setFont (font);
    repaint();
}

void mcl::TextEditor::setText (const String& text)
{
    document.replaceAll (text);
    repaint();
}

void mcl::TextEditor::translateView (float dx, float dy)
{
    auto W = viewScaleFactor * document.getBounds().getWidth();
    auto H = viewScaleFactor * document.getBounds().getHeight();

    translation.x = jlimit (jmin (gutter.getGutterWidth(), -W + getWidth()), gutter.getGutterWidth(), translation.x + dx);
    translation.y = jlimit (jmin (-0.f, -H + getHeight()), 0.0f, translation.y + dy);

    updateViewTransform();
}

void mcl::TextEditor::scaleView (float scaleFactorMultiplier, float verticalCenter)
{
    viewScaleFactor = jlimit(0.5f, 4.0f, viewScaleFactor * scaleFactorMultiplier);
	gutter.setScaleFactor(viewScaleFactor);
	
	

	refreshLineWidth();
	//translateView(0.0f, 0.0f);
	
}

void mcl::TextEditor::updateViewTransform()
{
    transform = AffineTransform::scale (viewScaleFactor).translated (translation.x, translation.y);
    highlight.setViewTransform (transform);
    caret.setViewTransform (transform);
    gutter.setViewTransform (transform);
	linebreakDisplay.setViewTransform(transform);


	auto rows = document.getRangeOfRowsIntersecting(getLocalBounds().toFloat().transformed(transform.inverted()));

	map.setVisibleRange(rows);
	

    repaint();
}

void mcl::TextEditor::updateSelections()
{
    highlight.updateSelections();
    caret.updateSelections();
    gutter.updateSelections();

	auto s = document.getSelections().getFirst();

	auto& doc = document.getCodeDocument();
	CodeDocument::Position pos(doc, s.head.x, s.head.y);
	pos.moveBy(-1);
	auto r = pos.getCharacter();

	if (ActionHelpers::isRightClosure(r))
	{
		int numToSkip = 0;

		while (pos.getPosition() > 0)
		{
			pos.moveBy(-1);

			auto l = pos.getCharacter();

			if (l == r)
			{
				numToSkip++;
			}

			if (ActionHelpers::isMatchingClosure(l, r))
			{
				numToSkip--;

				if (numToSkip < 0)
				{
					currentClosure[0] = { pos.getLineNumber(), pos.getIndexInLine() + 1, pos.getLineNumber(), pos.getIndexInLine() + 1 };
					currentClosure[1] = s;
					showClosures = true;
					return;
				}
			}
		}

		currentClosure[0] = {};
		currentClosure[1] = s;
		showClosures = true;
		return;
	}
	
	currentClosure[0] = {};
	currentClosure[1] = {};
	showClosures = false;
}

void mcl::TextEditor::translateToEnsureCaretIsVisible()
{
    auto i = document.getSelections().getLast().head;
    auto t = Point<float> (0.f, document.getVerticalPosition (i.x, TextDocument::Metric::top))   .transformedBy (transform);
    auto b = Point<float> (0.f, document.getVerticalPosition (i.x, TextDocument::Metric::bottom)).transformedBy (transform);

    if (t.y < 0.f)
    {
        translateView (0.f, -t.y);
    }
    else if (b.y > getHeight())
    {
        translateView (0.f, -b.y + getHeight());
    }
}

namespace Icons
{
static const unsigned char lineBreak[] = { 110,109,254,60,16,68,10,247,170,68,108,254,60,16,68,0,8,177,68,98,254,60,16,68,215,27,177,68,221,28,16,68,215,43,177,68,63,245,15,68,215,43,177,68,108,72,217,13,68,215,43,177,68,108,72,217,13,68,205,44,177,68,108,172,60,9,68,205,44,177,68,108,172,60,
9,68,10,55,179,68,108,0,104,3,68,205,76,176,68,108,172,60,9,68,143,98,173,68,108,172,60,9,68,205,108,175,68,108,201,38,13,68,205,108,175,68,108,201,38,13,68,10,247,170,68,108,254,60,16,68,10,247,170,68,99,101,0,0 };

}

void mcl::TextEditor::resized()
{
	auto b = getLocalBounds();
	
	
	
	

	map.setBounds(b.removeFromRight(150));
	linebreakDisplay.setBounds(b.removeFromRight(15));

	maxLinesToShow = b.getWidth() - TEXT_INDENT - 10;

	refreshLineWidth();
	
    highlight.setBounds (b);
    caret.setBounds (b);
    gutter.setBounds (b);
    resetProfilingData();
}

void mcl::TextEditor::paint (Graphics& g)
{
    auto start = Time::getMillisecondCounterHiRes();
    g.fillAll (findColour (CodeEditorComponent::backgroundColourId));

    String renderSchemeString;

	renderTextUsingGlyphArrangement(g);

#if REMOVE
    switch (renderScheme)
    {
        case RenderScheme::usingAttributedStringSingle:
            renderTextUsingAttributedStringSingle (g);
            renderSchemeString = "AttributedStringSingle";
            break;
        case RenderScheme::usingAttributedString:
            renderTextUsingAttributedString (g);
            renderSchemeString = "attr. str";
            break;
        case RenderScheme::usingGlyphArrangement:
            renderTextUsingGlyphArrangement (g);
            renderSchemeString = "glyph arr.";
            break;
    }
#endif

    lastTimeInPaint = Time::getMillisecondCounterHiRes() - start;
    accumulatedTimeInPaint += lastTimeInPaint;
    numPaintCalls += 1;

    if (drawProfilingInfo)
    {
        String info;
        info += "paint mode         : " + renderSchemeString + "\n";
        info += "cache glyph bounds : " + String (document.lines.cacheGlyphArrangement ? "yes" : "no") + "\n";
        info += "core graphics      : " + String (allowCoreGraphics ? "yes" : "no") + "\n";
        info += "opengl             : " + String (useOpenGLRendering ? "yes" : "no") + "\n";
        info += "syntax highlight   : " + String (enableSyntaxHighlighting ? "yes" : "no") + "\n";
        info += "mean render time   : " + String (accumulatedTimeInPaint / numPaintCalls) + " ms\n";
        info += "last render time   : " + String (lastTimeInPaint) + " ms\n";
        info += "tokeniser time     : " + String (lastTokeniserTime) + " ms\n";

        g.setColour (findColour (CodeEditorComponent::defaultTextColourId));
        g.setFont (Font ("Courier New", 12, 0));
        g.drawMultiLineText (info, getWidth() - 280, 10, 280);
    }

	if (showClosures)
	{
		bool ok = !(currentClosure[0] == Selection());

		auto rect = [this](const Selection& s)
		{
			auto p = s.head;
			auto l = document.getBoundsOnRow(p.x, { p.y - 1, p.y }, GlyphArrangementArray::ReturnLastCharacter);
			auto r = l.getRectangle(0);
			return r.transformed(transform).expanded(1.0f);
		};

		if (ok)
		{
			g.setColour(findColour(CodeEditorComponent::defaultTextColourId).withAlpha(0.6f));
			g.drawRoundedRectangle(rect(currentClosure[0]), 2.0f, 1.0f);
			g.drawRoundedRectangle(rect(currentClosure[1]), 2.0f, 1.0f);
		}
		else
		{
			g.setColour(Colours::red.withAlpha(0.5f));
			g.drawRoundedRectangle(rect(currentClosure[1]), 2.0f, 1.0f);
		}

		
	}

#if PROFILE_PAINTS
    std::cout << "[TextEditor::paint] " << lastTimeInPaint << std::endl;
#endif
}

void mcl::TextEditor::paintOverChildren (Graphics& g)
{
}

void mcl::TextEditor::mouseDown (const MouseEvent& e)
{
    if (e.getNumberOfClicks() > 1)
    {
        return;
    }
    else if (e.mods.isRightButtonDown())
    {
        PopupMenu menu;

        menu.addItem (1, "Render scheme: AttributedStringSingle", true, renderScheme == RenderScheme::usingAttributedStringSingle, nullptr);
        menu.addItem (2, "Render scheme: AttributedString", true, renderScheme == RenderScheme::usingAttributedString, nullptr);
        menu.addItem (3, "Render scheme: GlyphArrangement", true, renderScheme == RenderScheme::usingGlyphArrangement, nullptr);
        menu.addItem (4, "Cache glyph positions", true, document.lines.cacheGlyphArrangement, nullptr);
        menu.addItem (5, "Allow Core Graphics", true, allowCoreGraphics, nullptr);
        menu.addItem (6, "Use OpenGL rendering", true, useOpenGLRendering, nullptr);
        menu.addItem (7, "Syntax highlighting", true, enableSyntaxHighlighting, nullptr);
        menu.addItem (8, "Draw profiling info", true, drawProfilingInfo, nullptr);
        menu.addItem (9, "Debug tokens", true, DEBUG_TOKENS, nullptr);
		menu.addItem(10, "Enable line breaks", true, linebreakEnabled);

        switch (menu.show())
        {
            case 1: renderScheme = RenderScheme::usingAttributedStringSingle; break;
            case 2: renderScheme = RenderScheme::usingAttributedString; break;
            case 3: renderScheme = RenderScheme::usingGlyphArrangement; break;
            case 4: document.lines.cacheGlyphArrangement = ! document.lines.cacheGlyphArrangement; break;
            case 5: allowCoreGraphics = ! allowCoreGraphics; break;
#if MCL_ENABLE_OPEN_GL
            case 6: useOpenGLRendering = ! useOpenGLRendering; if (useOpenGLRendering) context.attachTo (*this); else context.detach(); break;
#else
			// You haven't enabled open GL
			case 6: jassertfalse; break;
#endif
            case 7: enableSyntaxHighlighting = ! enableSyntaxHighlighting; break;
            case 8: drawProfilingInfo = ! drawProfilingInfo; break;
            case 9: DEBUG_TOKENS = ! DEBUG_TOKENS; break;
			case 10: linebreakEnabled = !linebreakEnabled; refreshLineWidth();
        }

        resetProfilingData();
        repaint();
        return;
    }

    auto selections = document.getSelections();
    auto index = document.findIndexNearestPosition (e.position.transformedBy (transform.inverted()));

    if (selections.contains (index))
    {
        return;
    }
    if (! e.mods.isCommandDown() || ! TEST_MULTI_CARET_EDITING)
    {
        selections.clear();
    }

    selections.add (index);
    document.setSelections (selections);
    updateSelections();
}

void mcl::TextEditor::mouseDrag (const MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown())
    {
		if (e.mods.isAltDown())
		{
			auto start = document.findIndexNearestPosition(e.mouseDownPosition.transformedBy(transform.inverted()));
			auto current = document.findIndexNearestPosition(e.position.transformedBy(transform.inverted()));

			Range<int> lineRange = { start.x, current.x + 1 };

			Array<Selection> multiLineSelections;

			for (int i = lineRange.getStart(); i < lineRange.getEnd(); i++)
			{
				multiLineSelections.add({ i, current.y, i, start.y});
			}

			document.setSelections(multiLineSelections);
			updateSelections();
		}
		else
		{
			auto selection = document.getSelections().getFirst();
			selection.head = document.findIndexNearestPosition(e.position.transformedBy(transform.inverted()));
			document.setSelections({ selection });
			translateToEnsureCaretIsVisible();
			updateSelections();
		}
    }
}

void mcl::TextEditor::mouseDoubleClick (const MouseEvent& e)
{
    if (e.getNumberOfClicks() == 2)
    {
        document.navigateSelections (TextDocument::Target::whitespace, TextDocument::Direction::backwardCol, Selection::Part::head);
        document.navigateSelections (TextDocument::Target::whitespace, TextDocument::Direction::forwardCol,  Selection::Part::tail);
        updateSelections();
    }
    else if (e.getNumberOfClicks() == 3)
    {
        document.navigateSelections (TextDocument::Target::line, TextDocument::Direction::backwardCol, Selection::Part::head);
        document.navigateSelections (TextDocument::Target::line, TextDocument::Direction::forwardCol,  Selection::Part::tail);
        updateSelections();
    }
    updateSelections();
}

void mcl::TextEditor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& d)
{
	float dx = d.deltaX;

	if (e.mods.isCommandDown())
	{
		auto factor = 1.0f + (float)d.deltaY / 5.0f;

		scaleView(factor, 0.0f);
		return;
	}

#if JUCE_WINDOWS

	translateView(dx * 80, d.deltaY * 160);

#else
    float dx = d.deltaX;
    /*
     make scrolling away from the gutter just a little "sticky"
     */
    if (translation.x == GUTTER_WIDTH && -0.01f < dx && dx < 0.f)
    {
        dx = 0.f;
    }
    translateView (dx * 400, d.deltaY * 800);
#endif
}

void mcl::TextEditor::mouseMagnify (const MouseEvent& e, float scaleFactor)
{
    scaleView (scaleFactor, e.position.y);
}


bool mcl::TextEditor::keyPressed (const KeyPress& key)
{
    // =======================================================================================
    using Target     = TextDocument::Target;
    using Direction  = TextDocument::Direction;
    auto mods        = key.getModifiers();
    auto isTab       = tabKeyUsed && (key.getTextCharacter() == '\t');


    // =======================================================================================
    auto nav = [this, mods] (Target target, Direction direction)
    {
		if (mods.isShiftDown())
            document.navigateSelections (target, direction, Selection::Part::head);
        else
            document.navigateSelections (target, direction, Selection::Part::both);

        translateToEnsureCaretIsVisible();
        updateSelections();
        return true;
    };
    auto expandBack = [this, mods] (Target target, Direction direction)
    {
        document.navigateSelections (target, direction, Selection::Part::head);
        translateToEnsureCaretIsVisible();
        updateSelections();
        return true;
    };

	auto skipIfClosure = [this](juce_wchar c)
	{
		if (ActionHelpers::isRightClosure(c))
		{
			auto s = document.getSelections().getFirst();
			auto e = document.getCharacter(s.head);
			
			if (e == c)
			{
				document.navigateSelections(Target::character, Direction::forwardCol, Selection::Part::both);
				updateSelections();
				return true;
			}
		}

		

		insert(String::charToString(c));
		return true;
	};

	auto insertClosure = [this](juce_wchar c)
	{
		switch (c)
		{
		case '"': insert("\"\""); break;
		case '(': insert("()"); break;
		case '{': insert("{}"); break;
		case '[': insert("[]"); break;
		}

		document.navigateSelections(Target::character, Direction::backwardCol, Selection::Part::both);

		return true;
	};

    auto expand = [this, nav] (Target target)
    {
		document.navigateSelections(target, Direction::backwardCol, Selection::Part::tail);
		document.navigateSelections(target, Direction::forwardCol, Selection::Part::head);
		//translateToEnsureCaretIsVisible();
		updateSelections();
        return true;
    };

	auto insertTabAfterBracket = [this]()
	{
		auto s = document.getSelections().getLast();
		auto l = document.getCharacter(s.head.translated(0, -1));

		if (l == '{')
		{
			int numChars = s.head.y-1;

			juce::String s = "\n\t";
			juce::String t = "\n";

			while (--numChars >= 0)
			{
				s << "\t";
				t << "\t";
			}

			insert(s);
			insert(t);
			document.navigateSelections(Target::line, Direction::backwardCol, Selection::Part::both);
			document.navigateSelections(Target::character, Direction::backwardCol, Selection::Part::both);
			return true;
		}
		else
		{
			
			CodeDocument::Position pos(document.getCodeDocument(), s.head.x, s.head.y);
			CodeDocument::Position lineStart(document.getCodeDocument(), s.head.x, 0);

			auto before = document.getCodeDocument().getTextBetween(lineStart, pos);
			auto trimmed = before.trimCharactersAtStart(" \t");

			auto delta = before.length() - trimmed.length();

			insert("\n" + before.substring(0, delta));
		}

		

		return true;
	};

	auto addNextTokenToSelection = [this]()
	{
		auto s = document.getSelections().getLast().oriented();
		
		CodeDocument::Position start(document.getCodeDocument(), s.head.x, s.head.y);
		CodeDocument::Position end(document.getCodeDocument(), s.tail.x, s.tail.y);

		auto t = document.getCodeDocument().getTextBetween(start, end);

		while (start.getPosition() < document.getCodeDocument().getNumCharacters())
		{
			start.moveBy(1);
			end.moveBy(1);

			auto current = document.getCodeDocument().getTextBetween(start, end);

			if (current == t)
			{
				Selection s(start.getLineNumber(), start.getIndexInLine(), end.getLineNumber(), end.getIndexInLine());

				

				document.addSelection(s.swapped());
				translateToEnsureCaretIsVisible();
				updateSelections();
				return true;
			}
				
		}

		
		return true;
	};

    auto addCaret = [this] (Target target, Direction direction)
    {
        auto s = document.getSelections().getLast();
        document.navigate (s.head, target, direction);
        document.addSelection (s);
        translateToEnsureCaretIsVisible();
        updateSelections();
        return true;
    };
    auto addSelectionAtNextMatch = [this] ()
    {
        const auto& s = document.getSelections().getLast();

        if (! s.isSingleLine())
        {
            return false;
        }
        auto t = document.search (s.tail, document.getSelectionContent (s));

        if (t.isSingular())
        {
            return false;
        }
        document.addSelection (t);
        translateToEnsureCaretIsVisible();
        updateSelections();
        return true;
    };

	auto remove = [this, expand, expandBack](Target target, Direction direction)
	{
		const auto& s = document.getSelections().getLast();

		auto l = document.getCharacter(s.head.translated(0, -1));
		auto r = document.getCharacter(s.head);
		
		if (ActionHelpers::isMatchingClosure(l, r))
		{
			document.navigateSelections(Target::character, Direction::backwardCol, Selection::Part::tail);
			document.navigateSelections(Target::character, Direction::forwardCol, Selection::Part::head);
			
			insert({});
			return true;
		}

		if (s.isSingular())
			expandBack(target, direction);

		insert({});
		return true;
	};

    // =======================================================================================
    if (key.isKeyCode (KeyPress::escapeKey))
    {
		bool doneSomething = false;

		for (auto& s : document.getSelections())
		{
			if (!s.isSingular())
			{
				s.tail = s.head;
				doneSomething = true;
			}
		}

		if (!doneSomething)
		{
			document.setSelections(document.getSelections().getLast());
		}
			
        updateSelections();
        return true;
    }
    if (mods.isCtrlDown() && mods.isAltDown())
    {
        if (key.isKeyCode (KeyPress::downKey)) return addCaret (Target::character, Direction::forwardRow);
        if (key.isKeyCode (KeyPress::upKey  )) return addCaret (Target::character, Direction::backwardRow);
    }
    if (mods.isCtrlDown())
    {
        if (key.isKeyCode (KeyPress::rightKey)) return nav (Target::whitespace, Direction::forwardCol)  && nav (Target::word, Direction::forwardCol);
        if (key.isKeyCode (KeyPress::leftKey )) return nav (Target::whitespace, Direction::backwardCol) && nav (Target::word, Direction::backwardCol);
        if (key.isKeyCode (KeyPress::downKey )) return nav (Target::word, Direction::forwardCol)  && nav (Target::paragraph, Direction::forwardRow);
        if (key.isKeyCode (KeyPress::upKey   )) return nav (Target::word, Direction::backwardCol) && nav (Target::paragraph, Direction::backwardRow);

        if (key.isKeyCode (KeyPress::backspaceKey)) return (   expandBack (Target::whitespace, Direction::backwardCol)
                                                            && expandBack (Target::word, Direction::backwardCol)
                                                            && insert (""));

        if (key == KeyPress ('e', ModifierKeys::ctrlModifier, 0) ||
            key == KeyPress ('e', ModifierKeys::ctrlModifier | ModifierKeys::shiftModifier, 0))
            return nav (Target::line, Direction::forwardCol);


    }
    if (mods.isCommandDown())
    {
        if (key.isKeyCode (KeyPress::downKey)) return nav (Target::document, Direction::forwardRow);
        if (key.isKeyCode (KeyPress::upKey  )) return nav (Target::document, Direction::backwardRow);
    }



	if (key.isKeyCode(KeyPress::tabKey))
	{
		auto s = document.getSelections().getFirst();

		if (s.head.x != s.tail.x)
		{
			CodeDocument::Position start(document.getCodeDocument(), s.head.x, s.head.y);
			CodeDocument::Position end(document.getCodeDocument(), s.tail.x, s.tail.y);

			start.setPositionMaintained(true);
			end.setPositionMaintained(true);

			s = s.oriented();

			Range<int> rows(s.head.x, s.tail.x + 1);

			Array<Selection> lineStarts;

			for (int i = rows.getStart(); i < rows.getEnd(); i++)
			{
				lineStarts.add(Selection(i, 0, i, 0));
			}

			if (mods.isShiftDown())
			{
				document.setSelections(lineStarts);
				document.navigateSelections(Target::character, Direction::forwardCol, Selection::Part::both);

				remove(Target::character, Direction::backwardCol);
			}
			else
			{
				document.setSelections(lineStarts);
				insert("\t");
			}

			

			Selection prev(start.getLineNumber(), start.getIndexInLine(), end.getLineNumber(), end.getIndexInLine());

			document.setSelections({ prev });

			updateSelections();
			return true;
		}
	}

	if (key.isKeyCode(KeyPress::rightKey)) return nav(Target::character, Direction::forwardCol);
	if (key.isKeyCode(KeyPress::leftKey)) return nav(Target::character, Direction::backwardCol);
	if (key.isKeyCode(KeyPress::downKey)) return nav(Target::character, Direction::forwardRow);
    if (key.isKeyCode (KeyPress::upKey   )) return nav (Target::character, Direction::backwardRow);

	if (key.isKeyCode(KeyPress::backspaceKey)) return remove(Target::character, Direction::backwardCol);
	if (key.isKeyCode(KeyPress::deleteKey))	   return remove(Target::character, Direction::forwardCol);

	if (key.isKeyCode(KeyPress::homeKey)) return nav(Target::firstnonwhitespace, Direction::backwardCol);
	if (key.isKeyCode(KeyPress::endKey))  return nav(Target::line, Direction::forwardCol);

    if (key == KeyPress ('a', ModifierKeys::commandModifier, 0)) return expand (Target::document);
	if (key == KeyPress('d', ModifierKeys::commandModifier, 0))  return addNextTokenToSelection();
    if (key == KeyPress ('e', ModifierKeys::commandModifier, 0)) return expand (Target::token);
    if (key == KeyPress ('l', ModifierKeys::commandModifier, 0)) return expand (Target::line);
    if (key == KeyPress ('f', ModifierKeys::commandModifier, 0)) return addSelectionAtNextMatch();
    if (key == KeyPress ('z', ModifierKeys::commandModifier, 0)) return undo.undo();
    if (key == KeyPress ('r', ModifierKeys::commandModifier, 0)) return undo.redo();

    if (key == KeyPress ('x', ModifierKeys::commandModifier, 0))
    {
		auto s = document.getSelections().getFirst();

		bool move = false;

		if (s.isSingular())
		{
			document.navigate(s.head, Target::line, Direction::backwardCol);
			document.navigate(s.head, Target::character, Direction::backwardCol);
			document.navigate(s.tail, Target::line, Direction::forwardCol);
			document.setSelection(0, s);
			move = true;
		}
		
		SystemClipboard::copyTextToClipboard(document.getSelectionContent(s));

		insert("");

		if (move)
		{
			nav(Target::character, Direction::forwardRow);
			nav(Target::firstnonwhitespace, Direction::backwardCol);
			
			
		}

		return true;
    }
    if (key == KeyPress ('c', ModifierKeys::commandModifier, 0))
    {
        SystemClipboard::copyTextToClipboard (document.getSelectionContent (document.getSelections().getFirst()));
        return true;
    }

    if (key == KeyPress ('v', ModifierKeys::commandModifier, 0))   return insert (SystemClipboard::getTextFromClipboard());
    if (key == KeyPress ('d', ModifierKeys::ctrlModifier, 0))      return insert (String::charToString (KeyPress::deleteKey));
    if (key.isKeyCode (KeyPress::returnKey))                       return insertTabAfterBracket();

	if(ActionHelpers::isLeftClosure(key.getTextCharacter()))   return insertClosure(key.getTextCharacter());
	if (ActionHelpers::isRightClosure(key.getTextCharacter())) return skipIfClosure(key.getTextCharacter());


    if (key.getTextCharacter() >= ' ' || isTab)                    return insert (String::charToString (key.getTextCharacter()));

    return false;
}

bool mcl::TextEditor::insert (const juce::String& content)
{
    double now = Time::getApproximateMillisecondCounter();

    if (now > lastTransactionTime + 400)
    {
        lastTransactionTime = Time::getApproximateMillisecondCounter();
        undo.beginNewTransaction();
    }
    
    for (int n = 0; n < document.getNumSelections(); ++n)
    {
        Transaction t;
        t.content = content;
        t.selection = document.getSelection (n);
        
        auto callback = [this, n] (const Transaction& r)
        {
            switch (r.direction) // NB: switching on the direction of the reciprocal here
            {
                case Transaction::Direction::forward: document.setSelection (n, r.selection); break;
                case Transaction::Direction::reverse: document.setSelection (n, r.selection.tail); break;
            }

            if (! r.affectedArea.isEmpty())
            {
                repaint (r.affectedArea.transformedBy (transform).getSmallestIntegerContainer());
            }
        };
        undo.perform (t.on (document, callback));
    }
	
	translateToEnsureCaretIsVisible();
    updateSelections();
    return true;
}

MouseCursor mcl::TextEditor::getMouseCursor()
{
    return getMouseXYRelative().x < gutter.getGutterWidth() ? MouseCursor::NormalCursor : MouseCursor::IBeamCursor;
}



#if REMOVE
//==============================================================================
void mcl::TextEditor::renderTextUsingAttributedStringSingle (juce::Graphics& g)
{
    g.saveState();
    g.addTransform (transform);

    auto colourScheme = CPlusPlusCodeTokeniser().getDefaultColourScheme();
    auto font = document.getFont();
    auto rows = document.getRangeOfRowsIntersecting (g.getClipBounds().toFloat());
    auto T = document.getVerticalPosition (rows.getStart(), TextDocument::Metric::ascent);
    auto B = document.getVerticalPosition (rows.getEnd(),   TextDocument::Metric::top);
    auto W = 1000;
    auto bounds = Rectangle<float>::leftTopRightBottom (TEXT_INDENT, T, W, B);
    auto content = document.getSelectionContent (Selection (rows.getStart(), 0, rows.getEnd(), 0));

    AttributedString s;
    s.setLineSpacing ((document.getLineSpacing() - 1.f) * font.getHeight());

    CppTokeniserFunctions::StringIterator si (content);
    auto previous = si.t;
    auto start = Time::getMillisecondCounterHiRes();

    while (! si.isEOF())
    {
        auto tokenType = CppTokeniserFunctions::readNextToken (si);
        auto colour = colourScheme.types[tokenType].colour;
        auto token = String (previous, si.t);

        previous = si.t;

        if (enableSyntaxHighlighting)
        {
            s.append (token, font, colour);
        }
        else
        {
            s.append (token, font);
        }
    }

    lastTokeniserTime = Time::getMillisecondCounterHiRes() - start;

    if (allowCoreGraphics)
    {
        s.draw (g, bounds);
    }
    else
    {
        TextLayout layout;
        layout.createLayout (s, bounds.getWidth());
        layout.draw (g, bounds);
    }
    g.restoreState();
}

void mcl::TextEditor::renderTextUsingAttributedString (juce::Graphics& g)
{
    /*
     Credit to chrisboy2000 for this
     */
    auto colourScheme = CPlusPlusCodeTokeniser().getDefaultColourScheme();
    auto originalHeight = document.getFont().getHeight();
    auto font = document.getFont().withHeight (originalHeight * transform.getScaleFactor());
    auto rows = document.findRowsIntersecting (g.getClipBounds().toFloat().transformedBy (transform.inverted()));

    lastTokeniserTime = 0.f;

    for (const auto& r: rows)
    {
        auto line = document.getLine (r.rowNumber);
        auto T = document.getVerticalPosition (r.rowNumber, TextDocument::Metric::ascent);
        auto B = document.getVerticalPosition (r.rowNumber, TextDocument::Metric::bottom);
        auto bounds = Rectangle<float>::leftTopRightBottom (0.f, T, 1000.f, B).transformedBy (transform);

        AttributedString s;

        if (! enableSyntaxHighlighting)
        {
            s.append (line, font);
        }
        else
        {
            auto start = Time::getMillisecondCounterHiRes();

            CppTokeniserFunctions::StringIterator si (line);
            auto previous = si.t;

            while (! si.isEOF())
            {
                auto tokenType = CppTokeniserFunctions::readNextToken (si);
                auto colour = colourScheme.types[tokenType].colour;
                auto token = String (previous, si.t);

                previous = si.t;
                s.append (token, font, colour);
            }

            lastTokeniserTime += Time::getMillisecondCounterHiRes() - start;
        }
        if (allowCoreGraphics)
        {
            s.draw (g, bounds);
        }
        else
        {
            TextLayout layout;
            layout.createLayout (s, bounds.getWidth());
            layout.draw (g, bounds);
        }
    }
}
#endif

void mcl::TextEditor::renderTextUsingGlyphArrangement (juce::Graphics& g)
{
    g.saveState();
    g.addTransform (transform);

    if (enableSyntaxHighlighting)
    {
        auto rows = document.getRangeOfRowsIntersecting (g.getClipBounds().toFloat());


		rows.setStart(jmax(0, rows.getStart() - 20));

        auto index = Point<int> (rows.getStart(), 0);
        

        auto it = TextDocument::Iterator (document, index);
        auto previous = it.getIndex();
        auto zones = Array<Selection>();
        auto start = Time::getMillisecondCounterHiRes();

        while (it.getIndex().x < rows.getEnd() && ! it.isEOF())
        {
            auto tokenType = CppTokeniserFunctions::readNextToken (it);
            zones.add (Selection (previous, it.getIndex()).withStyle (tokenType));
            previous = it.getIndex();
        }
        document.clearTokens (rows);
        document.applyTokens (rows, zones);

        lastTokeniserTime = Time::getMillisecondCounterHiRes() - start;

        for (int n = 0; n < colourScheme.types.size(); ++n)
        {
            g.setColour (colourScheme.types[n].colour);
            document.findGlyphsIntersecting (g.getClipBounds().toFloat(), n).draw (g);
        }
    }
    else
    {
        lastTokeniserTime = 0.f;
        document.findGlyphsIntersecting (g.getClipBounds().toFloat()).draw (g);
    }
    g.restoreState();
}

void mcl::TextEditor::resetProfilingData()
{
    accumulatedTimeInPaint = 0.f;
    numPaintCalls = 0;
}

void LinebreakDisplay::paint(Graphics& g)
{
	float yPos = 0.0f;

	Path p;
	p.loadPathFromData(Icons::lineBreak, sizeof(Icons::lineBreak));
	
	for (int i = 0; i < document.getNumRows(); i++)
	{
		yPos = document.getVerticalPosition(i, mcl::TextDocument::Metric::top);
		int numLines = document.getNumLinesForRow(i) - 1;

		g.setColour(Colours::grey);

		for (int i = 0; i < numLines; i++)
		{
			Rectangle<float> d(0.0f, yPos, (float)getWidth(), (float)getWidth());

			d.reduce(3.0f, 3.0f);

			d = d.transformed(transform).withX(0.0f);

			p.scaleToFit(d.getX(), d.getY(), d.getWidth(), d.getHeight(), true);
			g.fillPath(p);
			
			yPos += document.getFontHeight();
		}
	}
}

void mcl::CodeMap::mouseEnter(const MouseEvent& e)
{
	auto yNormalised = e.position.getY() / (float)getHeight();
	auto lineNumber = surrounding.getStart() + yNormalised * surrounding.getLength();
	auto editor = findParentComponentOfClass<TextEditor>();

	editor->addChildComponent(preview = new HoverPreview(doc, lineNumber));

	preview->colourScheme = editor->colourScheme;
	preview->scale = editor->transform.getScaleFactor();
	preview->repaint();
	
	Desktop::getInstance().getAnimator().fadeIn(preview, 200);

	preview->setBounds(getPreviewBounds(e));
}

void mcl::CodeMap::mouseExit(const MouseEvent& e)
{
	Desktop::getInstance().getAnimator().fadeOut(preview, 200);
	auto editor = findParentComponentOfClass<TextEditor>();
	editor->removeChildComponent(preview);

	

	preview = nullptr;
	
	

}

void mcl::CodeMap::mouseMove(const MouseEvent& e)
{
	auto yNormalised = e.position.getY() / (float)getHeight();

	auto lineNumber = surrounding.getStart() + yNormalised * surrounding.getLength();

	if (preview != nullptr)
	{
		preview->setCenterRow(lineNumber);
		preview->setBounds(getPreviewBounds(e));
	}

	repaint();
}

void mcl::CodeMap::mouseDown(const MouseEvent& e)
{

}

juce::Rectangle<int> mcl::CodeMap::getPreviewBounds(const MouseEvent& e)
{
	auto editor = findParentComponentOfClass<TextEditor>();

	

	auto b = editor->getBounds();
	b.removeFromRight(getWidth());

	auto slice = b.removeFromRight(editor->getWidth() / 3).toFloat();

	auto yNormalised = e.position.getY() / (float)getHeight();

	auto ratio = (float)editor->getWidth() / (float)editor->getHeight();

	auto height = slice.getWidth() / ratio;

	auto diff = slice.getHeight() - height;

	auto a = yNormalised;
	auto invA = 1.0f - yNormalised;

	slice.removeFromTop(a * diff);
	slice.removeFromBottom(invA * diff);

	return slice.toNearestInt();

}
