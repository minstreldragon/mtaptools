// mtap - Markus Brenner's tap converter
//
//        V 0.1  - still pretty experimental
//        V 0.11 - free memory
//        V 0.12 - Header corrected:
//                  'MTAP' removed
//                  filelength entry corrected
//                  Cassette stop correction
//        V 0.13 - correct '00' bytes
//        V 0.13a- various long pulses corrections
//                 force filename extension to '.tap'
//        V 0.14   Pauses should be mostly correct now
//                 now converts V and Boulder Dash CS correctly
//                 prints program name and version number
//                 always sets .TAP extension
//   
//        to do:
//                  choice of LPT port
//                  alternative technique of converting (R. Storer)


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <pc.h>
#include <crt0.h>


#define VERSION 0.14

#define LPT1 0x378 + 1
#define read_pin  0x40
#define sense_pin 0x20
#define BUFFERSIZE 0x400000	// set buffer to 4 Megabyte
#define BASEFREQ 1193182
#define SCALE 1.0
#define ZERO 2463		// 1/50 s

int _crt0_startup_flags = _CRT0_FLAG_LOCK_MEMORY;


void usage(void)
{
	fprintf(stderr, "usage: mtap [tap output file]\n");
	exit(1);
}


void SetFileExtension(char *str, char *ext)
{
	// sets the file extension of String *str to *ext
	int i, len;

	for (i=0, len = strlen(str); (i < len) && (str[i] != '.'); i++);
	str[i] = '\0';
	strcat(str, ext);
}


void main(int argc, char **argv)
{
	FILE *fpout;
	unsigned int status;
	long int pulsbytes, databytes;
	unsigned long int time, lasttime;
	unsigned long int deltat, longpulse;
	long int zerot;
	unsigned char *buffer, *bufferp;
	char outname[100];
	int i;
	int waitflag;
	int overflow;

	printf("\nmtap - Commodore TAP file Generator v%.2f\n\n", VERSION);

	if (argc < 2)
		usage();

	strcpy(outname, argv[1]);
	SetFileExtension(outname, ".TAP");
	
	if ((fpout = fopen(outname,"wb")) == NULL)
	{
		fprintf(stderr, "Couldn't open output file %s!\n", outname);
		exit(2);
	}

	if ((buffer = calloc(BUFFERSIZE, sizeof(char))) == NULL)
	{
		fprintf(stderr, "Couldn't allocate buffer memory!\n");
		exit(3);
	}


	if (!(inp(LPT1) & sense_pin))
		printf("Please <STOP> the tape.\n");
	while (!(inp(LPT1) & sense_pin));

	printf("Press <PLAY> on tape!\n");
	while (inp(LPT1) & sense_pin);

	printf("Tape now running, recording...\n");

	bufferp = buffer;

	outp(0x43,0x34);
	outp(0x40,0);	// rate = 65536
	outp(0x40,0);

	disable();
	overflow = 0;
	waitflag = 0;

	do
	{
		while (!(inp(LPT1) & (read_pin | sense_pin)))
		{
			outp(0x43,0x00);	// latch counter 0 output
			inp(0x40);		// read CTC channel 0
			inp(0x61); // dummy instruction
			if (inp(0x40) == overflow)	
			{
				if (waitflag)
				{
					*bufferp++ = 0xff;
					*bufferp++ = overflow;
					waitflag = 0;
				}
			}
			else waitflag = 1;
		}

		while ((inp(LPT1) & (read_pin | sense_pin)) == read_pin)
		{
			outp(0x43,0x00);	// latch counter 0 output
			inp(0x40);		// read CTC channel 0
			inp(0x61); // dummy instruction
			if (inp(0x40) == overflow)	
			{
				if (waitflag)
				{
					*bufferp++ = 0xff;
					*bufferp++ = overflow;
					waitflag = 0;
				}
			}
			else waitflag = 1;
		}

		outp(0x43,0x00);	// latch counter 0 output
		*bufferp++ = inp(0x40);		// read CTC channel 0
		inp(0x61); // dummy instruction
		*bufferp = inp(0x40);
		overflow = *bufferp++;
		waitflag = 0;
	} while (!(inp(LPT1) & sense_pin));

	enable();

	// first pulselen is second value, discard <STOP> pulse
	pulsbytes = (bufferp-buffer)/2 - 2;
	
	if (pulsbytes < 1)
	{
		fprintf(stderr, "No pulses recorded\n");
		exit(4);
	}

	printf("pulses recorded = %x\n", pulsbytes);
	fprintf(fpout,"C64-TAPE-RAW");
	fprintf(fpout,"%c%c%c%c",0,0,0,0);	// for future use
	fprintf(fpout,"%c%c%c%c",0,0,0,0);	// filelength


	bufferp = buffer;
	longpulse = 0;
	lasttime = *bufferp++;
	lasttime += (*bufferp++)*256;

	for (i = 1; i <= pulsbytes; i++)
	{
		time = *bufferp++;
		time += (*bufferp++)*256;

		if (lasttime <= time)
			lasttime += 0x10000;
		deltat = (lasttime-time) * SCALE * 123156 / BASEFREQ;

		if (deltat < 0x100)
		{
			if (longpulse) 
			{
				for (zerot = longpulse; zerot > 0; zerot -= ZERO)
					fputc(0, fpout);
				longpulse = 0;
			}
			fputc((deltat & 0xff), fpout);
		}
		else
			longpulse += deltat;
		lasttime = time;
	}


	// write trailing Zeroes, if any
	for (zerot = longpulse; zerot > 0; zerot -= ZERO) fputc(0, fpout);

	// determine data length and set it in header
	databytes = ftell(fpout) - 20;
	fseek(fpout, 16, SEEK_SET);

	for (i=0; i < 4; i++)
	{
		fputc(databytes % 256, fpout);
		databytes /= 256;
	}

	fclose(fpout);
	free(buffer);
}
