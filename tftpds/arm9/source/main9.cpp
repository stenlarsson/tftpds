#include <nds.h>
#include <nds/arm9/console.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

//Fat support - Smiths
#include <fat.h>
//SRAM Stuff again
#define SRAM_START ((u8*)0x0A000000)
#define SRAM_END ((u8*)0x0A03FFFF) //256KB
#include "sramfile.h"

#include <dswifi9.h>

#include <driver.h>

#include "tftpserver.h"
#include "cartlib.h"
#include "bootdialog.h"


BootDialog* dialog = NULL;

// some functions needed by wifi lib
extern "C" {
	void * sgIP_malloc(int size)
	{
		return malloc(size);
	}

	void sgIP_free(void *ptr)
	{
		free(ptr);
	}

	void sgIP_dbgprint(char *txt, ...)
	{
		va_list args;
		va_start(args,txt);
		vprintf(txt,args);
	}

	// wifi timer function, to update internals of sgIP
	void Timer_50ms(void) {
		Wifi_Timer(50);
	}

	// notification function to send fifo message to arm7
	void arm9_synctoarm7()
	{
		IPC_SendSync(0);
	}

	// interrupt handler to receive fifo messages from arm7
	void arm9_ipc_sync()
	{
		Wifi_Sync();
	}
}


// function used by our own server for timeouts
void ResetTimer()
{
	// make a 1 Hz timer
	TIMER0_CR = 0;
	TIMER1_CR = 0;
	TIMER0_DATA = 0xffff - 0x7fff;
	TIMER1_DATA = 0;
	TIMER0_CR = TIMER_DIV_1024;
	TIMER1_CR = TIMER_CASCADE;
}

// function used by our own server for timeouts
int GetTimer()
{
	return TIMER1_DATA;
}

void SetupWifi()
{
	// send fifo message to initialize the arm7 wifi
	REG_IPC_FIFO_CR = IPC_FIFO_ENABLE | IPC_FIFO_SEND_CLEAR; // enable & clear FIFO

	u32 Wifi_pass= Wifi_Init(WIFIINIT_OPTION_USELED);
	REG_IPC_FIFO_TX=0x12345678;
	REG_IPC_FIFO_TX=Wifi_pass;

	*((volatile u16 *)0x0400010E) = 0; // disable timer3

	irqSet(IRQ_TIMER3, Timer_50ms); // setup timer IRQ
	irqEnable(IRQ_TIMER3);
	irqSet(IRQ_IPC_SYNC, arm9_ipc_sync);
	irqEnable(IRQ_IPC_SYNC);
	REG_IPC_SYNC = IPC_SYNC_IRQ_ENABLE;

	REG_IPC_FIFO_CR = IPC_FIFO_ENABLE | IPC_FIFO_RECV_IRQ; // enable FIFO IRQ

	Wifi_SetSyncHandler(arm9_synctoarm7); // tell wifi lib to use our handler to notify arm7

	// set timer3
	*((volatile u16 *)0x0400010C) = -6553; // 6553.1 * 256 cycles = ~50ms;
	*((volatile u16 *)0x0400010E) = 0x00C2; // enable, irq, 1/256 clock

	printf("Waiting for ARM7...\n");

	while(Wifi_CheckInit()==0)
	{ // wait for arm7 to be initted successfully
		swiWaitForVBlank();
	}
		
	// wifi init complete - wifi lib can now be used!

	printf("Connecting via WFC data...\n");
	Wifi_AutoConnect();
}

bool IsConnected()
{
	static int status = ASSOCSTATUS_DISCONNECTED;

	char* statusMessages[] = {
		"disconnected",
		"searching",
		"authenticating",
		"associating",
		"acquiring dhcp",
		"associated",
		"cannot connect"
	};

	if(status != Wifi_AssocStatus())
	{
		status = Wifi_AssocStatus();
		printf("...%s\n", statusMessages[status]);
	}

	return (status == ASSOCSTATUS_ASSOCIATED);
}

void DumpMemory(u8* address)
{
	int x, y;
	u8* bufferpos;
	char strbuffer[30];

	bufferpos = address;
	for(y = 0; y < 20; y++)
	{
		printf("%02x: ", ((u32)bufferpos) & 0xFF);
		for(x = 0; x < 8; x++)
		{
			printf("%02x", *bufferpos);

			if( *bufferpos >= 32 && *bufferpos < 127 )
			{
				strbuffer[x] = *bufferpos;
			}
			else
			{
				strbuffer[x] = '.';
			}

			bufferpos++;
		}

		strbuffer[x] = '\0';

		printf(" %s\n", strbuffer);
	}
}

void WaitForKeyPress()
{
	scanKeys();
	do
	{
		swiWaitForVBlank();
		scanKeys();
	}
	while(keysDown() == 0);
}

void TcpTest()
{
	int sock = socket(PF_INET, SOCK_STREAM, 0);
	if(sock == -1)
	{
		printf("socket() failed\n");
		return;
	}

	struct sockaddr_in sain;
	sain.sin_family = AF_INET;
	sain.sin_port = htons(80);
	sain.sin_addr.s_addr = INADDR_ANY;
	int result = bind(sock, (struct sockaddr *)&sain, sizeof(sain));
	if(result == -1)
	{
		printf("bind() failed\n");
		return;
	}

	result = listen(sock, 1);
	if(result == -1)
	{
		printf("listen() failed\n");
		return;
	}

	printf("TcpTest Listening...\n");

	int len = sizeof(sain);
	int sock2 = accept(sock, (struct sockaddr *)&sain, &len);
	if(sock2 == -1)
	{
		printf("accept() failed\n");
		return;
	}

	printf("accepted %lu.%lu.%lu.%lu\n",
		(sain.sin_addr.s_addr >>  0) & 0xFF,
		(sain.sin_addr.s_addr >>  8) & 0xFF,
		(sain.sin_addr.s_addr >> 16) & 0xFF,
		(sain.sin_addr.s_addr >> 24) & 0xFF);

	// set socket to non-blocking
	//int i = 1;
	//ioctl(sock, FIONBIO, &i);

	printf("received \e[s         0 bytes");

	ResetTimer();
	char buffer[10240];
	unsigned int recieved = 0;
	int length;
	while((length = recv(sock2, buffer, sizeof(buffer), 0)) != 0)
	{
		if(length == -1 && errno != EAGAIN)
		{
			break;
		}
		else
		{
			recieved += length;
			printf("\e[u\e[0K%10u", recieved);
		}
	}

	int elapsed = GetTimer();
	printf("\nelapsed %i s\n", elapsed);
	printf("%f kb/s\n", recieved/(double)elapsed/1024.0);

	closesocket(sock2);
	closesocket(sock);
}

void TcpSendTest()
{
	int sock = socket(PF_INET, SOCK_STREAM, 0);
	if(sock == -1)
	{
		printf("socket() failed\n");
		return;
	}

	struct sockaddr_in sain;
	sain.sin_family = AF_INET;
	sain.sin_port = htons(4000);
	sain.sin_addr.s_addr = 0x0100A8C0;
	int result = connect(sock, (struct sockaddr *)&sain, sizeof(sain));
	if(result == -1)
	{
		printf("connect() failed\n");
		return;
	}

	// set socket to non-blocking
	int i = 0;
	ioctl(sock, FIONBIO, &i);

	printf("sent \e[s         0 bytes");

	ResetTimer();
	char buffer[10240];
	unsigned int sent = 0;
	do {
		int length = send(sock, buffer, sizeof(buffer), 0);
		if(length == -1)
		{
			if(errno == EAGAIN)
			{
				continue;
			}
			//printf("send() failed\n");
			perror("send");
			return;
		}

		sent += length;
		printf("\e[u\e[0K%10u", sent);
	} while (sent < 200*1024);

	int elapsed = GetTimer();
	printf("\nelapsed %i s\n", elapsed);
	printf("%f kb/s\n", sent/(double)elapsed/1024.0);

	closesocket(sock);
}

int main()
{
	IPC->mailData=0;
	IPC->mailSize=0;

	consoleDemoInit();
	irqInit();
	irqEnable(IRQ_VBLANK); // needed by swiWaitForVBlank()

	fatInitDefault(); // initialize FAT - Smiths
	
	FwGui::Driver gui;
	
	printf("tftpds v2.5-sr\n");
	printf("-----------\n");
	printf("Press SELECT to back up SRAM Bank 1\n");
	printf("-----------\n");

	try
	{
		SetupWifi();

		// map gba cartridge to arm9
		REG_EXMEMCNT &= ~0x80;
		VisolySetFlashBaseAddress(0);

		dialog = new BootDialog();
		gui.SetActiveDialog(dialog);
		while(!IsConnected())
		{
			swiWaitForVBlank();
			gui.Tick();
		}

		unsigned long ip = Wifi_GetIP();
		printf("ip: %lu.%lu.%lu.%lu\n",
			(ip >>  0) & 0xFF,
			(ip >>  8) & 0xFF,
			(ip >> 16) & 0xFF,
			(ip >> 24) & 0xFF);

		TftpServer server;

		while(true)
		{
			while(!server.Poll())
			{
				gui.Tick();
				if(keysDown() & KEY_X)
				{
					TcpTest();
				}
				if(keysDown() & KEY_Y)
				{
					TcpSendTest();
				}
				//Back up SRAM Bank 1 with SELECT - Smiths
				if(keysDown() & KEY_SELECT)
				{
					BackupSRAM();
				}
				swiWaitForVBlank();
			}

			dialog->ScanItems();
			dialog->RefreshButtons();
			dialog->Repaint();
		}
		

	}
	catch(const char* exception)
	{
		printf("\n*** Exception\n%s\n", exception);
	}

	while(true)
	{
		swiWaitForVBlank();
	}
}

//Get that first 64KB! - Smiths
void BackupSRAM() {

FILE * savedata = fopen ("fat1:/bank1.sav", "wb");
printf("Backing up Bank 1\n");
u8* start = SRAM_START; //Beginning of SRAM
u8* bank1 = SRAM_START+65535; //64KB = bank1 (rest need bank switching I believe)
u8* end = SRAM_END; //unused, but eventually (hopefully) map to end of SRAM for proper 256KB dump
char strbuffer[8]; //only likes to work in blocks of 8 (1 byte at a time!)

while (start < bank1 - 1) {
	VisolyModePreamble (); //Don't know if needed, FLinker has it though
	memcpy(strbuffer, start, sizeof(strbuffer));
	fwrite((u8*)strbuffer, 1, sizeof(strbuffer), savedata); //1 byte at a time, whee!
	start = start+8; //next byte, please
	}

fclose (savedata);
printf("Done!\n");
printf("File: bank1.sav in root of Slot-1 Device\n");
}

