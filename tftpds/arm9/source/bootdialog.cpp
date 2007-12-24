#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "bootdialog.h"
#include "cartlib.h"
#include "boot9.h"

#define min(x, y) ((x) < (y) ? (x) : (y))

using namespace FwGui;

BootDialog::BootDialog()
:	Dialog("BOOT", NUM_BUTTONS + 2),
	numItems(0),
	scrollOffset(0)
{
	memset(items, 0, sizeof(items));

	for(int i = 0; i < NUM_BUTTONS; i++)
	{
		buttons[i] = new Button(4, 27*(i+1)+1, 222, 24, "");
		AddControl(buttons[i]);
	}

	up = new ImageButton(231, 28, STDIMG_UP);
	AddControl(up);
	down = new ImageButton(231, 167, STDIMG_DOWN);
	AddControl(down);

	ScanItems();
	RefreshButtons();
}

BootDialog::~BootDialog()
{
	delete up;
	delete down;

	for(int i = 0; i < NUM_BUTTONS; i++)
	{
		delete buttons[i];
	}
}


static const unsigned char nintendo_logo[] =
{
	0x24,0xFF,0xAE,0x51,0x69,0x9A,0xA2,0x21,0x3D,0x84,0x82,0x0A,0x84,0xE4,0x09,0xAD,
	0x11,0x24,0x8B,0x98,0xC0,0x81,0x7F,0x21,0xA3,0x52,0xBE,0x19,0x93,0x09,0xCE,0x20,
	0x10,0x46,0x4A,0x4A,0xF8,0x27,0x31,0xEC,0x58,0xC7,0xE8,0x33,0x82,0xE3,0xCE,0xBF,
	0x85,0xF4,0xDF,0x94,0xCE,0x4B,0x09,0xC1,0x94,0x56,0x8A,0xC0,0x13,0x72,0xA7,0xFC,
	0x9F,0x84,0x4D,0x73,0xA3,0xCA,0x9A,0x61,0x58,0x97,0xA3,0x27,0xFC,0x03,0x98,0x76,
	0x23,0x1D,0xC7,0x61,0x03,0x04,0xAE,0x56,0xBF,0x38,0x84,0x00,0x40,0xA7,0x0E,0xFD,
	0xFF,0x52,0xFE,0x03,0x6F,0x95,0x30,0xF1,0x97,0xFB,0xC0,0x85,0x60,0xD6,0x80,0x25,
	0xA9,0x63,0xBE,0x03,0x01,0x4E,0x38,0xE2,0xF9,0xA2,0x34,0xFF,0xBB,0x3E,0x03,0x44,
	0x78,0x00,0x90,0xCB,0x88,0x11,0x3A,0x94,0x65,0xC0,0x7C,0x63,0x87,0xF0,0x3C,0xAF,
	0xD6,0x25,0xE4,0x8B,0x38,0x0A,0xAC,0x72,0x21,0xD4,0xF8,0x07,
};

void BootDialog::ScanItems()
{
	char* ptr = (char*)0x8000000;
	int i = 0;
	while(i < MAX_ITEMS && ptr < (char*)0xA000000)
	{
		bool pass = (memcmp("PASS", ptr+0xAC, 4) == 0);
		bool gbalogo = (memcmp(nintendo_logo, ptr+0x4, sizeof(nintendo_logo)) == 0);
		bool loader = (memcmp("NDS loader for GBA flashcards", ptr+0x21, 29) == 0);

		if(loader || (pass && gbalogo))
		{
			items[i].filetype = FILETYPE_DS_GBA;
			strncpy(items[i].title, ptr+0xA0, 12);
		}
		else if(pass)
		{
			items[i].filetype = FILETYPE_NDS;
			strcpy(items[i].title, "unknown");
		}
		else if(gbalogo)
		{
			items[i].filetype = FILETYPE_GBA;
			strncpy(items[i].title, ptr+0xA0, 12);
		}
		else
		{
			items[i].filetype = FILETYPE_NONE;
		}

		if(items[i].filetype != FILETYPE_NONE)
		{
			items[i].address = ptr - 0x8000000;
			i++;
		}

		ptr += 0x40000;
	}

	numItems = i;

	for(; i < MAX_ITEMS; i++)
	{
		memset(items + i, 0, sizeof(BootItem));
	}
}

void BootDialog::RefreshButtons()
{
	up->SetEnabled(scrollOffset > 0);
	down->SetEnabled(scrollOffset + NUM_BUTTONS < numItems);

	for(int i = 0; i < NUM_BUTTONS; i++)
	{
		Button* button = buttons[i];
		BootItem* item = &items[i + scrollOffset];

		if(item->filetype != FILETYPE_NONE)
		{
			char* filetypes[] = {
				"none",
				"gba",
				"nds",
				"ds.gba"
			};

			char buf[100];
			sprintf(
				buf,
				"%s.%s (%08x)",
				item->title,
				filetypes[item->filetype],
				(u32)item->address);
			button->SetText(buf);
			button->SetEnabled(true);
			button->SetData(item);
		}
		else
		{
			button->SetText("");
			button->SetEnabled(false);
			button->SetData(NULL);
		}
	}
}

void BootDialog::ControlClicked(Control* control)
{
	if(control == up)
	{
		scrollOffset--;
		RefreshButtons();
		Repaint();
	}
	else if(control == down)
	{
		scrollOffset++;
		RefreshButtons();
		Repaint();
	}
	else
	{
		BootItem* item = (BootItem*)control->GetData();

		if(item != NULL)
		{
			VisolySetFlashBaseAddress((u32)item->address);
			if(item->filetype == FILETYPE_GBA)
			{
				printf("Booting a .gba-file...\n");
				BootGbaARM9();
			}
			else
			{
				printf("Booting a .ds.gba-file...\n");
				BootDsGbaARM9();
			}
		}
	}
}

void BootDialog::KeyUp()
{
	if(selectedControl == 0)
	{
		if(scrollOffset > 0)
		{
			scrollOffset--;
			RefreshButtons();
			Repaint();
		}
	}
	else
	{
		selectedControl--;
		Repaint();
	}
}

void BootDialog::KeyDown()
{
	if(selectedControl == min(numItems, NUM_BUTTONS) - 1)
	{
		if(scrollOffset + NUM_BUTTONS < numItems)
		{
			scrollOffset++;
			RefreshButtons();
			Repaint();
		}
	}
	else
	{
		selectedControl++;
		Repaint();
	}
}

void BootDialog::KeyLeft()
{
}

void BootDialog::KeyRight()
{
}
