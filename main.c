/*********************************************************************
 *
 * Code testing the basic functionality of STM32 on VL discovery kit
 * The code displays message via UART1 using printf mechanism
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of either the GNU General Public License version 2
 * or the GNU Lesser General Public License version 2.1, both as
 * published by the Free Software Foundation.
 *
 *********************************************************************
 * FileName:    main.c
 * Depends:
 * Processor:   STM32F100RBT6B
 *
 * Author               Date       Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Kubik                13.11.2010 Initial code
 * Kubik                14.11.2010 Added debug code
 * Kubik                15.11.2010 Debug now goes to UART2
 * Kubik                ??.11.2010 Added test code for quadrature encoder
 * Kubik                 4.12.2010 Implemented SD/MMC card support
 ********************************************************************/

// important:
//
// - commented out int fputc(int ch, FILE * f) in printf.c and redefined in main.h to get printf sending directly to USART1
// - commented out struct _reent *_impure_ptr = &r; in printf.c to prevent a linker error


//-------------------------------------------------------------------
// Includes
#include <stddef.h>
#include <stdio.h>
#include <sys/unistd.h>
#include <string.h>
#include <inttypes.h>
#include "stm32f10x.h"
#include "STM32_Discovery.h"
#include "debug.h"
#include "uart.h"
#include "diskio.h"
#include "ff.h"



/* This function is used to transmit a string of characters via
 * the USART specified in USARTx.
 *
 * It takes two arguments: USARTx --> can be any of the USARTs e.g. USART1, USART2 etc.
 * 						   (volatile) char *s is the string you want to send
 *
 * Note: The string has to be passed to the function as a pointer because
 * 		 the compiler doesn't know the 'string' data type. In standard
 * 		 C a string is just an array of characters
 *
 * Note 2: At the moment it takes a volatile char because the received_string variable
 * 		   declared as volatile char --> otherwise the compiler will spit out warnings
 * */
void USART_puts(USART_TypeDef* USARTx, volatile char *s)
{
	while(*s)
	{
		// wait until data register is empty
		while( !(USARTx->SR & 0x00000040) );

		USART_SendData(USARTx, *s);
		*s++;
	}
}

unsigned char USART_getc(USART_TypeDef* USARTx)
{
	// wait until data register is empty
	/* Wait until the data is ready to be received. */
	while ((USARTx->SR & USART_SR_RXNE) == 0);

	// read RX data, combine with DR mask (we only accept a max of 9 Bits)
	return USARTx->DR & 0x1FF;
}

// This will print on usart 1 (the original function has been commented out in printf.c !!!
int fputc(int ch, FILE * f)
{
	/* Wait until transmit finishes */
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);

	/* Transmit the character using USART1 */
	USART_SendData(USART1, (u8) ch);


	return ch;
}

// receives an 32 bit integer from serial port. (little endian)
uint32_t USART_recv_dword(USART_TypeDef* USARTx)
{
	uint32_t i, result = 0;
	for(i = 0; i < 4; i++)
	{
		uint32_t recv = USART_getc(USART1) << (i * 8);
		result |= recv;
	}

	return result;
}


//-------------------------------------------------------------------
// Defines

//---------------------------------------------------------------------------
// Static variables

//---------------------------------------------------------------------------
// Local functions

// Redirecting of printf to UARTx - this works for Atollic
// fd selects stdout or stderr - that's UARTx or UARTd
int _write_r(void *reent, int fd, char *ptr, size_t len) {
	size_t counter = len;
	USART_TypeDef *Usart;

	if(fd == STDOUT_FILENO) {			// stdout goes to UARTx
		Usart = UARTx;
	} else if(fd == STDERR_FILENO) {
		Usart = UARTd;					// stderr goes to UARTd
	} else {
		return len;
	}

	while(counter-- > 0) {				// Send the character from the buffer to UART
		while (USART_GetFlagStatus(Usart, USART_FLAG_TXE) == RESET);
		USART_SendData(Usart, (uint8_t) (*ptr));
		ptr++;
	}

	return len;
}


/*******************************************************************************
* Function Name  : delay
* Description    : Inserts a time delay.
* Input          : None
* Output         : None
* Return         : None
*******************************************************************************/
static void delay(void)
{
  vu32 i = 0;

  for(i = 0xFF; i != 0; i--)
  {
  }
}

//
//--- Helpers for FAT support
//

FATFS Fatfs[1];		/* File system object for each logical drive */
BYTE Buff[1*1024] __attribute__ ((aligned (4))) ;		/* Working buffer */
char Lfname[512];
FATFS *fs;				/* Pointer to file system object */
DWORD acc_size;				/* Work register for fs command */
WORD acc_files, acc_dirs;
FILINFO Finfo;

static
void put_rc (FRESULT rc)
{
	const char *p;
	static const char str[] =
		"OK\0" "NOT_READY\0" "NO_FILE\0" "FR_NO_PATH\0" "INVALID_NAME\0" "INVALID_DRIVE\0"
		"DENIED\0" "EXIST\0" "RW_ERROR\0" "WRITE_PROTECTED\0" "NOT_ENABLED\0"
		"NO_FILESYSTEM\0" "INVALID_OBJECT\0" "MKFS_ABORTED\0";
	FRESULT i;

	for (p = str, i = 0; i != rc && *p; i++) {
		while(*p++);
	}
	printf("rc=%u FR_%s\n", (UINT)rc, p);
}

static
FRESULT scan_files (char* path)
{
	DIR dirs;
	FRESULT res;
	BYTE i;
	char *fn;


	if ((res = f_opendir(&dirs, path)) == FR_OK) {
		i = strlen(path);
		while (((res = f_readdir(&dirs, &Finfo)) == FR_OK) && Finfo.fname[0]) {
#if _USE_LFN
				fn = *Finfo.lfname ? Finfo.lfname : Finfo.fname;
#else
				fn = Finfo.fname;
#endif
			if (Finfo.fattrib & AM_DIR) {
				acc_dirs++;
				*(path+i) = '/'; strcpy(path+i+1, fn);
				printf("%s\r\n", path);

				res = scan_files(path);
				*(path+i) = '\0';

				if (res != FR_OK) break;
			} else {

				printf("%s/%s - %d Bytes\r\n", path, fn, Finfo.fsize);

				acc_files++;
				acc_size += Finfo.fsize;
			}
		}
	}

	return res;
}

//---------------------------------------------------------------------------
// Local functions

int main(void) {
	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_ClocksTypeDef RCC_ClockFreq;
    uint32_t dw;
    uint16_t w;
    uint8_t b;
    uint32_t i, j;

    char file_name[64];
    uint32_t file_size;

    static uint8_t buffer[1024];

	//
	// Clock initialization
	//

	// Output SYSCLK clock on MCO pin
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	RCC_MCOConfig(RCC_MCO_SYSCLK);

	//
	// Configure debug trigger as output (GPIOA pin 0)
	//

    GPIO_WriteBit(GPIOA, GPIO_Pin_0, Bit_SET);
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	//
	// Configure peripherals used - basically enable their clocks to enable them
	//

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC, ENABLE);

    //
    // Configure LEDs - we use them as an indicator the platform is alive somehow
    //

    // PC8 is blue LED
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // PC9 is green LED - some setting inherited from blue LED config code!
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // Blue LED on - we're running!
    GPIO_WriteBit(GPIOC, GPIO_Pin_8, Bit_SET);

    //
    // GPIO initialization
    //

    // none used at the moment besides the LEDs and stuff that other peripherals configured for themselves

	//
	// UART initialization
	//

    InitializeUarts();

    //
    // Setup SysTick Timer for 1 millisecond interrupts, also enables Systick and Systick-Interrupt
    //

	if (SysTick_Config(SystemCoreClock / 1000)) {
        DEBUG(("Systick failed, system halted\n"));
		while (1);
	}

    //
    // Main program loop
    //

    // Show welcome message (From now on use USART1 when iprintf is called
	//USART_puts(USART1, "UART test " __DATE__ "\r\n"); // just send a message to indicate that it works

	printf("UART test " __DATE__ "\n");

    RCC_GetClocksFreq(&RCC_ClockFreq);

    printf("SYSCLK = %ld  HCLK = %ld  PCLK1 = %ld  PCLK2 = %ld ADCCLK = %ld\r\n", RCC_ClockFreq.SYSCLK_Frequency,
												RCC_ClockFreq.HCLK_Frequency,
												RCC_ClockFreq.PCLK1_Frequency,
												RCC_ClockFreq.PCLK2_Frequency,
												RCC_ClockFreq.ADCCLK_Frequency);


    // SD Card Test
    //
    // Disk initialization - this sets SPI1 to correct mode and initializes the SD/MMC card
    //

    if(disk_initialize(0) & STA_NOINIT) {
    	printf("No memory card found\n");
    } else {
        disk_ioctl(0, GET_SECTOR_COUNT, &dw);
        disk_ioctl(0, MMC_GET_TYPE, &b);
        if(b & CT_MMC) {
        	printf("MMC");
        } else if(b & CT_SD1) {
        	printf("SD 1.0");
        } else if(b & CT_SD2) {
        	printf("SD 2.0");
        } else {
        	printf("unknown");
        }
        printf(" card found, %" PRIu32 "MB\n", dw >> 11);
    }

	char ptr[1024] = {0};


	long p;
	FRESULT res;
    f_mount(0, &Fatfs[0]);

	res = f_getfree("", (DWORD*)&p, &fs);
	if(res) {
		put_rc(res);
	} else {
		printf("FAT type = %d (%s)\nBytes/Cluster = %d\nNumber of FATs = %d\n"
				"Root DIR entries = %d\nSectors/FAT = %d\nNumber of clusters = %d\n"
				"FAT start (lba) = %d\nDIR start (lba,clustor) = %d\nData start (lba) = %d\n\n",
				(WORD)fs->fs_type,
				(fs->fs_type==FS_FAT12) ? "FAT12" : (fs->fs_type==FS_FAT16) ? "FAT16" : "FAT32",
				(DWORD)fs->csize * 512, (WORD)fs->n_fats,
				fs->n_rootdir, fs->fsize, (DWORD)fs->n_fatent - 2,
				fs->fatbase, fs->dirbase, fs->database
		);

	}

	acc_size = acc_files = acc_dirs = 0;


#if _USE_LFN
	Finfo.lfname = Lfname;
	Finfo.lfsize = sizeof(Lfname);
#endif
	res = scan_files(ptr);
	if(res) {
		put_rc(res);
	} else {
		printf("%d files, %d bytes.\n%d folders.\n"
				"%d MB total disk space.\n%d MB available.\n",
				acc_files, acc_size, acc_dirs,
				(fs->n_fatent - 2) * (fs->csize / 2) / 1024, p * (fs->csize / 2) / 1024

		);
	}

	FIL *fp = malloc( sizeof(FIL) );

	// code for receiving a file from the serial port and writing to sd card
	printf("waiting for file info...\n");


	// file size of the receiving file
	printf("file size: ");
	file_size = USART_recv_dword(USART1);

	printf("%d\n", file_size);


	// file name of the receiving file
	printf("file name: ");
	i = 0;
	do
	{
		b = USART_getc(USART1);
		file_name[i] = b;

		i++;
	}
	while (b != '\0');
	printf("%s\n", file_name);

	printf("ready to receive! now send the file!\n");
	// create file on sd card
	res = f_open (fp, file_name, FA_CREATE_ALWAYS | FA_WRITE);

	// now receive the file!
	uint8_t bytes_written = 0;
	for(i = 0; i < file_size; i += 1024)
	{
		uint32_t chunk_size = i + 1024 < file_size ? 1024 : file_size - i;

		// write every 1024 byte chunk to sd card, not every byte
		for(j = 0; j < chunk_size; j++)
			buffer[j] = USART_getc(USART1);

		f_write(fp, buffer, chunk_size, &bytes_written); // TODO: Flowcontrol!!!
	}

	// close file
	f_close(fp);

	printf("file received successfully!!!\n");

	free(fp);


	/*
	// code for reading a file and sending it to the serial port
	printf("now sending file:\n");

	res = f_open (fp, "/jammin.mid", FA_READ);

	if (res == FR_OK)
	{
		static char buf[1024];
		UINT bytes_read = 0;

		for(i = 0; i < fp->fsize; i += bytes_read)
		{
			 res = f_read(fp, buf, 1024, &bytes_read);
			 if(bytes_read < 1024)
				 bytes_read--;

			 for(j = 0; j < bytes_read; j++)
			 {
				 while( !(USART1->SR & 0x00000040) ); // wait until data register is empty
				 USART_SendData(USART1, buf[j]);
			 }
		}

		f_close(fp);
	}
	else
		printf("error opening file.");


	free(fp);
*/

    // loop forever
    while(1);
}






















