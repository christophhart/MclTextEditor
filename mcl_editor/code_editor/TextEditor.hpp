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

 - fix tab not aligning
 - make codemap draggable
 - fix gutter zoom with gradient
 - Find & replace with nice selections



 */


#pragma once
#include "JuceHeader.h"

namespace mcl {

using namespace juce;

    /**
        Factoring of responsibilities in the text editor classes:
     */
    class CaretComponent;         // draws the caret symbol(s)
    class GutterComponent;        // draws the gutter
    class GlyphArrangementArray;  // like StringArray but caches glyph positions
    class HighlightComponent;     // draws the highlight region(s)
    class Selection;              // stores leading and trailing edges of an editing region
    class TextDocument;           // stores text data and caret ranges, supplies metrics, accepts actions
    class TextEditor;             // is a component, issues actions, computes view transform
    class Transaction;            // a text replacement, the document computes the inverse on fulfilling it
	class CodeMap;

    //==============================================================================
    template <typename ArgType, typename DataType>
    class Memoizer
    {
    public:
        using FunctionType = std::function<DataType(ArgType)>;

        Memoizer (FunctionType f) : f (f) {}
        DataType operator() (ArgType argument) const
        {
            if (map.contains (argument))
            {
                return map.getReference (argument);
            }
            map.set (argument, f (argument));
            return this->operator() (argument);
        }
        FunctionType f;
        mutable juce::HashMap<ArgType, DataType> map;
    };
}

using namespace juce;

struct Helpers
{
	static String replaceTabsWithSpaces(const String& s, int numToInsert)
	{
		if (!s.containsChar('\t'))
			return s;

		String rp;

		auto start = s.getCharPointer();
		auto end = s.getCharPointer() + s.length();

		int index = 0;

		while (start != end)
		{
			if (*start == '\t')
			{
				auto numSpaces = numToInsert - index % numToInsert;
				while (--numSpaces >= 0)
					rp << ' ';
			}
			else
				rp << *start;

			start++;
		}

		return rp;
	}
};



struct CoallescatedCodeDocumentListener : public CodeDocument::Listener
{
	CoallescatedCodeDocumentListener(CodeDocument& doc_) :
		lambdaDoc(doc_)
	{
		lambdaDoc.addListener(this);
	}

	virtual ~CoallescatedCodeDocumentListener()
	{
		lambdaDoc.removeListener(this);
	}

	void codeDocumentTextDeleted(int startIndex, int endIndex) override
	{
		codeChanged(false, startIndex, endIndex);
	}

	void codeDocumentTextInserted(const juce::String& newText, int insertIndex) override
	{
		codeChanged(true, insertIndex, insertIndex + newText.length());
	}

	virtual void codeChanged(bool wasAdded, int startIndex, int endIndex) = 0;

private:

	CodeDocument& lambdaDoc;
};



struct LambdaCodeDocumentListener : public CoallescatedCodeDocumentListener
{
	using Callback = std::function<void()>;

	LambdaCodeDocumentListener(CodeDocument& doc_) :
		CoallescatedCodeDocumentListener(doc_)
	{}

	virtual ~LambdaCodeDocumentListener() {};

	void codeChanged(bool, int, int) override
	{
		if (f)
			f();
	}

	void setCallback(const Callback& c)
	{
		f = c;
	}

private:

	Callback f;
};

//==============================================================================
/**
    A data structure encapsulating a contiguous range within a TextDocument.
    The head and tail refer to the leading and trailing edges of a selected
    region (the head is where the caret would be rendered). The selection is
    exclusive with respect to the range of columns (y), but inclusive with
    respect to the range of rows (x). It is said to be oriented when
    head <= tail, and singular when head == tail, in which case it would be
    rendered without any highlighting.
 */
struct mcl::Selection
{
	struct Listener
	{
		virtual ~Listener() {};

		virtual void selectionChanged() = 0;

		JUCE_DECLARE_WEAK_REFERENCEABLE(Listener);
	};

    enum class Part
    {
        head, tail, both,
    };

    Selection() {}
    Selection (juce::Point<int> head) : head (head), tail (head) {}
    Selection (juce::Point<int> head, juce::Point<int> tail) : head (head), tail (tail) {}
    Selection (int r0, int c0, int r1, int c1) : head (r0, c0), tail (r1, c1) {}

    /** Construct a selection whose head is at (0, 0), and whose tail is at the end of
        the given content string, which may span multiple lines.
     */
    Selection (const juce::String& content);

    bool operator== (const Selection& other) const
    {
        return head == other.head && tail == other.tail;
    }

    bool operator< (const Selection& other) const
    {
        const auto A = this->oriented();
        const auto B = other.oriented();
        if (A.head.x == B.head.x) return A.head.y < B.head.y;
        return A.head.x < B.head.x;
    }

    juce::String toString() const
    {
        return "(" + head.toString() + ") - (" + tail.toString() + ")";
    }

    /** Whether or not this selection covers any extent. */
    bool isSingular() const { return head == tail; }

    /** Whether or not this selection is only a single line. */
    bool isSingleLine() const { return head.x == tail.x; }

    /** Whether the given row is within the selection. */
    bool intersectsRow (int row) const
    {
        return isOriented()
            ? head.x <= row && row <= tail.x
            : head.x >= row && row >= tail.x;
    }

    /** Return the range of columns this selection covers on the given row.
     */
    juce::Range<int> getColumnRangeOnRow (int row, int numColumns) const
    {
        const auto A = oriented();

        if (row < A.head.x || row > A.tail.x)
            return { 0, 0 };
        if (row == A.head.x && row == A.tail.x)
            return { A.head.y, A.tail.y };
        if (row == A.head.x)
            return { A.head.y, numColumns };
        if (row == A.tail.x)
            return { 0, A.tail.y };
        return { 0, numColumns };
    }

    /** Whether the head precedes the tail. */
    bool isOriented() const;

    /** Return a copy of this selection, oriented so that head <= tail. */
    Selection oriented() const;

    /** Return a copy of this selection, with its head and tail swapped. */
    Selection swapped() const;

    /** Return a copy of this selection, with head and tail at the beginning and end
        of their respective lines if the selection is oriented, or otherwise with
        the head and tail at the end and beginning of their respective lines.
     */
    Selection horizontallyMaximized (const TextDocument& document) const;

    /** Return a copy of this selection, with its tail (if oriented) moved to
        account for the shape of the given content, which may span multiple
        lines. If instead head > tail, then the head is bumped forward.
     */
    Selection measuring (const juce::String& content) const;

    /** Return a copy of this selection, with its head (if oriented) placed
        at the given index, and tail moved as to leave the measure the same.
        If instead head > tail, then the tail is moved.
     */
    Selection startingFrom (juce::Point<int> index) const;

    Selection withStyle (int token) const { auto s = *this; s.token = token; return s; }

    /** Modify this selection (if necessary) to account for the disapearance of a
        selection someplace else.
     */
    void pullBy (Selection disappearingSelection);

    /** Modify this selection (if necessary) to account for the appearance of a
        selection someplace else.
     */
    void pushBy (Selection appearingSelection);

    /** Modify an index (if necessary) to account for the disapearance of
        this selection.
     */
    void pull (juce::Point<int>& index) const;

    /** Modify an index (if necessary) to account for the appearance of
        this selection.
     */
    void push (juce::Point<int>& index) const;

    juce::Point<int> head; // (row, col) of the selection head (where the caret is drawn)
    juce::Point<int> tail; // (row, col) of the tail
    int token = 0;
};




//==============================================================================
struct mcl::Transaction
{
    using Callback = std::function<void(const Transaction&)>;
    enum class Direction { forward, reverse };

    /** Return a copy of this transaction, corrected for delete and backspace
        characters. For example, if content == "\b" then the selection head is
        decremented and the content is erased.
     */
    Transaction accountingForSpecialCharacters (const TextDocument& document) const;

    /** Return an undoable action, whose perform method thill fulfill this
        transaction, and which caches the reciprocal transaction to be
        issued in the undo method.
     */
    juce::UndoableAction* on (TextDocument& document, Callback callback);

    mcl::Selection selection;
    juce::String content;
    juce::Rectangle<float> affectedArea;
    Direction direction = Direction::forward;

private:
    class Undoable;
};




//==============================================================================
/**
   This class wraps a StringArray and memoizes the evaluation of glyph
   arrangements derived from the associated strings.
*/
class mcl::GlyphArrangementArray
{
public:

	

	enum OutOfBoundsMode
	{
		ReturnNextLine,
		ReturnLastCharacter,
		ReturnBeyondLastCharacter,
		AssertFalse,
		numOutOfBoundsModes
	};

    int size() const { return lines.size(); }
    void clear() { lines.clear(); }
    void add (const juce::String& string) 
	{ 
		auto hash = Entry::createHash(string, maxLineWidth);
		int lineNumber = lines.size();
		auto cachedItem = cache.getCachedItem(lineNumber, hash);

		if (cachedItem == nullptr)
		{
			cachedItem = new Entry(string, maxLineWidth);
			cache.cachedItems.set(lineNumber, { hash, cachedItem });
		}
		
		lines.add(cachedItem); 
	}

    void removeRange (int startIndex, int numberToRemove) { lines.removeRange (startIndex, numberToRemove); }
    const juce::String& operator[] (int index) const;

	


    int getToken (int row, int col, int defaultIfOutOfBounds) const;
    void clearTokens (int index);
    void applyTokens (int index, Selection zone);
    juce::GlyphArrangement getGlyphs (int index,
                                      float baseline,
                                      int token,
                                      bool withTrailingSpace=false) const;

	struct Entry: public ReferenceCountedObject
	{
		using Ptr = ReferenceCountedObjectPtr<Entry>;

		Entry() {}
		Entry(const juce::String& string, int maxLineWidth) : string(string), maxColumns(maxLineWidth) {}
		juce::String string;
		juce::GlyphArrangement glyphsWithTrailingSpace;
		juce::GlyphArrangement glyphs;
		juce::Array<int> tokens;
		bool glyphsAreDirty = true;
		bool tokensAreDirty = true;

		Array<Point<int>> positions;

		static int64 createHash(const String& text, int maxCharacters)
		{
			return text.hashCode64() + (int64)maxCharacters;
		}

		int64 getHash() const
		{
			return createHash(string, maxColumns);
		}

		Array<Line<float>> getUnderlines(Range<int> columnRange, bool createFirstForEmpty)
		{
			struct LR
			{
				void expandLeft(float v)
				{
					l = jmin(l, v);
				}

				void expandRight(float v)
				{
					r = jmax(r, v);
				}

				Line<float> toLine()
				{
					return  Line<float>(l, y, r, y);
				}

				float l = std::numeric_limits<float>::max();
				float r = 0.0f;
				float y = 0.0f;
				bool used = false;
			};

			Array<Line<float>> lines;
			
			if (string.isEmpty() && createFirstForEmpty)
			{
				LR empty;
				empty.used = true;
				empty.y = 0.0f;
				empty.l = 0.0f;
				empty.r = characterBounds.getRight()/2.0f;
				
				lines.add(empty.toLine());
				return lines;
			}
			
			Array<LR> lineRanges;
			lineRanges.insertMultiple(0, {}, charactersPerLine.size());

			for (int i = columnRange.getStart(); i < columnRange.getEnd(); i++)
			{
				auto pos = getPositionInLine(i, ReturnLastCharacter);
				auto lineNumber = pos.x;

				auto b = characterBounds.translated(pos.y * characterBounds.getWidth(), pos.x * characterBounds.getHeight());

				

				auto& l = lineRanges.getReference(lineNumber);

				l.used = true;
				l.y = b.getY();
				l.expandLeft(b.getX());
				l.expandRight(b.getRight());
			}

			

			for (auto& lr : lineRanges)
			{
				if(lr.used)
					lines.add(lr.toLine());
			}

			return lines;
		}

		int getNextColumn(Point<int> pos)
		{

		}

		Point<int> getPositionInLine(int col, OutOfBoundsMode mode) const
		{
			if(isPositiveAndBelow(col, positions.size()))
				return positions[col];

			if (mode == AssertFalse)
			{
				jassertfalse;
				return {};
			}

			int l = 0;

			if (mode == ReturnLastCharacter)
			{
				if (charactersPerLine.isEmpty())
				{
					return { 0, 0 };
				}

				auto l = (int)charactersPerLine.size() - 1;
				auto c = jmax(0, charactersPerLine[l]-1);

				return { l, c };
			}

			if (mode == ReturnNextLine)
			{
				auto l = (int)charactersPerLine.size();
				auto c = 0;

				return { l, c };
			}

			if (mode == ReturnBeyondLastCharacter)
			{
				if (charactersPerLine.isEmpty())
				{
					return { 0, 0 };
				}

				auto l = (int)charactersPerLine.size() - 1;
				auto c = charactersPerLine[l];

				auto isTab = string[jmax(0, col-1)] == '\t';

				if (isTab)
					return { l, roundToTab(c) };

				return { l, c };
			}

			jassertfalse;

			if (col >= string.length())
			{
				l = charactersPerLine.size() - 1;

				if (l < 0)
					return { 0, 0 };

				col = charactersPerLine[l];
				return { l, col };
			}

			for (int i = 0; i < charactersPerLine.size(); i++)
			{
				if (col >= charactersPerLine[i])
				{
					col -= charactersPerLine[i];
					l++;
				}
				else
					break;
			}

			

			return { l, col };
		}

		int getLength() const
		{
			return string.length() + 1;
		}

		Rectangle<float> characterBounds;
		Array<int> charactersPerLine;

		float height = 0.0f;

		int maxColumns = 0;
	};

	struct Cache
	{
		struct Item
		{
			int64 hash;
			Entry::Ptr p;
		};

		Cache()
		{
			
		}

		Entry::Ptr getCachedItem(int line, int64 hash) const
		{
			if (isPositiveAndBelow(line, cachedItems.size()))
			{
				auto l = cachedItems.begin() + line;

				if (l->hash == hash)
					return l->p;
			}

			return nullptr;
		}

		Array<Item> cachedItems;
	} cache;

	static int roundToTab(int c)
	{
		return c + 4 - c % 4;
	}

	mutable juce::ReferenceCountedArray<Entry> lines;

	

	Rectangle<float> characterRectangle;

private:

	

	int maxLineWidth = -1;

    friend class TextDocument;
    friend class TextEditor;
    juce::Font font;
    bool cacheGlyphArrangement = true;

    void ensureValid (int index) const;
    void invalidate(Range<int> lineRange);

    
};




//==============================================================================
class mcl::TextDocument: public CoallescatedCodeDocumentListener
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
        Iterator (const TextDocument& document, juce::Point<int> index) noexcept : document (&document), index (index) { t = get(); }
        juce::juce_wchar nextChar() noexcept      { if (isEOF()) return 0; auto s = t; document->next (index); t = get(); return s; }
        juce::juce_wchar peekNextChar() noexcept  { return t; }
        void skip() noexcept                      { if (! isEOF()) { document->next (index); t = get(); } }
        void skipWhitespace() noexcept            { while (! isEOF() && juce::CharacterFunctions::isWhitespace (t)) skip(); }
        void skipToEndOfLine() noexcept           { while (t != '\r' && t != '\n' && t != 0) skip(); }
        bool isEOF() const noexcept               { return index == document->getEnd(); }
        const juce::Point<int>& getIndex() const noexcept { return index; }
    private:
        juce::juce_wchar get() { return document->getCharacter (index); }
        juce::juce_wchar t;
        const TextDocument* document;
        juce::Point<int> index;
    };

	TextDocument(CodeDocument& doc_) :
		CoallescatedCodeDocumentListener(doc_),
		doc(doc_)
	{
		doc.setDisableUndo(true);
	};

	

    /** Get the current font. */
    juce::Font getFont() const { return font; }

    /** Get the line spacing. */
    float getLineSpacing() const { return lineSpacing; }

    /** Set the font to be applied to all text. */
    void setFont (juce::Font fontToUse) 
	{ 
		
		font = fontToUse; lines.font = fontToUse;  
		lines.characterRectangle = { 0.0f, 0.0f, font.getStringWidthFloat(" "), font.getHeight() };
	}

    /** Replace the whole document content. */
    void replaceAll (const juce::String& content);

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

    

    /** Get the number of columns in the given row. */
    int getNumColumns (int row) const;

    /** Return the vertical position of a metric on a row. */
    float getVerticalPosition (int row, Metric metric) const;

    /** Return the position in the document at the given index, using the given
        metric for the vertical position. */
    juce::Point<float> getPosition (juce::Point<int> index, Metric metric) const;

    /** Return an array of rectangles covering the given selection. If
        the clip rectangle is empty, the whole selection is returned.
        Otherwise it gets only the overlapping parts.
     */
    RectangleList<float> getSelectionRegion (Selection selection,
                                                            juce::Rectangle<float> clip={}) const;

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
    juce::RectangleList<float> getBoundsOnRow (int row, juce::Range<int> columns, GlyphArrangementArray::OutOfBoundsMode m_) const;

    /** Return the position of the glyph at the given row and column. */
    juce::Rectangle<float> getGlyphBounds (juce::Point<int> index, GlyphArrangementArray::OutOfBoundsMode m) const;

    /** Return a glyph arrangement for the given row. If token != -1, then
     only glyphs with that token are returned.
     */
    juce::GlyphArrangement getGlyphsForRow (int row, int token=-1, bool withTrailingSpace=false) const;

    /** Return all glyphs whose bounding boxes intersect the given area. This method
        may be generous (including glyphs that don't intersect). If token != -1, then
        only glyphs with that token mask are returned.
     */
    juce::GlyphArrangement findGlyphsIntersecting (juce::Rectangle<float> area, int token=-1) const;

    /** Return the range of rows intersecting the given rectangle. */
    juce::Range<int> getRangeOfRowsIntersecting (juce::Rectangle<float> area) const;

    /** Return data on the rows intersecting the given area. This is sort
        of a convenience method for calling getBoundsOnRow() over a range,
        but could be faster if horizontal extents are not computed.
     */
    juce::Array<RowData> findRowsIntersecting (juce::Rectangle<float> area,
                                               bool computeHorizontalExtent=false) const;

    /** Find the row and column index nearest to the given position. */
    juce::Point<int> findIndexNearestPosition (juce::Point<float> position) const;

    /** Return an index pointing to one-past-the-end. */
    juce::Point<int> getEnd() const;

    /** Advance the given index by a single character, moving to the next
        line if at the end. Return false if the index cannot be advanced
        further.
     */
    bool next (juce::Point<int>& index) const;

    /** Move the given index back by a single character, moving to the previous
        line if at the end. Return false if the index cannot be advanced
        further.
     */
    bool prev (juce::Point<int>& index) const;

    /** Move the given index to the next row if possible. */
    bool nextRow (juce::Point<int>& index) const;

    /** Move the given index to the previous row if possible. */
    bool prevRow (juce::Point<int>& index) const;

    /** Navigate an index to the first character of the given categaory.
     */
    void navigate (juce::Point<int>& index, Target target, Direction direction) const;

    /** Navigate all selections. */
    void navigateSelections (Target target, Direction direction, Selection::Part part);

    Selection search (juce::Point<int> start, const juce::String& target) const;

    /** Return the character at the given index. */
    juce::juce_wchar getCharacter (juce::Point<int> index) const;

    /** Add a selection to the list. */
    void addSelection (Selection selection) { selections.add (selection); }

    /** Return the number of active selections. */
    int getNumSelections() const { return selections.size(); }

    /** Return a line in the document. */
    const juce::String& getLine (int lineIndex) const { return lines[lineIndex]; }

    /** Return one of the current selections. */
    const Selection& getSelection (int index) const;

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
    juce::String getSelectionContent (Selection selection) const;

    /** Apply a transaction to the document, and return its reciprocal. The selection
        identified in the transaction does not need to exist in the document.
     */
    Transaction fulfill (const Transaction& transaction);

    /* Reset glyph token values on the given range of rows. */
    void clearTokens (juce::Range<int> rows);

    /** Apply tokens from a set of zones to a range of rows. */
    void applyTokens (juce::Range<int> rows, const juce::Array<Selection>& zones);

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

	

private:

	Array<float> rowPositions;

	bool checkThis = false;
	String shouldBe;
	String isReally;

    friend class TextEditor;

	float lineSpacing = 1.5f;

	Selection duplicateOriginal;

	CodeDocument& doc;
	bool internalChange = false;

    mutable juce::Rectangle<float> cachedBounds;
    GlyphArrangementArray lines;
    juce::Font font;

	Array<WeakReference<Selection::Listener>> selectionListeners;

    juce::Array<Selection> selections;
};

class LinebreakDisplay : public Component,
						 public LambdaCodeDocumentListener
{
	public:

	LinebreakDisplay(mcl::TextDocument& d) :
		LambdaCodeDocumentListener(d.getCodeDocument()),
		document(d)
	{
		setCallback(std::bind(&LinebreakDisplay::refresh, this));
	}

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

	CodeMap(TextDocument& doc_, CodeTokeniser* tok):
		doc(doc_),
		tokeniser(tok)
	{
		doc.addSelectionListener(this);
		doc.getCodeDocument().addListener(this);
	}

	void timerCallback()
	{
		rebuild();
		stopTimer();
	}

	~CodeMap()
	{
		doc.getCodeDocument().removeListener(this);
		doc.removeSelectionListener(this);
	}

	void selectionChanged() override
	{
		repaint();
	}

	void codeDocumentTextDeleted(int startIndex, int endIndex) override
	{
		startTimer(300);
	}

	void codeDocumentTextInserted(const String& newText, int insertIndex) override
	{
		startTimer(300);
	}

	Rectangle<int> getPreviewBounds(const MouseEvent& e);

	void mouseEnter(const MouseEvent& e) override;

	void mouseExit(const MouseEvent& e) override;

	void mouseMove(const MouseEvent& e) override;

	void mouseDown(const MouseEvent& e) override;
	



	int getNumLinesToShow() const
	{
		auto numLinesFull = getHeight() / 2;

		return jmin(doc.getCodeDocument().getNumLines(), numLinesFull);
	}

	void rebuild()
	{
		colouredRectangles.clear();

		if (!isActive())
			return;

		CodeDocument::Iterator it(doc.getCodeDocument());

		auto lineLength = (float)doc.getCodeDocument().getMaximumLineLength();

		auto xScale = (float)(getWidth() - 6) / jlimit(1.0f, 80.0f, lineLength);

		if (tokeniser != nullptr)
		{
			

			while (!it.isEOF())
			{
				CodeDocument::Position start(doc.getCodeDocument(), it.getPosition());
				auto token = tokeniser->readNextToken(it);
				
				auto colour = colourScheme.types[token].colour;
				
				if (token == 0)
					break;

				CodeDocument::Position end(doc.getCodeDocument(), it.getPosition());

				auto startLine = start.getLineNumber();
				auto endLine = end.getLineNumber();
				
				auto pos = start;

				auto h = getHeight();
				
				float height = (float)getHeight() / (float)getNumLinesToShow();

				while (pos != end)
				{
					float randomValue = (float)((pos.getCharacter() * 120954801) % 313) / 313.0f;

					auto x = 3.0f + xScale * (float)pos.getIndexInLine();
					auto h = height;
					auto y = (float)pos.getLineNumber() * h;
					auto w = xScale;

					ColouredRectangle r;
					r.lineNumber = pos.getLineNumber();
					r.position = pos.getPosition();

					if (!CharacterFunctions::isWhitespace(pos.getCharacter()))
					{
						r.upper = CharacterFunctions::isUpperCase(pos.getCharacter());
                        
                        auto alpha = jlimit(0.0f, 1.0f, 0.4f + randomValue);
                        
						r.c = colour.withAlpha(alpha);
						
					}
					else
					{
						r.c = Colours::transparentBlack;
					}

					r.area = { x, y, w, h };

					colouredRectangles.add(r);

					pos.moveBy(1);
				}
			}
		}

		

		repaint();
	}

	struct HoverPreview : public Component
	{
		HoverPreview(TextDocument& doc, int centerRow):
			document(doc)
		{
			setCenterRow(centerRow);
		}

		void setCenterRow(int newCenterRow)
		{
			centerRow = newCenterRow;
			auto numRowsToShow = getHeight() / document.getFontHeight();

			rows = Range<int>(centerRow - numRowsToShow, centerRow + numRowsToShow / 2);

			rows.setStart(jmax(0, rows.getStart()));

			repaint();
		}

		void paint(Graphics& g) override
		{
			auto index = Point<int>(jmax(0, rows.getStart()-20), 0);

			auto it = TextDocument::Iterator(document, index);
			auto previous = it.getIndex();
			auto zones = Array<Selection>();
			auto start = Time::getMillisecondCounterHiRes();

			while (it.getIndex().x < rows.getEnd() && !it.isEOF())
			{
				auto tokenType = CppTokeniserFunctions::readNextToken(it);
				zones.add(Selection(previous, it.getIndex()).withStyle(tokenType));
				previous = it.getIndex();
			}

			document.clearTokens(rows.expanded(20));
			document.applyTokens(rows.expanded(20), zones);

			int top = rows.getStart();
			int bottom = rows.getEnd();

			RectangleList<float> area;

			area.add(document.getBoundsOnRow(top, { 0, document.getNumColumns(top) }, GlyphArrangementArray::ReturnLastCharacter));
			area.add(document.getBoundsOnRow(bottom, { 0, document.getNumColumns(bottom) }, GlyphArrangementArray::ReturnLastCharacter));

			auto displayBounds = area.getBounds();

			g.fillAll(Colour(0xCC333333));

			g.setColour(Colours::white.withAlpha(0.6f));
			g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 2.0f, 1.0f);

			auto transform = AffineTransform::scale(1.5f / scale).translated(displayBounds.getX() - 10, displayBounds.getY() - 10);

			g.saveState();
			g.addTransform(transform.inverted());

			g.setColour(Colours::black);

			for (int i = 0; i < colourScheme.types.size(); i++)
			{
				g.setColour(colourScheme.types[i].colour);
				auto glyphs = document.findGlyphsIntersecting(displayBounds, i);
				glyphs.draw(g);
			}

			g.restoreState();
		}

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

	void setVisibleRange(Range<int> visibleLines)
	{
		if (!isActive())
		{
			return;
		}
			

		displayedLines = visibleLines;
	
		auto numRows = doc.getNumRows();

		float a = (float)displayedLines.getStart() / (float)numRows;
		float invA = 1.0f - a;

		auto numToAdd = (float)(getNumLinesToShow() - displayedLines.getLength());

		auto before = roundToInt(a * numToAdd);
		auto after = roundToInt(invA * numToAdd);

		auto sStart = displayedLines.getStart() - before;
		auto sEnd = displayedLines.getEnd() + after;
		
		surrounding = Range<int>(sStart, sEnd);

		
		if (surrounding.getStart() < 0)
			surrounding = surrounding.movedToStartAt(0);
	}

	bool isActive() const
	{
		return doc.getNumRows() < 10000;
	}

	float lineToY(int lineNumber) const
	{
		if (surrounding.contains(lineNumber))
		{
			auto normalised = (float)(lineNumber - surrounding.getStart()) / (float)surrounding.getLength();
			return normalised * (float)getHeight();
		}
		else if (lineNumber < surrounding.getStart())
			return 0.0f;
		else
			return 1.0f;
	}

	int yToLine(float y) const
	{
		auto normalised = y / (float)getHeight();

		return (float)surrounding.getStart() + normalised * (float)surrounding.getLength();
	}

	ScopedPointer<HoverPreview> preview;

	void paint(Graphics& g)
	{
		if (!isActive())
		{
			return;
		}

		int offsetY = -1.0f;

		BigInteger shownLines;
		int index = 0;
		shownLines.setBit(colouredRectangles.size(), false);

		SparseSet<int> selectedLines;

		for (auto& s : doc.getSelections())
		{
			if (!s.isSingular())
			{
				auto start = s.head;
				auto end = s.tail;

				if (start.x > end.x)
					std::swap(start, end);
				if (start.x == end.x && start.y > end.y)
					std::swap(start, end);

				CodeDocument::Position startPos(doc.getCodeDocument(), start.x, start.y);
				CodeDocument::Position endPos(doc.getCodeDocument(), end.x, end.y);
				selectedLines.addRange({ startPos.getPosition(), endPos.getPosition() + 1 });
			}
		}

		RectangleList<float> selection;

		auto numCharacters = doc.getCodeDocument().getNumCharacters();
		auto numRectangles = colouredRectangles.size();

		for (auto& a : colouredRectangles)
		{
			if (surrounding.contains(a.lineNumber))
			{
				if (offsetY == -1.0f)
					offsetY = a.area.getY();

				bool shown = displayedLines.contains(a.lineNumber);

				shownLines.setBit(index, shown);

				Colour c = a.c;

				

				

				auto characterArea = a.area.translated(0.0f, -offsetY);

				if (selectedLines.contains(a.position))
					selection.add(characterArea.withLeft(0.0f));

				if (a.isWhitespace())
					continue;

				g.setColour(c.withMultipliedAlpha(shown ? 1.0f : 0.4f));

				characterArea.removeFromBottom(characterArea.getHeight() / 4.0f);
				characterArea.removeFromRight(characterArea.getWidth() * 0.2f);

				if (!a.upper)
					characterArea.removeFromTop(characterArea.getHeight() * 0.33f);

				

				g.fillRect(characterArea);

				
			}

			index++;
		}

		g.setColour(Colours::blue.withAlpha(0.4f));
		g.fillRectList(selection);

		auto y1 = lineToY(displayedLines.getStart());
		auto y2 = lineToY(displayedLines.getEnd());

		g.setColour(Colours::grey.withAlpha(0.2f));

		Rectangle<float> shownArea(0.0f, y1, (float)getWidth(), y2 - y1);

		g.fillRoundedRectangle(shownArea, 1.0f);
		g.drawRoundedRectangle(shownArea, 1.0f, 1.0f);

		if (preview != nullptr)
		{
			auto x = 0.0f;
			auto y = lineToY(preview->centerRow - preview->rows.getLength()/2);
			auto h = lineToY(preview->centerRow + preview->rows.getLength()/2) - y;
			auto w = (float)getWidth();

			g.setColour(Colours::white.withAlpha(0.1f));
			g.fillRect(x, y, w, h);

			

			
		}

	}

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

	Range<int> displayedLines;
	Range<int> surrounding;
	int offsetY = 0;
};

//==============================================================================
class mcl::CaretComponent : public juce::Component, private juce::Timer
{
public:
    CaretComponent (const TextDocument& document);
    void setViewTransform (const juce::AffineTransform& transformToUse);
    void updateSelections();

    //==========================================================================
    void paint (juce::Graphics& g) override;

private:
    //==========================================================================
    float squareWave (float wt) const;
    void timerCallback() override;
    juce::Array<juce::Rectangle<float>> getCaretRectangles() const;
    //==========================================================================
    float phase = 0.f;
    const TextDocument& document;
    juce::AffineTransform transform;
};




//==============================================================================
class mcl::GutterComponent : public juce::Component
{
public:
    GutterComponent (const TextDocument& document);
    void setViewTransform (const juce::AffineTransform& transformToUse);
    void updateSelections();

	float getGutterWidth() const
	{
		auto w = document.getCharacterRectangle().getWidth() * 6;

		return w * scaleFactor;
	}

    //==========================================================================
    void paint (juce::Graphics& g) override;

	void setScaleFactor(float newFactor)
	{
		scaleFactor = newFactor;
		repaint();
	}

private:

	float scaleFactor = 1.0f;

    juce::GlyphArrangement getLineNumberGlyphs (int row) const;
    //==========================================================================
    const TextDocument& document;
    juce::AffineTransform transform;
    Memoizer<int, juce::GlyphArrangement> memoizedGlyphArrangements;
};




//==============================================================================
class mcl::HighlightComponent : public juce::Component
{
public:
    HighlightComponent (const TextDocument& document);
    void setViewTransform (const juce::AffineTransform& transformToUse);
    void updateSelections();

    //==========================================================================
    void paint (juce::Graphics& g) override;

private:
	juce::Path getOutlinePath(const Selection& rectangles, Rectangle<float> clip) const;

	RectangleList<float> outlineRects;

    //==========================================================================
    bool useRoundedHighlight = true;
    const TextDocument& document;
    juce::AffineTransform transform;
    juce::Path outlinePath;
};


//==============================================================================
class mcl::TextEditor : public juce::Component,
						public CodeDocument::Listener
{
public:
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

	void codeDocumentTextDeleted(int startIndex, int endIndex) override
	{
		
		updateSelections();
		updateViewTransform();
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

	void codeDocumentTextInserted(const String& newText, int insertIndex) override
	{
		updateSelections();
		updateViewTransform();
	}

	int getFirstLineOnScreen() const
	{
		auto rows = document.getRangeOfRowsIntersecting(getLocalBounds().toFloat().transformed(transform.inverted()));

		return rows.getStart();
	}

	void setFirstLineOnScreen(int firstRow)
	{
		translation.y = -document.getVerticalPosition(firstRow, TextDocument::Metric::top) * viewScaleFactor;

		translateView(0.0f, 0.0f);
		//updateViewTransform();
	}

	CodeEditorComponent::ColourScheme colourScheme;

	juce::AffineTransform transform;

private:

	CodeDocument& docRef;

    //==========================================================================
    bool insert (const juce::String& content);
    void updateViewTransform();
    void updateSelections();
    void translateToEnsureCaretIsVisible();

    //==========================================================================
#if REMOVE
    void renderTextUsingAttributedStringSingle (juce::Graphics& g);
    void renderTextUsingAttributedString (juce::Graphics& g);
#endif

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
    CaretComponent caret;
    GutterComponent gutter;
    HighlightComponent highlight;
	CodeMap map;
	LinebreakDisplay linebreakDisplay;

	

	bool linebreakEnabled = true;

    float viewScaleFactor = 1.f;
	int maxLinesToShow = 0;
    juce::Point<float> translation;
    
    juce::UndoManager undo;

	bool showClosures = false;
	Selection currentClosure[2];


#if MCL_ENABLE_OPEN_GL
    juce::OpenGLContext context;
#endif
};

