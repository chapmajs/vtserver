/*
 * Virtual tape driver - copyright 1998/2000 Warren Toomey wkt@cs.adfa.edu.au
 *
 * $Revision: 1.13 $
 * $Date: 2001/03/25 00:18:10 $
 */

#include <sys/param.h>
#include "saio.h"

/* Command sent to tape server */
struct vtcmd {
    char hdr1;			/* Header, 31 followed by 42 (decimal) */
    char hdr2;
    char cmd;			/* Command, one of VTC_XXX below */
				/* Error comes back in top 4 bits */
    char record;		/* Record we're accessing */
    char blklo;			/* Block number, in lo/hi format */
    char blkhi;
    char sum0;			/* 16-bit checksum */
    char sum1;
};

/* Header bytes */
#define VT_HDR1		31
#define VT_HDR2		42

/* Client commands available */
#define VTC_QUICK	0	/* Quick read, no cksum sent */
#define VTC_OPEN	1
#define VTC_CLOSE	2
#define VTC_READ	3
#define VTC_WRITE	4
#define VTC_ZEROREAD    6

/* Errors returned */
#define VTE_NOREC	1	/* No such record available */
#define VTE_OPEN	2	/* Can't open requested block */
#define VTE_CLOSE	3	/* Can't close requested block */
#define VTE_READ	4	/* Can't read requested block */
#define VTE_WRITE	5	/* Can't write requested block */
#define VTE_NOCMD       6       /* No such command */
#define VTE_EOF         7       /* End of file: no blocks left to read */

#define BLKSIZE         512

/* Static things */
struct vtcmd vtcmd;		/* Command buffer */
struct vtcmd vtreply;		/* Reply from server */
char *vtbuf;			/* Pointer to input buffer */
unsigned int hitim, lotim;	/* Variables for delay loop */
unsigned int curblk;		/* Current block */

char vtsendcmd();		/* Forward references */
char vtgetc();

struct  vtdevice  {
        int     rcsr,rbuf;
        int     tcsr,tbuf;
};

#define NVT	2
struct  vtdevice *VTcsr[NVT + 1] = {
                (struct vtdevice *)0177560,	/* We use VTcsr[0]: KL unit 0 */
                (struct vtdevice *)0,
                (struct vtdevice *)-1
};

extern int tapemark;    /* flag to indicate tapemark encountered
                           (see sys.c as to how it's used) */


/*** Standalone-dependent section */

/* vtopen() is used to inform the server which record we'll be using */
int vtopen(io)
register struct iob *io;
{
    register i;

    vtcmd.record= io->i_part; vtcmd.cmd= VTC_OPEN;
    curblk=0; vtcmd.blklo=0; vtcmd.blkhi=0;
    io->i_flgs |= F_TAPE;

    vtsendcmd();		/* Send the command to the server */
}

int vtclose(io)
struct  iob     *io;
{
	return(-1);		/* Same as nullsys */
}

int vtseek(io, space)
struct  iob     *io;
register int    space;
{
    curblk+= space+space;	/* Double to convert 1024K blk# to 512 blk# */
}


/* We send a command to the server, get a reply, and return the
 * amount read (even on EOF, which may be zero) or -1 on error.
 */
int vtstrategy(io, func)
register struct iob *io;
{
    register error, i;

    vtbuf= io->i_ma;

    /* Assume record, blklo and blkhi are ok */
    /* Assume i->i_cc is in multiples of BLKSIZE */
    for (i=0; i<io->i_cc; i+=BLKSIZE, vtbuf+=BLKSIZE, curblk++) {
        if (func==WRITE) vtcmd.cmd= VTC_WRITE;
        else vtcmd.cmd= VTC_ZEROREAD;

        error= vtsendcmd();		/* Send the command to the server */

	/* Some programs rely on the buffer being
 	 * cleared to indicate EOF, e.g cat.
	 */
	if (error == VTE_EOF) { tapemark=1; vtbuf[0]=0; return(i); }

	if (error != 0) { printf("tape error %d", error); return(-1); }
    }
    return(io->i_cc);
}

/*** Protocol-specific stuff ***/

char vtsendcmd()
{
    register i;
    char error, cmd, sum0, sum1;

			/* Determine block bytes */
    vtcmd.blklo= curblk & 0377;
    vtcmd.blkhi= curblk >>8;

			/* Build the checksum */
    vtcmd.hdr1= VT_HDR1; vtcmd.hdr2= VT_HDR2;
    vtcmd.sum0= VT_HDR1 ^ vtcmd.cmd    ^ vtcmd.blklo;
    vtcmd.sum1= VT_HDR2 ^ vtcmd.record ^ vtcmd.blkhi;

    /* Calculate the checksum over the data if a write */
    if (vtcmd.cmd==VTC_WRITE)
      for (i=0; i<BLKSIZE; i++) {
	vtcmd.sum0 ^= vtbuf[i]; i++;
	vtcmd.sum1 ^= vtbuf[i];
      }

			/* Send the command to the server */
  sendcmd:
    vtputc(vtcmd.hdr1);  vtputc(vtcmd.hdr2);
    vtputc(vtcmd.cmd);   vtputc(vtcmd.record);
    vtputc(vtcmd.blklo); vtputc(vtcmd.blkhi);
    if (vtcmd.cmd==VTC_WRITE) for (i=0; i<BLKSIZE; i++) vtputc(vtbuf[i]);
    vtputc(vtcmd.sum0);  vtputc(vtcmd.sum1);

  			/* Now get a valid reply from the server */
  getreply:
    vtreply.hdr1= vtgetc(); if (hitim==0) goto sendcmd;
    if (vtreply.hdr1!=VT_HDR1) goto getreply;
    vtreply.hdr2= vtgetc(); if (hitim==0) goto sendcmd;
    if (vtreply.hdr2!=VT_HDR2) goto getreply;
    vtreply.cmd= vtgetc(); if (hitim==0) goto sendcmd;
    vtreply.record= vtgetc(); if (hitim==0) goto sendcmd;
    vtreply.blklo= vtgetc(); if (hitim==0) goto sendcmd;
    vtreply.blkhi= vtgetc(); if (hitim==0) goto sendcmd;


			/* Calc. the cksum to date */
    sum0= VT_HDR1 ^ vtreply.cmd ^ vtreply.blklo;
    sum1= VT_HDR2 ^ vtreply.record ^ vtreply.blkhi;

			/* Retrieve the block if no errs and a READ reply */
    if (vtreply.cmd==VTC_READ) {
      for (i=0; i<BLKSIZE; i++) {
	vtbuf[i]= vtgetc(); if (hitim==0) goto sendcmd;
	sum0 ^= vtbuf[i]; i++;
	vtbuf[i]= vtgetc(); if (hitim==0) goto sendcmd;
	sum1 ^= vtbuf[i];
      }
    }
			/* Get the checksum */
    vtreply.sum0= vtgetc(); if (hitim==0) goto sendcmd;
    vtreply.sum1= vtgetc(); if (hitim==0) goto sendcmd;

			/* Try again on a bad checksum */
    if ((sum0!=vtreply.sum0) || (sum1!=vtreply.sum1)) {
	putchar('e'); goto sendcmd;
    }
			/* Zero the buffer if a successful zero read */
    if (vtreply.cmd==VTC_ZEROREAD) for (i=0; i<BLKSIZE; i++) vtbuf[i]=0;

			/* Extract any error */
    error= vtreply.cmd >> 4; return(error);
}

/*** Harware-specific stuff ***/

/* vtgetc() and vtputc(): A sort-of repeat of the getchar/putchar
 * code in prf.c, but without any console stuff
 */

/* Get a character, or timeout and return with hitim zero */
char vtgetc()
{   
        register c;
    
        VTcsr[0]->rcsr = 1; hitim=3; lotim=65535;
  
        while ((VTcsr[0]->rcsr&0200)==0) {
	   lotim--;
	   if (lotim==0) hitim--;
	   if (hitim==0) { putchar('t'); return(0); }
	}
        c = VTcsr[0]->rbuf; return(c);
}

vtputc(c)
register c;
{
        register s;

        while((VTcsr[0]->tcsr&0200) == 0) ;
        s = VTcsr[0]->tcsr;
        VTcsr[0]->tcsr = 0; VTcsr[0]->tbuf = c; VTcsr[0]->tcsr = s;
}
