/*
  ==============================================================================

    This file was auto-generated!

  ==============================================================================
*/

#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent():
	editor(doc),
	old(doc, &tok)
{
	addAndMakeVisible(editor);
	addAndMakeVisible(old);
	

	editor.setColour(CodeEditorComponent::ColourIds::backgroundColourId, Colour(0xCC38383A));
	editor.setOpaque(false);
	editor.setColour(CodeEditorComponent::ColourIds::lineNumberTextId, Colours::white);

	editor.setColour(CodeEditorComponent::ColourIds::lineNumberBackgroundId, Colour(0x33FFFFFF));



	

	editor.setColour(CodeEditorComponent::backgroundColourId, Colour(0xff333333));
	editor.setColour(CodeEditorComponent::ColourIds::defaultTextColourId, Colour(0xFFCCCCCC));
	editor.setColour(CodeEditorComponent::ColourIds::lineNumberTextId, Colour(0xFFCCCCCC));
	editor.setColour(CodeEditorComponent::ColourIds::lineNumberBackgroundId, Colour(0xff363636));
	editor.setColour(CodeEditorComponent::ColourIds::highlightColourId, Colour(0x66AAAAAA));
	editor.setColour(CaretComponent::ColourIds::caretColourId, Colour(0xFFDDDDDD));
	editor.setColour(ScrollBar::ColourIds::thumbColourId, Colour(0x3dffffff));




	old.setFont(old.getFont().withHeight(16.0f));



    setSize (600, 400);

	Component::SafePointer<Component> s = &editor;

	auto f = [s]()
	{
		if(s.getComponent())
			s.getComponent()->grabKeyboardFocus();
	};

	MessageManager::callAsync(f);

	
}

MainComponent::~MainComponent()
{
}

//==============================================================================
void MainComponent::paint (Graphics& g)
{
	g.fillAll(Colour(0xff262626));
}

void MainComponent::resized()
{
	auto b = getLocalBounds();

	editor.setBounds(b.reduced(5));// .removeFromLeft(getWidth() / 2));
	//old.setBounds(b);
}
