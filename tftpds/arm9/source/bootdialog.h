#pragma once

#include <dialog.h>
#include <label.h>
#include <button.h>
#include <imagebutton.h>

#define NUM_BUTTONS 6
#define MAX_ITEMS 32

enum Filetype
{
	FILETYPE_NONE = 0,
	FILETYPE_GBA,
	FILETYPE_NDS,
	FILETYPE_DS_GBA
};

struct BootItem
{
	Filetype filetype;
	char title[13];
	char* address;
};	

class BootDialog : public FwGui::Dialog
{
public:
	BootDialog();
	virtual ~BootDialog();

	void ScanItems();
	void RefreshButtons();
	virtual void ControlClicked(FwGui::Control* control);
	virtual void KeyUp();
	virtual void KeyDown();
	virtual void KeyLeft();
	virtual void KeyRight();

private:
	FwGui::ImageButton* up;
	FwGui::ImageButton* down;
	FwGui::Button* buttons[NUM_BUTTONS];
	BootItem items[MAX_ITEMS];
	int numItems;
	int scrollOffset;
};
