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



class FoldableLineRange : public ReferenceCountedObject
{
public:

	using LineRangeFunction = std::function<Array<Range<int>>()>;

	FoldableLineRange(Range<int> r, bool folded_) :
		lineRange(r),
		folded(folded_)
	{};


	using Ptr = ReferenceCountedObjectPtr<FoldableLineRange>;
	using List = ReferenceCountedArray<FoldableLineRange>;
	using WeakPtr = WeakReference<FoldableLineRange>;

	class Listener
	{
	public:

		virtual void foldStateChanged(WeakPtr rangeThatHasChanged) = 0;
		virtual void rootWasRebuilt(WeakPtr newRoot) {};

	private:

		JUCE_DECLARE_WEAK_REFERENCEABLE(Listener);
	};

	class Holder
	{
	public:

		Holder(CodeDocument& d) :
			doc(d)
		{};

		enum LineType
		{
			Nothing,
			RangeStartOpen,
			RangeStartClosed,
			Between,
			Folded,
			RangeEnd
		};

		void toggleFoldState(int lineNumber)
		{
			if (auto r = getRangeWithStartAtLine(lineNumber))
			{
				auto type = getLineType(lineNumber);
				r->folded = !r->folded;

				updateFoldState(r);
			}

		}

		void updateFoldState(WeakPtr r)
		{
			lineStates.clear();
			foldedPositions.clear();

			for (auto a : all)
			{
				if (a->isFolded())
				{
					CodeDocument::Position p(doc, a->lineRange.getStart(), 0);
					foldedPositions.add(p);
					foldedPositions.getReference(foldedPositions.size() - 1).setPositionMaintained(true);
					lineStates.setRange(a->lineRange.getStart() + 1, a->lineRange.getLength() - 1, true);
				}
			}

			for (auto l : listeners)
			{
				if (l.get() != nullptr)
				{
					l->foldStateChanged(r);
				}
			}
		}

		WeakPtr getRangeWithStartAtLine(int lineNumber) const
		{
			for (auto r : all)
			{
				if (r->lineRange.getStart() == lineNumber)
					return r;
			}

			return nullptr;
		}

		WeakPtr getRangeContainingLine(int lineNumber) const
		{
			for (auto r : all)
			{
				if (r->lineRange.contains(lineNumber))
					return r;
			}

			return nullptr;
		}

		Range<int> getRangeForLineNumber(int lineNumber) const
		{
			if (auto p = getRangeContainingLine(lineNumber))
			{
				if (p->folded)
					return { lineNumber, lineNumber + 1 };
				else
					return p->lineRange;
			}

			return {};
		}

		LineType getLineType(int lineNumber) const
		{
			bool isBetween = false;
			bool isEnd = false;
			bool isStart = false;

			for (auto l : all)
			{
				isBetween |= l->lineRange.contains(lineNumber);

				

				if (l->lineRange.getStart() == lineNumber)
					return l->isFolded() ? RangeStartClosed : RangeStartOpen;

				if (l->lineRange.contains(lineNumber) && l->isFolded())
					return Folded;

				if (l->lineRange.getEnd()-1 == lineNumber)
					return RangeEnd;
			}

			if (isBetween)
				return Between;
			else
				return Nothing;
		}

		bool isFolded(int lineNumber) const
		{
			return lineStates[lineNumber];
		}

		void setRanges(Array<Range<int>> ranges)
		{
			Array<int> foldedLines;

			for (auto& r : foldedPositions)
			{
				foldedLines.add(r.getLineNumber());
			}

			List l;

			for (auto r : ranges)
			{
				l.add(new FoldableLineRange(r, foldedLines.contains(r.getStart())));
			}
				

			struct PositionSorter
			{
				static int compareElements(FoldableLineRange* first, FoldableLineRange* second)
				{
					auto start1 = first->lineRange.getStart();
					auto start2 = second->lineRange.getStart();

					if (start1 < start2)
						return -1;
					if (start1 > start2)
						return 1;

					return 0;
				}
			};

			PositionSorter s;
			l.sort(s);

			for (int i = 1; i < l.size(); i++)
			{
				Ptr iParent = nullptr;

				for (int j = 0; j < i; j++)
				{
					if (l[j]->contains(l[i]))
						iParent = l[j];
				}

				if (iParent != nullptr)
				{
					iParent->children.add(l[i]);
					l[i]->parent = iParent;
				}
					
			}

			roots.clear();

			for (auto r : l)
			{
				if (r->getParent() == nullptr)
					roots.add(r);
			}

			std::swap(all, l);

			for (auto l : listeners)
			{
				if (l.get() != nullptr)
					l->rootWasRebuilt(nullptr);
			}

			updateFoldState(nullptr);
		}

		CodeDocument& doc;
		Array<CodeDocument::Position> foldedPositions;

		BigInteger lineStates;
		Array<WeakReference<Listener>> listeners;

		List all;
		List roots;
	};


	bool contains(Ptr other) const
	{
		return lineRange.contains(other->lineRange);
	}

	WeakPtr getParent() const { return parent; }

	Range<int> lineRange;
	
	bool isFolded() const
	{
		if (folded)
			return true;

		WeakPtr p = parent;

		while (p != nullptr)
		{
			if (p->folded)
				return true;

			p = p->parent;
		}

		return false;
	}


	bool forEach(const std::function<bool(WeakPtr)>& f)
	{
		if (f(this))
			return true;

		for (auto c : children)
		{
			if (c->forEach(f))
				return true;
		}

		return false;
	}

	void setFolded(bool shouldBeFolded)
	{
		folded = shouldBeFolded;
	}


	

	List children;
	WeakPtr parent;

private:

	bool folded = false;

	

	JUCE_DECLARE_WEAK_REFERENCEABLE(FoldableLineRange);
};




//==============================================================================
class mcl::TextDocument : public CoallescatedCodeDocumentListener,
						  public FoldableLineRange::Listener
{
public:
	enum class Metric
	{
		top,
		ascent,
		baseline,
		bottom,
	};

	/**
	 Text categories the caret may be targeted to. For forward jumps,
	 the caret is moved to be immediately in front of the first character
	 in the given catagory. For backward jumps, it goes just after the
	 first character of that category.
	 */
	enum class Target
	{
		whitespace,
		punctuation,
		character,
		subword,
		cppToken,
		subwordWithPoint,
		word,
		firstnonwhitespace,
		token,
		line,
		paragraph,
		scope,
		document,
	};
	enum class Direction { forwardRow, backwardRow, forwardCol, backwardCol, };

	struct RowData
	{
		int rowNumber = 0;
		bool isRowSelected = false;
		juce::RectangleList<float> bounds;
	};

	class Iterator
	{
	public:
		Iterator(const TextDocument& document, juce::Point<int> index) noexcept : document(&document), index(index) { t = get(); }
		juce::juce_wchar nextChar() noexcept { if (isEOF()) return 0; auto s = t; document->next(index); t = get(); return s; }
		juce::juce_wchar peekNextChar() noexcept { return t; }
		void skip() noexcept { if (!isEOF()) { document->next(index); t = get(); } }
		void skipWhitespace() noexcept { while (!isEOF() && juce::CharacterFunctions::isWhitespace(t)) skip(); }
		void skipToEndOfLine() noexcept { while (t != '\r' && t != '\n' && t != 0) skip(); }
		bool isEOF() const noexcept { return index == document->getEnd(); }
		const juce::Point<int>& getIndex() const noexcept { return index; }
	private:
		juce::juce_wchar get() { return document->getCharacter(index); }
		juce::juce_wchar t;
		const TextDocument* document;
		juce::Point<int> index;
	};

	TextDocument(CodeDocument& doc_);;

	void deactivateLines(SparseSet<int> deactivatedLines)
	{
		
	}

	/** Get the current font. */
	juce::Font getFont() const { return font; }

	/** Get the line spacing. */
	float getLineSpacing() const { return lineSpacing; }

	/** Set the font to be applied to all text. */
	void setFont(juce::Font fontToUse)
	{

		font = fontToUse; lines.font = fontToUse;
		lines.characterRectangle = { 0.0f, 0.0f, font.getStringWidthFloat(" "), font.getHeight() };
	}

	/** Replace the whole document content. */
	void replaceAll(const juce::String& content);

	/** Replace the list of selections with a new one. */
	void setSelections(const juce::Array<Selection>& newSelections) { selections = newSelections; sendSelectionChangeMessage(); }

	/** Replace the selection at the given index. The index must be in range. */
	void setSelection(int index, Selection newSelection) { selections.setUnchecked(index, newSelection); sendSelectionChangeMessage(); }

	void sendSelectionChangeMessage()
	{
		for (auto l : selectionListeners)
		{
			if (l != nullptr)
				l->selectionChanged();
		}
	}

	/** Get the number of rows in the document. */
	int getNumRows() const;

	void foldStateChanged(FoldableLineRange::WeakPtr rangeThatHasChanged)
	{
		rebuildRowPositions();
	}

	void rootWasRebuilt(FoldableLineRange::WeakPtr newRoot)
	{

	}

	/** Get the number of columns in the given row. */
	int getNumColumns(int row) const;

	/** Return the vertical position of a metric on a row. */
	float getVerticalPosition(int row, Metric metric) const;

	/** Return the position in the document at the given index, using the given
		metric for the vertical position. */
	juce::Point<float> getPosition(juce::Point<int> index, Metric metric) const;

	/** Return an array of rectangles covering the given selection. If
		the clip rectangle is empty, the whole selection is returned.
		Otherwise it gets only the overlapping parts.
	 */
	RectangleList<float> getSelectionRegion(Selection selection,
		juce::Rectangle<float> clip = {}) const;

	/** Return the bounds of the entire document. */
	juce::Rectangle<float> getBounds() const;

	Array<Line<float>> getUnderlines(const Selection& s, Metric m) const;

	/** Return the bounding box for the glyphs on the given row, and within
		the given range of columns. The range start must not be negative, and
		must be smaller than ncols. The range end is exclusive, and may be as
		large as ncols + 1, in which case the bounds include an imaginary
		whitespace character at the end of the line. The vertical extent is
		that of the whole line, not the ascent-to-descent of the glyph.
	 */
	juce::RectangleList<float> getBoundsOnRow(int row, juce::Range<int> columns, GlyphArrangementArray::OutOfBoundsMode m_) const;

	/** Return the position of the glyph at the given row and column. */
	juce::Rectangle<float> getGlyphBounds(juce::Point<int> index, GlyphArrangementArray::OutOfBoundsMode m) const;

	/** Return a glyph arrangement for the given row. If token != -1, then
	 only glyphs with that token are returned.
	 */
	juce::GlyphArrangement getGlyphsForRow(int row, int token = -1, bool withTrailingSpace = false) const;

	/** Return all glyphs whose bounding boxes intersect the given area. This method
		may be generous (including glyphs that don't intersect). If token != -1, then
		only glyphs with that token mask are returned.
	 */
	juce::GlyphArrangement findGlyphsIntersecting(juce::Rectangle<float> area, int token = -1) const;

	/** Return the range of rows intersecting the given rectangle. */
	juce::Range<int> getRangeOfRowsIntersecting(juce::Rectangle<float> area) const;

	/** Return data on the rows intersecting the given area. This is sort
		of a convenience method for calling getBoundsOnRow() over a range,
		but could be faster if horizontal extents are not computed.
	 */
	juce::Array<RowData> findRowsIntersecting(juce::Rectangle<float> area,
		bool computeHorizontalExtent = false) const;

	/** Find the row and column index nearest to the given position. */
	juce::Point<int> findIndexNearestPosition(juce::Point<float> position) const;

	/** Return an index pointing to one-past-the-end. */
	juce::Point<int> getEnd() const;

	/** Advance the given index by a single character, moving to the next
		line if at the end. Return false if the index cannot be advanced
		further.
	 */
	bool next(juce::Point<int>& index) const;

	/** Move the given index back by a single character, moving to the previous
		line if at the end. Return false if the index cannot be advanced
		further.
	 */
	bool prev(juce::Point<int>& index) const;

	/** Move the given index to the next row if possible. */
	bool nextRow(juce::Point<int>& index) const;

	/** Move the given index to the previous row if possible. */
	bool prevRow(juce::Point<int>& index) const;

	/** Navigate an index to the first character of the given categaory.
	 */
	void navigate(juce::Point<int>& index, Target target, Direction direction) const;

	/** Navigate all selections. */
	void navigateSelections(Target target, Direction direction, Selection::Part part);

	Selection search(juce::Point<int> start, const juce::String& target) const;

	/** Return the character at the given index. */
	juce::juce_wchar getCharacter(juce::Point<int> index) const;

	/** Add a selection to the list. */
	void addSelection(Selection selection) { selections.add(selection); }

	/** Return the number of active selections. */
	int getNumSelections() const { return selections.size(); }

	/** Return a line in the document. */
	const juce::String& getLine(int lineIndex) const { return lines[lineIndex]; }

	/** Return one of the current selections. */
	const Selection& getSelection(int index) const;

	float getRowHeight() const
	{
		return font.getHeight() * lineSpacing;
	}

	/** Return the current selection state. */
	const juce::Array<Selection>& getSelections() const;

	Rectangle<float> getCharacterRectangle() const
	{
		return lines.characterRectangle;
	}

	/** Return the content within the given selection, with newlines if the
		selection spans muliple lines.
	 */
	juce::String getSelectionContent(Selection selection) const;

	/** Apply a transaction to the document, and return its reciprocal. The selection
		identified in the transaction does not need to exist in the document.
	 */
	Transaction fulfill(const Transaction& transaction);

	/* Reset glyph token values on the given range of rows. */
	void clearTokens(juce::Range<int> rows);

	/** Apply tokens from a set of zones to a range of rows. */
	void applyTokens(juce::Range<int> rows, const juce::Array<Selection>& zones);

	void setMaxLineWidth(int maxWidth)
	{
		if (maxWidth != lines.maxLineWidth)
		{
			lines.maxLineWidth = maxWidth;
			invalidate({});
		}
	}

	CodeDocument& getCodeDocument()
	{
		return doc;
	}



	void invalidate(Range<int> lineRange)
	{
		lines.invalidate(lineRange);
		cachedBounds = {};
		rebuildRowPositions();
	}

	void rebuildRowPositions()
	{
		rowPositions.clearQuick();
		rowPositions.ensureStorageAllocated(lines.size());

		float yPos = 0.0f;

		float gap = getCharacterRectangle().getHeight() * (lineSpacing - 1.f) * 0.5f;

		for (int i = 0; i < lines.size(); i++)
		{
			rowPositions.add(yPos);

			auto l = lines.lines[i];

			lines.ensureValid(i);

			if(!foldManager.isFolded(i))
				yPos += l->height + gap;
		}
	}

	void codeChanged(bool wasInserted, int startIndex, int endIndex)
	{
		CodeDocument::Position start(getCodeDocument(), startIndex);
		CodeDocument::Position end(getCodeDocument(), endIndex);

		auto sameLine = start.getLineNumber() == end.getLineNumber();
		auto existingLine = isPositiveAndBelow(start.getLineNumber(), lines.size());

		auto b = getCodeDocument().getTextBetween(start, end);

		bool includesLineBreak = b.contains("\n");

		lines.lines.clearQuick();
		lines.lines.ensureStorageAllocated(doc.getNumLines());

		for (int i = 0; i < doc.getNumLines(); i++)
		{
			lines.add(doc.getLineWithoutLinebreak(i));
		}

		rebuildRowPositions();
	}

	/** returns the amount of lines occupied by the row. This can be > 1 when the line-break is active. */
	int getNumLinesForRow(int rowIndex) const
	{
		return roundToInt(lines.lines[rowIndex]->height / font.getHeight());
	}

	float getFontHeight() const { return font.getHeight(); };

	void addSelectionListener(Selection::Listener* l)
	{
		selectionListeners.addIfNotAlreadyThere(l);
	}

	void removeSelectionListener(Selection::Listener* l)
	{
		selectionListeners.removeAllInstancesOf(l);
	}

	void setDuplicateOriginal(const Selection& s)
	{
		duplicateOriginal = s;
	}

	FoldableLineRange::Holder& getFoldableLineRangeHolder()
	{
		return foldManager;
	}

	const FoldableLineRange::Holder& getFoldableLineRangeHolder() const
	{
		return foldManager;
	}

	void addFoldListener(FoldableLineRange::Listener* l)
	{
		foldManager.listeners.addIfNotAlreadyThere(l);
	}

	void removeFoldListener(FoldableLineRange::Listener* l)
	{
		foldManager.listeners.removeAllInstancesOf(l);
	}

private:

	FoldableLineRange::Holder foldManager;

	Array<float> rowPositions;

	bool checkThis = false;
	String shouldBe;
	String isReally;

	friend class TextEditor;

	float lineSpacing = 1.333f;

	Selection duplicateOriginal;

	CodeDocument& doc;
	bool internalChange = false;

	mutable juce::Rectangle<float> cachedBounds;
	GlyphArrangementArray lines;
	juce::Font font;

	Array<WeakReference<Selection::Listener>> selectionListeners;

	juce::Array<Selection> selections;
};

}