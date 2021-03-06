/** ============================================================================
 *
 * TextEditor.hpp
 *
 * Copyright (C) Jonathan Zrake
 *
 * You may use, distribute and modify this code under the terms of the GPL3
 * license.
 * =============================================================================
 */


/**
 * 
 TODO:

 - make codemap draggable
 - fix gutter zoom with gradient
 - Find & replace with nice selections



 */


#pragma once




namespace mcl
{




//==============================================================================
class TextEditor : public juce::Component,
				   public CodeDocument::Listener,
				   public ScrollBar::Listener,
				   public TooltipWithArea::Client,
				   public SearchBoxComponent::Listener
{
public:

	using TokenTooltipFunction = std::function<String(const String&, int)>;

    enum class RenderScheme {
        usingAttributedStringSingle,
        usingAttributedString,
        usingGlyphArrangement,
    };


    TextEditor(juce::CodeDocument& doc);
    ~TextEditor();
    void setFont (juce::Font font);
    void setText (const juce::String& text);
    void translateView (float dx, float dy);
    void scaleView (float scaleFactor, float verticalCenter);

	void scrollToLine(float centerLine, bool roundToLine);

	int getNumDisplayedRows() const;

	void setShowNavigation(bool shouldShowNavigation)
	{
		map.setVisible(shouldShowNavigation);
		treeview.setVisible(shouldShowNavigation);
		resized();
	}

    //==========================================================================
    void resized() override;
    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& d) override;
    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override;
    bool keyPressed (const juce::KeyPress& key) override;
    juce::MouseCursor getMouseCursor() override;

	void focusGained(FocusChangeType t) override
	{
		caret.startTimer(50);
	}

	void focusLost(FocusChangeType t) override
	{
		caret.stopTimer();
		caret.repaint();
	}

	Font getFont() const { return document.getFont(); }

	void scrollBarMoved(ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;

	void codeDocumentTextDeleted(int startIndex, int endIndex) override
	{
		updateAfterTextChange();
	}

	void setDeactivatedLines(SparseSet<int> deactivatesLines_)
	{
		deactivatesLines = deactivatesLines_;
		repaint();
	}

	void clearWarningsAndErrors()
	{
		currentError = nullptr;
		warnings.clear();
		repaint();
	}

	void addWarning(const String& errorMessage)
	{
		warnings.add(new Error(document, errorMessage));
		repaint();
	}

	void setError(const String& errorMessage)
	{
		if (errorMessage.isEmpty())
			currentError = nullptr;
		else
			currentError = new Error(document, errorMessage);

		repaint();
	}

	void refreshLineWidth()
	{
		auto firstRow = getFirstLineOnScreen();

		auto actualLineWidth = (maxLinesToShow - gutter.getGutterWidth()) / viewScaleFactor;

		if (linebreakEnabled)
			document.setMaxLineWidth(actualLineWidth);
		else
			document.setMaxLineWidth(-1);

		setFirstLineOnScreen(firstRow);
	}

	TooltipWithArea::Data getTooltip(Point<float> position) override
	{
		if (currentError != nullptr)
		{
			if (auto d = currentError->getTooltip(transform, position))
				return d;
		}

		for (auto w : warnings)
		{
			if (auto d = w->getTooltip(transform, position))
				return d;
		}

		if (tokenTooltipFunction)
		{
			auto start = document.findIndexNearestPosition(position.transformedBy(transform.inverted()));
			auto end = start;

			document.navigate(start, mcl::TextDocument::Target::subword, mcl::TextDocument::Direction::backwardCol);
			document.navigate(end, mcl::TextDocument::Target::subword, mcl::TextDocument::Direction::forwardCol);

			auto token = document.getSelectionContent({ start, end });

			if (token.isNotEmpty())
			{
				TooltipWithArea::Data d;
				d.id = Identifier(token);
				d.text = tokenTooltipFunction(token, start.x);

				

				auto b = document.getBoundsOnRow(start.x, { start.y, end.y }, GlyphArrangementArray::OutOfBoundsMode::ReturnLastCharacter).getRectangle(0);

				d.relativePosition = b.getBottomLeft().transformedBy(transform);

				if (d.text.isEmpty())
					return {};

				return d;
			}
		}

		return {};
	}

	void updateAutocomplete(bool forceShow = false)
	{
		if (document.getSelections().size() != 1)
		{
			closeAutocomplete(true, {}, {});
			return;
		}

		const auto o = document.getSelections().getFirst().oriented().tail;

		auto p = o;
		auto s = p;

		document.navigate(s, mcl::TextDocument::Target::subword, mcl::TextDocument::Direction::backwardCol);
		document.navigate(p, mcl::TextDocument::Target::subword, mcl::TextDocument::Direction::forwardCol);

		autocompleteSelection = { s.x, s.y, p.x, p.y };
		auto input = document.getSelectionContent(autocompleteSelection);

		auto te = s;
		auto ts = s;

		document.navigate(ts, TextDocument::Target::cppToken, TextDocument::Direction::backwardCol);

		Selection beforeToken = { ts.x, ts.y, te.x, te.y };

		auto tokenBefore = document.getSelectionContent(beforeToken);

		auto lineNumber = o.x;
		
		if (forceShow || (input.isNotEmpty() && tokenCollection.hasEntries(input, tokenBefore, lineNumber) || tokenBefore.endsWith(".")))
		{
			if (currentAutoComplete != nullptr)
				currentAutoComplete->setInput(input, tokenBefore, lineNumber);
			else
			{
				addAndMakeVisible(currentAutoComplete = new Autocomplete(tokenCollection, input, tokenBefore, lineNumber));
				addKeyListener(currentAutoComplete);
			}

			auto sToUse = input.isEmpty() ? o.translated(0, 1) : s;

			auto cBounds = document.getBoundsOnRow(sToUse.x, { sToUse.y, sToUse.y + 1 }, GlyphArrangementArray::ReturnLastCharacter).getRectangle(0);
			auto topLeft = cBounds.getBottomLeft();

			currentAutoComplete->setTopLeftPosition(topLeft.roundToInt());

			if (currentAutoComplete->getBoundsInParent().getBottom() > getHeight())
			{
				auto b = cBounds.getTopLeft().translated(0, -currentAutoComplete->getHeight());

				currentAutoComplete->setTopLeftPosition(b.roundToInt());
			}
				

			currentAutoComplete->setTransform(transform);

		}
		else 
			closeAutocomplete(false, {}, {});
	}

	void codeDocumentTextInserted(const String& newText, int insertIndex) override
	{
		updateAfterTextChange();
	}
	
	void closeAutocomplete(bool async, const String& textToInsert, Range<int> selectRange)
	{
		if (currentAutoComplete != nullptr)
		{
			auto f = [this, textToInsert, selectRange]()
			{
				removeKeyListener(currentAutoComplete);

				Desktop::getInstance().getAnimator().fadeOut(currentAutoComplete, 300);

				currentAutoComplete = nullptr;

				if (textToInsert.isNotEmpty())
				{
					ScopedValueSetter<bool> svs(skipTextUpdate, true);
					document.setSelections({ autocompleteSelection });
					insert(textToInsert);

					if (!selectRange.isEmpty())
					{
						auto deltaEnd = textToInsert.length() - selectRange.getEnd();
						auto deltaStart = textToInsert.length() - selectRange.getStart()-1;

						auto s = document.getSelection(0).oriented();

						s.tail.y -= deltaStart;
						s.head.y -= deltaEnd;

						document.setSelections({ s.oriented() });

						updateSelections();
						repaint();
					}

				}

				autocompleteSelection = {};
			};

			if (async)
				MessageManager::callAsync(f);
			else
				f();
		}
	}

	void updateAfterTextChange()
	{
		if (!skipTextUpdate)
		{
			auto b = document.getBounds();

			scrollBar.setRangeLimits({ b.getY(), b.getBottom() });
		
			if (lineRangeFunction)
			{

				auto ranges = lineRangeFunction();
				
				MessageManager::callAsync([this, ranges]()
				{
					document.getFoldableLineRangeHolder().setRanges(ranges);
				});
			}

			updateSelections();

			Timer::callAfterDelay(500, [this]()
			{
				this->updateAutocomplete();
			});

			
			
			updateViewTransform();

			if(currentError != nullptr)
				currentError->rebuild();

			for (auto w : warnings)
				w->rebuild();
		}
	}

	void setLineRangeFunction(const FoldableLineRange::LineRangeFunction& f)
	{
		lineRangeFunction = f;
	};

	int getFirstLineOnScreen() const
	{
		auto rows = document.getRangeOfRowsIntersecting(getLocalBounds().toFloat().transformed(transform.inverted()));
		return rows.getStart();
	}

	void searchItemsChanged() override
	{
		auto selectedLine = document.getSelection(0).head.x;

		Range<int> visibleLines = document.getRangeOfRowsIntersecting(getLocalBounds().toFloat().transformed(transform.inverted()));

		if (!visibleLines.contains(selectedLine))
		{
			auto firstLineToShow = jmax(0, selectedLine - 4);
			setFirstLineOnScreen(firstLineToShow);
		}

		updateSelections();
		repaint();
	}

	void setFirstLineOnScreen(int firstRow)
	{
		translation.y = -document.getVerticalPosition(firstRow, TextDocument::Metric::top) * viewScaleFactor;
		translateView(0.0f, 0.0f);
	}

	CodeEditorComponent::ColourScheme colourScheme;
	juce::AffineTransform transform;

	TokenCollection tokenCollection;

	void setTokenTooltipFunction(const TokenTooltipFunction& f)
	{
		tokenTooltipFunction = f;
	}

private:

	struct Error
	{
		Error(TextDocument& doc_, const String& e):
			document(doc_)
		{
			auto s = e.fromFirstOccurrenceOf("Line ", false, false);
			auto l = s.getIntValue() - 1;
			auto c = s.fromFirstOccurrenceOf("(", false, false).upToFirstOccurrenceOf(")", false, false).getIntValue();
			errorMessage = s.fromFirstOccurrenceOf(": ", false, false);

			Point<int> pos(l, c);
			
			document.navigate(pos, TextDocument::Target::subwordWithPoint, TextDocument::Direction::backwardCol);
			auto endPoint = pos;
			document.navigate(endPoint, TextDocument::Target::subwordWithPoint, TextDocument::Direction::forwardCol);

			if (pos == endPoint)
				endPoint.y += 1;

			start = CodeDocument::Position(document.getCodeDocument(), pos.x, pos.y);
			end = CodeDocument::Position(document.getCodeDocument(), endPoint.x, endPoint.y);
			start.setPositionMaintained(true);
			end.setPositionMaintained(true);

			rebuild();
		}

		void paintLines(Graphics& g, const AffineTransform& transform, Colour c)
		{
			for (auto l : errorLines)
			{
				l.applyTransform(transform);
				Path p;
				p.startNewSubPath(l.getStart());

				auto startX = jmin(l.getStartX(), l.getEndX());
				auto endX = jmax(l.getStartX(), l.getEndX());
				auto y = l.getStartY() - 2.0f;

				float delta = 2.0f;
				float deltaY = delta * 0.5f;

				for (float s = startX + delta; s < endX; s += delta)
				{
					deltaY *= -1.0f;
					p.lineTo(s, y + deltaY);
				}

				p.lineTo(l.getEnd());

				g.setColour(c);
				g.strokePath(p, PathStrokeType(1.0f));
			}
		}

		TooltipWithArea::Data getTooltip(const AffineTransform& transform, Point<float> position)
		{
			auto a = area.transformed(transform);

			TooltipWithArea::Data d;

			if (a.contains(position))
			{
				d.text = errorMessage;
				d.relativePosition = a.getBottomLeft().translated(0.0f, 5.0f);

				d.id = String(d.relativePosition.toString().hash());

				d.clickAction = {};
			}

			return d;
		}

		void rebuild()
		{
			Selection errorWord(start.getLineNumber(), start.getIndexInLine(), end.getLineNumber(), end.getIndexInLine());
			errorLines = document.getUnderlines(errorWord, mcl::TextDocument::Metric::baseline);
			area = document.getSelectionRegion(errorWord).getRectangle(0);
		}

		TextDocument& document;

		CodeDocument::Position start;
		CodeDocument::Position end;

		juce::Rectangle<float> area;
		Array<Line<float>> errorLines;

		String errorMessage;
	};

	TooltipWithArea tooltipManager;

	
	bool skipTextUpdate = false;
	Selection autocompleteSelection;
	ScopedPointer<Autocomplete> currentAutoComplete;
	CodeDocument& docRef;

    //==========================================================================
    bool insert (const juce::String& content);
    void updateViewTransform();
    void updateSelections();
    void translateToEnsureCaretIsVisible();

    void renderTextUsingGlyphArrangement (juce::Graphics& g);
    void resetProfilingData();
    bool enableSyntaxHighlighting = true;
    bool allowCoreGraphics = true;
    bool useOpenGLRendering = false;
    bool drawProfilingInfo = false;
    float accumulatedTimeInPaint = 0.f;
    float lastTimeInPaint = 0.f;
    float lastTokeniserTime = 0.f;
    int numPaintCalls = 0;
    RenderScheme renderScheme = RenderScheme::usingGlyphArrangement;

    //==========================================================================
    double lastTransactionTime;
    bool tabKeyUsed = true;
    TextDocument document;
	ScopedPointer<Error> currentError;

	OwnedArray<Error> warnings;

	FoldableLineRange::LineRangeFunction lineRangeFunction;

    CaretComponent caret;
    GutterComponent gutter;
    HighlightComponent highlight;
	CodeMap map;
	FoldMap foldMap;
	LinebreakDisplay linebreakDisplay;
	DocTreeView treeview;
	ScrollBar scrollBar;
	SparseSet<int> deactivatesLines;
	bool linebreakEnabled = true;
    float viewScaleFactor = 1.f;
	int maxLinesToShow = 0;
	bool lastInsertWasDouble = false;
    juce::Point<float> translation;
    juce::UndoManager undo;
	bool showClosures = false;
	Selection currentClosure[2];
	TokenTooltipFunction tokenTooltipFunction;
	ScopedPointer<SearchBoxComponent> currentSearchBox;
};


}

