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

class SearchReplaceComponent : public Component
{
	enum Mode
	{
		Search,
		SearchAndReplace,
		numModes
	};

	SearchReplaceComponent(TextEditor* parent_, Mode searchMode):
		parent(parent_)
	{
		addAndMakeVisible(searchLabel);

		if (searchMode)
			addAndMakeVisible(replaceLabel);
	}

	Label searchLabel;
	Label replaceLabel;

	TextEditor* parent;
};


class LinebreakDisplay : public Component,
	public LambdaCodeDocumentListener
{
public:

	LinebreakDisplay(mcl::TextDocument& d);

	void refresh()
	{
		repaint();
	}

	void paint(Graphics& g) override;

	void setViewTransform(const AffineTransform& t)
	{
		transform = t;
		repaint();
	}

	AffineTransform transform;

	mcl::TextDocument& document;
};


class mcl::CodeMap : public Component,
	public CodeDocument::Listener,
	public Timer,
	public Selection::Listener
{
public:

	CodeMap(TextDocument& doc_, CodeTokeniser* tok) :
		doc(doc_),
		tokeniser(tok),
		rebuilder(*this)
	{
		doc.addSelectionListener(this);
		doc.getCodeDocument().addListener(this);
	}

	struct DelayedUpdater: public Timer
	{
		DelayedUpdater(CodeMap& p) :
			parent(p)
		{};

		void timerCallback() override
		{
			parent.rebuild();
			stopTimer();
		}

		CodeMap& parent;
	} rebuilder;

	void timerCallback();

	~CodeMap()
	{
		doc.getCodeDocument().removeListener(this);
		doc.removeSelectionListener(this);
	}

	void selectionChanged() override
	{
		rebuilder.startTimer(300);
	}

	

	void codeDocumentTextDeleted(int startIndex, int endIndex) override
	{
		rebuilder.startTimer(300);
	}

	void codeDocumentTextInserted(const String& newText, int insertIndex) override
	{
		rebuilder.startTimer(300);
	}

	float getLineNumberFromEvent(const MouseEvent& e) const;

	Rectangle<int> getPreviewBounds(const MouseEvent& e);

	void mouseEnter(const MouseEvent& e) override;

	void mouseExit(const MouseEvent& e) override;

	void mouseMove(const MouseEvent& e) override;

	void mouseDown(const MouseEvent& e) override;

	void mouseDrag(const MouseEvent& e) override;

	void mouseUp(const MouseEvent& e) override;

	int getNumLinesToShow() const
	{
		auto numLinesFull = getHeight() / 2;

		return jmin(doc.getCodeDocument().getNumLines(), numLinesFull);
	}

	void rebuild();

	struct HoverPreview : public Component
	{
		HoverPreview(TextDocument& doc, int centerRow) :
			document(doc)
		{
			setCenterRow(centerRow);
		}

		void setCenterRow(int newCenterRow);

		void paint(Graphics& g) override;

		TextDocument& document;

		CodeEditorComponent::ColourScheme colourScheme;

		Range<int> rows;
		int centerRow;
		float scale = 1.0f;
	};

	void resized()
	{
		rebuild();
	}

	void setVisibleRange(Range<int> visibleLines);

	bool isActive() const
	{
		return doc.getNumRows() < 10000;
	}

	float lineToY(int lineNumber) const;

	int yToLine(float y) const;

	ScopedPointer<HoverPreview> preview;

	void paint(Graphics& g);

	struct ColouredRectangle
	{
		bool isWhitespace() const
		{
			return c.isTransparent();
		}

		int lineNumber;
		bool upper;
		bool selected = false;
		Colour c;
		int position;
		Rectangle<float> area;
	};

	Array<ColouredRectangle> colouredRectangles;

	CodeEditorComponent::ColourScheme colourScheme;

	TextDocument& doc;
	ScopedPointer<CodeTokeniser> tokeniser;

	float currentAnimatedLine = -1.0f;
	float targetAnimatedLine = -1.0f;

	int hoveredLine = -1;

	int dragDown = 0;
	bool dragging = false;

	Range<int> displayedLines;
	Range<int> surrounding;
	int offsetY = 0;

};


}