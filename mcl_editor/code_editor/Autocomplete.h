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


/** A TokenCollection handles the database for the autocomplete popup.

You can register new providers that will populate the given token list with their entries
and add listeners to be notified when the token list changes.

For a default implementation that just scans the current text content, take a look at the 
SimpleDocumentTokenProvider class.

*/
class TokenCollection : public Thread,
						public AsyncUpdater
{
	

public:

	/** A Token is the entry that is being used in the autocomplete popup (or any other IDE tools
	    that might use that database. */
	struct Token: public ReferenceCountedObject
	{
		Token(const String& text) :
			tokenContent(text)
		{};

		/** Override the method and check whether the currently written input matches the token. */
		virtual bool matches(const String& input) const
		{
			return tokenContent.contains(input);
		}

		bool operator==(const Token& other) const
		{
			return tokenContent == other.tokenContent;
		}

		/** Override this method if you want to customize the code that is about to be inserted. */
		virtual String getCodeToInsert(const String& input) const { return tokenContent; }

		String markdownDescription;
		String tokenContent;
		
		/** The base colour for displaying the entry. */
		Colour c = Colours::white;

		/** The priority of the token. Tokens with higher priority will show up first in the token list. */
		int priority = 0;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Token);
	};

	/** Make it iteratable. */
	Token** begin() const { return tokens.begin(); }
	Token** end() const { return tokens.end(); }

	using List = ReferenceCountedArray<Token>;
	using TokenPtr = ReferenceCountedObjectPtr<Token>;
	
	/** A provider is a class that adds its tokens to the given list. 
	
		In order to use it, subclass from this and override the addTokens() method.
		
		Then whenever you want to rebuild the token list, call the signalRebuild method
		to notify the registered TokenCollection to rebuild its tokens.
	*/
	struct Provider
	{
		virtual ~Provider() {}

		/** Override this method and add all tokens to the given list. This method will be called on a background thread
		    to keep the UI thread responsive, so make sure you do not interfere with the message thread for longer than necessary. 
		*/
		virtual void addTokens(List& tokens) = 0;

		/** Call the TokenCollections rebuild method. This will not be executed synchronously, but on a dedicated thread. */
		void signalRebuild()
		{
			if (assignedCollection != nullptr)
				assignedCollection->signalRebuild();
		}

		WeakReference<TokenCollection> assignedCollection;
	};

	/** A Listener interface that will be notified whenever the token list was rebuilt. */
	struct Listener
	{
		virtual ~Listener() {};

		/** This method will be called on the message thread after the list was rebuilt. */
		virtual void tokenListWasRebuild() = 0;

		JUCE_DECLARE_WEAK_REFERENCEABLE(Listener);
	};

	void signalRebuild()
	{
		dirty = true;
		notify();
	}

	void run() override
	{
		while (!threadShouldExit())
		{
			rebuild();
			wait(3000);
		}
	}

	/** Register a token provider to this instance. Be aware that you can't register a token provider to multiple instances,
	    but this shouldn't be a problem. */
	void addTokenProvider(Provider* ownedProvider)
	{
		if (tokenProviders.isEmpty())
			startThread();

		tokenProviders.add(ownedProvider);
		ownedProvider->assignedCollection = this;
	}

	TokenCollection():
		Thread("TokenRebuildThread")
	{

	}


	~TokenCollection()
	{
		stopThread(1000);
	}

	bool hasEntries(const String& input) const
	{
		for (auto t : tokens)
		{
			if (t->matches(input))
				return true;
		}

		return false;
	}

	void addListener(Listener* l)
	{
		listeners.addIfNotAlreadyThere(l);
	}

	void removeListener(Listener* l)
	{
		listeners.removeAllInstancesOf(l);
	}

	void handleAsyncUpdate() override
	{
		for (auto l : listeners)
		{
			if (l != nullptr)
				l->tokenListWasRebuild();
		}
	}

	static int64 getHashFromTokens(const List& l)
	{
		int64 hash;

		for (auto& t : l)
		{
			hash += t->tokenContent.hashCode64();
		}

		return hash;
	}

	void rebuild()
	{
		if (dirty)
		{
			List newTokens;

			for (auto tp : tokenProviders)
				tp->addTokens(newTokens);

			Sorter ts;
			newTokens.sort(ts);

			auto newHash = getHashFromTokens(newTokens);

			if (newHash != currentHash)
			{
				tokens.swapWith(newTokens);
				triggerAsyncUpdate();
			}
			
			dirty = false;
		}
	}

	private:

	struct Sorter
	{
		static int compareElements(Token* first, Token* second)
		{
			if (first->priority > second->priority)
				return -1;

			if (first->priority < second->priority)
				return 1;

			return first->tokenContent.compareIgnoreCase(second->tokenContent);
		}
	};

private:

	OwnedArray<Provider> tokenProviders;
	Array<WeakReference<Listener>> listeners;
	List tokens;
	int64 currentHash;
	std::atomic<bool> dirty = { false };

	JUCE_DECLARE_WEAK_REFERENCEABLE(TokenCollection);
};

/** A TokenCollection::Provider subclass that scans the current document and creates a list of all tokens. */
struct SimpleDocumentTokenProvider : public TokenCollection::Provider,
									 public CoallescatedCodeDocumentListener
{
	SimpleDocumentTokenProvider(CodeDocument& doc) :
		CoallescatedCodeDocumentListener(doc)
	{}

	void codeChanged(bool, int, int) override
	{
		signalRebuild();
	}

	void addTokens(TokenCollection::List& tokens) override
	{
		CodeDocument::Iterator it(lambdaDoc);
		String currentString;

		while (!it.isEOF())
		{
			auto c = it.nextChar();

			if (CharacterFunctions::isLetter(c) || (c == '_') || (currentString.isNotEmpty() && CharacterFunctions::isLetterOrDigit(c)))
				currentString << c;
			else
			{


				if (currentString.length() > 2)
				{
					bool found = false;

					for (auto& t : tokens)
					{
						if (t->tokenContent == currentString)
						{
							found = true;
							break;
						}
					}

					if(!found)
						tokens.add(new TokenCollection::Token(currentString));
				}
					

				currentString = {};
			}
		}
	}
};


struct Autocomplete : public Component,
	public KeyListener,
	public ScrollBar::Listener
{
	using Token = TokenCollection::Token;

	struct Item : public Component
	{
		Item(TokenCollection::TokenPtr t, const String& input_) :
			token(t),
			input(input_)
		{
			jassert(t->matches(input));
			setRepaintsOnMouseActivity(true);
		}

		void mouseUp(const MouseEvent& e) override;

		AttributedString createDisplayText() const;

		void paint(Graphics& g) override;

		bool isSelected()
		{
			if (auto p = findParentComponentOfClass<Autocomplete>())
			{
				if (isPositiveAndBelow(p->viewIndex, p->items.size()))
					return p->items[p->viewIndex] == this;
			}

			return false;
		}

		TokenCollection::TokenPtr token;
		String input;
	};

	Autocomplete(TokenCollection& tokenCollection_, const String& input) :
		tokenCollection(tokenCollection_),
		scrollbar(true),
		shadow(DropShadow(Colours::black.withAlpha(0.7f), 5, {}))
	{
		addAndMakeVisible(scrollbar);
		setInput(input);

		shadow.setOwner(this);
		scrollbar.addListener(this);
	}

	juce::DropShadower shadow;

	bool keyPressed(const KeyPress& key, Component*);

	String getCurrentText() const
	{
		if (isPositiveAndBelow(viewIndex, items.size()))
		{
			return items[viewIndex]->token->getCodeToInsert(currentInput);
		}

		return {};
	}

	void scrollBarMoved(ScrollBar* scrollBarThatHasMoved, double newRangeStart) override
	{
		displayedRange = displayedRange.movedToStartAt((int)newRangeStart);
		resized();
	}

	void selectNextItem(bool showNext, int delta = 1)
	{
		if (showNext)
			viewIndex = jmin(viewIndex + delta, items.size() - 1);
		else
			viewIndex = jmax(0, viewIndex - delta);

		setDisplayedIndex(viewIndex);

		setDisplayedIndex(viewIndex);
	}

	void mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& wheel) override
	{
		auto start = displayedRange.getStart();

		start -= (wheel.deltaY * 8);

		displayedRange = displayedRange.movedToStartAt(start);

		if (displayedRange.getEnd() >= items.size())
			displayedRange = displayedRange.movedToEndAt(items.size() - 1);
		if (displayedRange.getStart() < 0)
			displayedRange = displayedRange.movedToStartAt(0);

		scrollbar.setCurrentRange({ (double)displayedRange.getStart(), (double)displayedRange.getEnd() }, dontSendNotification);

		resized();
	}

	void setDisplayedIndex(int index)
	{
		if (!displayedRange.contains(viewIndex))
		{
			if (viewIndex < displayedRange.getStart())
				displayedRange = displayedRange.movedToStartAt(viewIndex);
			else
				displayedRange = displayedRange.movedToEndAt(viewIndex + 1);
		}

		if (displayedRange.getEnd() > items.size())
			displayedRange = displayedRange.movedToEndAt(items.size() - 1);

		if (displayedRange.getStart() < 0)
			displayedRange = displayedRange.movedToStartAt(0);


		scrollbar.setCurrentRange({ (double)displayedRange.getStart(), (double)displayedRange.getEnd() });

		resized();
		repaint();
	}

	Item* createItem(const TokenCollection::TokenPtr t, const String& input)
	{
		return new Item(t, input);
	}

	void setInput(const String& input)
	{
		currentInput = input;

		auto currentlyDisplayedItem = getCurrentText();
		items.clear();

		viewIndex = 0;

		for (auto t : tokenCollection)
		{
			if (t->matches(input))
			{
				if (t->tokenContent == currentlyDisplayedItem)
					viewIndex = items.size();

				items.add(createItem(t, input));
				addAndMakeVisible(items.getLast());
			}
		}

		int numLinesFull = 7;

		if (isPositiveAndBelow(numLinesFull, items.size()))
		{
			displayedRange = { 0, numLinesFull };

			displayedRange = displayedRange.movedToStartAt(viewIndex);

			if (displayedRange.getEnd() >= items.size())
			{
				displayedRange = displayedRange.movedToEndAt(items.size() - 1);
			}

		}
		else
			displayedRange = { 0, items.size() };

		scrollbar.setRangeLimits({ 0.0, (double)items.size() });

		setDisplayedIndex(viewIndex);

		auto h = getNumDisplayedRows() * getRowHeight();

		setSize(400, h);
		resized();
		repaint();
	}

	int getRowHeight() const
	{
		return 28;
	}

	void paint(Graphics& g) override
	{
		g.fillAll(Colour(0xFF282828));
		auto b = getLocalBounds().toFloat();

	}



	void paintOverChildren(Graphics& g) override
	{
		auto b = getLocalBounds();
		g.setColour(Colour(0xFF222222));
		g.drawRect(b.toFloat(), 1.0f);
	}

	int getNumDisplayedRows() const
	{
		return displayedRange.getLength();
	}

	void resized() override
	{
		auto scrollbarVisible = items.size() != displayedRange.getLength();

		scrollbar.setVisible(scrollbarVisible);

		auto b = getLocalBounds();

		if (scrollbarVisible)
		{

			scrollbar.setBounds(b.removeFromRight(10));
		}

		auto h = getRowHeight();

		Rectangle<int> itemBounds = { b.getX(), b.getY() - displayedRange.getStart() * h, b.getWidth(), h };

		for (auto i : items)
		{
			i->setBounds(itemBounds);
			itemBounds.translate(0, h);
		}
	}

	OwnedArray<Item> items;
	int viewIndex = 0;

	Range<int> displayedRange;
	String currentInput;

	TokenCollection& tokenCollection;
	ScrollBar scrollbar;
};




}