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
//        V 0.15   Borderflasher added
//                 short pulse after longpulse added to longpulse
//        V 0.16   Choice of LPT port
//                 delay play key
//        V 0.17   according to Andreas Boose set 'ZERO' to 2500
//                 buffersize can be set by command line
//   
//        to do:
//                 alternative technique of converting (R. Storer)
//                 optional starting and stopping with 'esc' key
//                 tape adjustment tool


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <pc.h>
#include <crt0.h>


#define VERSION 0.17

#define LPT1 0x378 + 1
#define LPT2 0x278 + 1
// DAC Color Mask register, default: 0xff. palette_color = PELMASK & color_reg
#define PELMASK 0x03c6
#define read_pin  0x40
#define sense_pin 0x20
#define BUFFERSIZE 0x400000	// set buffer to 4 Megabyte
#define BASEFREQ 1193182
#define SCALE 1.0
#define ZERO 2500		// approx. 1/50 s

int _crt0_startup_flags = _CRT0_FLAG_LOCK_MEMORY;
unsigned long int buffersize = BUFFERSIZE;


void usage(void)
{
	fprintf(stderr, "usage: mtap [-lpt2] [-buffer size] [tap output file]\n");
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

// Init border colors for borderflasher
void init_border_colors()
{
	union REGS r;
	int color;

	// colour table according to PC64

	int red[]   = {0x00,0x3f,0x2e,0x00,0x25,0x09,0x01,0x3f,0x26,0x14,0x3f,0x12,0x1c,0x17,0x15,0x2b};
	int green[] = {0x00,0x3f,0x00,0x30,0x00,0x33,0x04,0x3f,0x15,0x08,0x14,0x12,0x1c,0x3f,0x1c,0x2b};
	int blue[]  = {0x00,0x3f,0x05,0x2b,0x26,0x00,0x2c,0x00,0x00,0x05,0x1e,0x12,0x1c,0x15,0x3a,0x2b};

	for (color = 1; color < 16; color++)
	{
		r.h.ah = 0x10;       // set palette registers function in Video BIOS
	 	r.h.al = 0x10;       // set individual DAC colour register
		r.w.bx = color * 16; // set border color entry
		r.h.dh = red[color];
		r.h.ch = green[color];
		r.h.cl = blue[color];
		int86(0x10, &r, &r);
	}

	r.h.ah = 0x10;     // set palette registers function in Video BIOS
	r.h.al = 0x01;     // set border (overscan) DAC colour register
	r.h.bh = 0xf0;     // set border color entry
	int86(0x10, &r, &r);
}

// Reset border color to black
void set_border_black()
{
	union REGS r;
	r.h.ah = 0x10;     // set palette registers function in Video BIOS
	r.h.al = 0x01;     // set border (overscan) DAC colour register
	r.h.bh = 0x00;     // set border color entry
	int86(0x10, &r, &r);
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
	int flash=0;  // border color
	int port;

	printf("\nmtap - Commodore TAP file Generator v%.2f\n\n", VERSION);

	port = LPT1;
	while (--argc && (*(++argv)[0] == '-'))
	{
		switch ((*argv)[1])
		{
			case 'b':
			case 'B':
				buffersize = atoi(*(++argv));
				argc--;
				if (buffersize < 1 || buffersize > 128)
				{
					fprintf(stderr, "Illegal Buffersize spezified!\n");
					exit(3);
				}
				buffersize *= 0x100000;
				break;
			case 'l':
			case 'L':
				if ((*argv)[4] == '1') port = LPT1;
				else if ((*argv)[4] == '2') port = LPT2;
				else usage();
				break;
			default:
				break;
		}
	}

	if (argc < 1) usage();

	strcpy(outname, argv[0]);
	SetFileExtension(outname, ".TAP");
	
	if ((fpout = fopen(outname,"wb")) == NULL)
	{
		fprintf(stderr, "Couldn't open output file %s!\n", outname);
		exit(2);
	}

	if ((buffer = calloc(buffersize, sizeof(char))) == NULL)
	{
		fprintf(stderr, "Couldn't allocate buffer memory!\n");
		exit(3);
	}


	init_border_colors();
	if (!(inp(port) & sense_pin))
		printf("Please <STOP> the tape.\n");
	while (!(inp(port) & sense_pin));

	printf("Press <PLAY> on tape!\n");
	while (inp(port) & sense_pin);

	printf("Tape now running, recording...\n");

	delay(500);	/* delay for a second, so signal settles */

	bufferp = buffer;

	outp(0x43,0x34);
	outp(0x40,0);	// rate = 65536
	outp(0x40,0);

	disable();
	overflow = 0;
	waitflag = 0;

	do
	{
		while (!(inp(port) & (read_pin | sense_pin)))
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
					outp(PELMASK, (flash++ << 4) | 0x0f);
				}
			}
			else waitflag = 1;
		}

		while ((inp(port) & (read_pin | sense_pin)) == read_pin)
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
					outp(PELMASK, (flash++ << 4) | 0x0f);
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
		outp(PELMASK, (flash++ << 4) | 0x0f);
	} while (!(inp(port) & sense_pin));
	
	set_border_black();
	outp(PELMASK, 0xff);
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
				// output long pulse and add last short pulse
				longpulse += deltat;
				for (zerot = longpulse; zerot > 0; zerot -= ZERO)
					fputc(0, fpout);
				longpulse = 0;
			}
			else
			{
				fputc((deltat & 0xff), fpout);
			}
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
