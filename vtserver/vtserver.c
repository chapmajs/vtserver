/*
 * Virtual tape driver - copyright 1998 Warren Toomey	wkt@cs.adfa.edu.au
 *
 * $Revision: 2.3.1.5 $
 * $Date: 2001/04/04 02:57:28 $
 *
 *
 * This program sits on a serial line, receives `tape' commands from a
 * PDP-11, and returns the results of those command to the other machine.
 * It was designed to allow 7th Edition UNIX to be installed on to a
 * machine without a tape drive.
 *
 * Much of the functionality of the tape protocol has been designed but
 * not yet implemented.
 *
 * Commands look like the following:
 *
 *	  +----+----+-----+------+----+----+---+----+
 *	  | 31 | 42 | cmd | rec# | blk|num | ck|sum |
 *	  +----+----+-----+------+----+----+---+----+
 *
 * Each box is an octet. The block number is a 16-bit value, with the
 * low-order octet first. Any data that is transmitted (in either direction)
 * comes as 512 octets between the block number and the checksum. The
 * checksum is a bitwise-XOR of octet pairs, excluding the checksum itself.
 * I.e checksum octet 1 holds 31 XOR cmd XOR blklo [ XOR odd data octets ], and
 * checksum octet 2 holds 42 XOR rec# XOR blkhi [ XOR even data octets ].
 *
 * A write command from the client has 512 octets of data. Similarly, a read
 * command from the server to the client has 512 octets of data. 
 *
 *
 * The Protocol
 * ------------
 *
 * The protocol is stateless. Commands are read, zeroread, quickread, write,
 * open and close.
 *
 * The record number holds the fictitious tape record which is requested.
 * The server keeps one disk file per record. The block number holds the
 * 512-octet block number within the record.
 *
 * Assumptions: a read on a record without a previous open implies the open.
 * A command on a new record will close any previously opened record.
 * There is only one outstanding client request at any time.
 *
 * The client sends a command to the server. The server returns the command,
 * possibly with 512 octets of data, to the client. The top four bits of the
 * command octet hold the return error value. All bits off indicates no error.
 * If an error occurred, including EOF, no data octets are returned on a read
 * command.
 *
 * If the client receives a garbled return command, it will resend the command.
 * Therefore, the server must cope with this.
 *
 * The exception command to all of this is the QUICK read command. Here,
 * the server responds with one octet followed by 512 data octets. If the
 * octet is zero, data will be sent. Otherwise, the octet contains an
 * error value in the top four bits (including EOF), and no data is sent.
 * There are no command replies or checksums. This makes it useful only
 * to load one record by hand at bootstrap.

 * If the client requests a ZEROREAD, and if the server detects that the
 * block requested is all zeroes, then the returned message has cmd=ZEROREAD
 * and _no_ data octets. However, if any octet in the block non-zero, then
 * the server sends back a cmd=READ message with the 512-octets of data.
 *
 */

#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
char *strerror(int errno);

/* Commands sent in both directions */
struct vtcmd {
  unsigned char hdr1;		/* Header, 31 followed by 42 (decimal) */
  unsigned char hdr2;
  unsigned char cmd;		/* Command, one of VTC_XXX below */
  				/* Error comes back in top 4 bits */
  unsigned char record;		/* Record we're accessing */
  unsigned char blklo;		/* Block number, in lo/hi format */
  unsigned char blkhi;
  unsigned char sum0;		/* 16-bit checksum */
  unsigned char sum1;		/* 16-bit checksum */
};

/* Header octets */
#define VT_HDR1		31
#define VT_HDR2		42

/* Commands available */
#define VTC_QUICK	0	/* Quick read, no cksum sent */
#define VTC_OPEN	1	/* Open the requested record */
#define VTC_CLOSE	2	/* Close the requested record */
#define VTC_READ	3	/* Read requested block from record */
#define VTC_WRITE	4	/* Write requested block from record */
#define VTC_ZEROREAD	6	/* Zero read, return no data if all zeroes */

/* Errors returned */
#define VTE_NOREC	1	/* No such record available */
#define VTE_OPEN	2	/* Can't open requested block */
#define VTE_CLOSE	3	/* Can't close requested block */
#define VTE_READ	4	/* Can't read requested block */
#define VTE_WRITE	5	/* Can't write requested block */
#define VTE_NOCMD	6	/* No such command */
#define VTE_EOF		7	/* End of file: no blocks left to read */

#define BLKSIZE		512

/* Static things */
extern int errno;		/* Error from system calls etc. */
struct vtcmd vtcmd;		/* Command from client */
struct vtcmd vtreply;		/* Reply to client */
char inbuf[BLKSIZE];		/* Input buffer */
char *port = NULL;		/* Device for serial port */
int portfd;			/* File descriptor for the port */
int ttyfd=0;			/* File descriptor for the console */
int recfd = -1;			/* File descriptor for the in-use record */
int lastrec = -2;		/* Last record used */
char *recname[256];		/* Up to 256 records on the tape */
struct termios oldterm;		/* Original terminal settings */

/* This array holds the bootstrap code, which we can enter via ODT
 * at the BOOTSTART address.
 */
#define BOOTSTART 0140000
int bootcode[]= {
	0010706, 005003, 0012701, 0177560, 012704, 0140106, 0112400, 0100406,
	0105761, 000004, 0100375, 0110061, 000006, 0000770, 0005267, 0000052,
	0004767, 000030, 0001403, 0012703, 006400, 0005007, 0012702, 0001000,
	0004767, 000010, 0110023, 0005302, 001373, 0000746, 0105711, 0100376,
	0116100, 000002, 0000207, 0025037, 000000, 0000000, 0177777
};
int havesentbootcode=1;		/* Don't send it unless user asks on cmd line */


/* Get a command from the client.
 * If a command is received, returns 1,
 * otherwise return 0.
 */
int get_command(struct vtcmd *v)
{
  int i,loc;
  unsigned char sum0, sum1;
  char ch,bootbuf[40];

  /* Get a valid command from the client */
  read(portfd, &v->hdr1, 1);

  /* Send down the bootstrap code to ODT if we see an @ sign */
  if ((havesentbootcode==0) && (v->hdr1 == '@')) {
    for (i=0,loc=BOOTSTART;i<(sizeof(bootcode)/sizeof(int));i++,loc+=2) {
	sprintf(bootbuf, "%06o/", loc);
	write(portfd, bootbuf, strlen(bootbuf));

	/* wait for current value to print */
	while (1) { read(portfd, &ch, 1); if (ch==' ') break; }
	sprintf(bootbuf, "%06o\r", bootcode[i]);
	write(portfd, bootbuf, strlen(bootbuf));

	/* and suck up any characters sent from ODT */
	while (1) { read(portfd, &ch, 1); if (ch=='@') break; }
    }
    sprintf(bootbuf, "%06oG", BOOTSTART);
    write(portfd, bootbuf, strlen(bootbuf));
    while (1) { read(portfd, &ch, 1); if (ch=='G') break; }
    havesentbootcode=1; return(0);
  }

  if (v->hdr1 != VT_HDR1) { write(1,&v->hdr1, 1); return(0); }
  read(portfd, &v->hdr2, 1);
  if (v->hdr2 != VT_HDR2) { write(1,&v->hdr2, 1); return(0); }

  read(portfd, &v->cmd, 1); read(portfd, &v->record, 1);
  read(portfd, &v->blklo, 1); read(portfd, &v->blkhi, 1);

  /* Calc. the cksum to date */
  sum0 = VT_HDR1 ^ v->cmd ^ v->blklo;
  sum1 = VT_HDR2 ^ v->record ^ v->blkhi;

  /* All done if a quick read */
  if (v->cmd == VTC_QUICK) return(1);

  /* Retrieve the block if a WRITE cmd */
  if (v->cmd == VTC_WRITE) {
    for (i = 0; i < BLKSIZE; i++) {
      read(portfd, &inbuf[i], 1); sum0 ^= inbuf[i]; i++;
      read(portfd, &inbuf[i], 1); sum1 ^= inbuf[i];
    }
  }

  /* Get the checksum */
  read(portfd, &(v->sum0), 1);
  read(portfd, &(v->sum1), 1);

  /* Try again on a bad checksum */
  if ((sum0 != v->sum0) || (sum1 != v->sum1))
    { fputc('e',stderr); return(0); }

  return(1);
}


/* Reply has been mostly initialised by do_command */
void send_reply()
{
  int i;

  if ((vtcmd.cmd)==VTC_QUICK) {     /* Only send buffer on a quick read */
    write(portfd, &vtreply.cmd, 1);
    if (vtreply.cmd!=VTC_QUICK) return;	/* Must have been an error */
    for (i=0; i< BLKSIZE; i++) write(portfd, &inbuf[i], 1);
    return;
  }

  /* Calculate the checksum */
  vtreply.sum0 = VT_HDR1 ^ vtreply.cmd ^ vtreply.blklo;
  vtreply.sum1 = VT_HDR2 ^ vtreply.record ^ vtreply.blkhi;

			/* Data only on a read with no errors */
  if (vtreply.cmd == VTC_READ) {
    for (i = 0; i < BLKSIZE; i++) {
      vtreply.sum0 ^= inbuf[i]; i++;
      vtreply.sum1 ^= inbuf[i];
    }
  }
  /* Transmit the reply */
  write(portfd, &vtreply.hdr1, 1);
  write(portfd, &vtreply.hdr2, 1);
  write(portfd, &vtreply.cmd, 1);
  write(portfd, &vtreply.record, 1);
  write(portfd, &vtreply.blklo, 1);
  write(portfd, &vtreply.blkhi, 1);

  if (vtreply.cmd == VTC_READ) {
    for (i = 0; i < BLKSIZE; i++) write(portfd, &inbuf[i], 1);
  }
  write(portfd, &vtreply.sum0, 1);
  write(portfd, &vtreply.sum1, 1);
}


#define seterror(x)	vtreply.cmd |= (x<<4);

/* Actually do the command sent to us */
void do_command()
{
  int record, block, i, offset;

  /* First, copy the command to the reply */
  memcpy(&vtreply, &vtcmd, sizeof(vtcmd));

  record = vtcmd.record; block = (vtcmd.blkhi << 8) + vtcmd.blklo;
  offset = block * BLKSIZE;

  /* Open the record if not already open */
  if (record != lastrec) {
    if (recname[record] == NULL) {
	fprintf(stderr,"No such tape record %d\r\n",record);
	seterror(VTE_NOREC); return;
    }

    i = open(recname[record], O_RDWR);
    if (i>=0) {
       fprintf(stderr,"\nOpened %s read-write\r\n ", recname[record]);
       goto afteropen;			/* yuk, a goto! */
    }
    i = open(recname[record], O_RDONLY);
    if (i>=0) {
       fprintf(stderr,"\nOpened %s read-only\r\n ", recname[record]);
       goto afteropen;			/* yuk, a goto! */
    }
    i = open(recname[record], O_RDWR|O_CREAT|O_TRUNC, 0600);
    if (i>=0) {
       fprintf(stderr,"\nOpened %s as a new file\r\n ", recname[record]);
       goto afteropen;			/* yuk, a goto! */
    }
    fprintf(stderr,"Cannot open %s: %s\r\n",recname[record], strerror(errno));
    seterror(VTE_NOREC); return;
 
afteropen:
    if (record != lastrec) close(recfd);
    recfd = i; lastrec = record;
  }

  switch (vtcmd.cmd) {
    case VTC_OPEN:  break;
    case VTC_CLOSE: close(recfd); lastrec = -1; break;

    case VTC_QUICK: vtreply.cmd=0;	/* No errors yet */
    case VTC_ZEROREAD: 
    case VTC_READ:  
		    i= lseek(recfd, offset, SEEK_SET);
      		    if (i==-1)
      		      { fprintf(stderr," EOF\r\n"); seterror(VTE_EOF); return; }
   		    i = read(recfd, &inbuf, BLKSIZE);
      		    if (i == 0)
      		      { fprintf(stderr," EOF\r\n"); seterror(VTE_EOF); return; }
      		    if (i == -1) { seterror(VTE_READ); return; }

				/* Determine if the entire block is zero */
		    if (vtcmd.cmd==VTC_ZEROREAD) {
		      for (i=0;i<BLKSIZE;i++) if (inbuf[i]!=0) break;
		      if (i==BLKSIZE) vtreply.cmd=VTC_ZEROREAD;
		      else vtreply.cmd=VTC_READ;
		    }

		    if (offset && (offset % 102400) == 0)
			fprintf(stderr,"\r\n%dK sent\r\n", offset/1024);
		    fputc('r',stderr);
      		    break;

    case VTC_WRITE: i= lseek(recfd, offset, SEEK_SET);
      		    if (i==-1)
      		      { fprintf(stderr," seek error\r\n");
			seterror(VTE_WRITE); return;
		      }
		    i = write(recfd, &inbuf, BLKSIZE);
      		    if (i < 1) { seterror(VTE_WRITE); return; }
		    if (offset && (offset % 102400) == 0)
			fprintf(stderr,"\r\n%dK received\r\n", offset/1024);
		    fputc('w',stderr);
      	 	    break;

    default:	    fputc('?',stderr);
   		    seterror(VTE_NOCMD);
  }
  fflush(stderr);
}

/* The configuration file is .vtrc. The first line holds the name
 * of the serial device. The following lines hold the filenames which
 * are the successive tape records. Lines starting with a hash are ignored.
 * Files are not opened unless they are referenced by a client's command.
 */
void read_config()
{
  FILE *in;
  char *c;
  int i, cnt = 0, donesystem=0;

  in = fopen(".vtrc", "r");
  if (in == NULL) {
    fprintf(stderr, "Error opening .vtrc config file: %s\n", strerror(errno));
    exit(1);
  }
  while (cnt != 256) {
    if (feof(in)) break;
    c = fgets(inbuf, BLKSIZE - 2, in);
    if (feof(in)) break;

    if (c == NULL) {
      fprintf(stderr, "Error reading .vtrc config file: %s\n", strerror(errno));
      exit(1);
    }
    if (inbuf[0] == '#') continue;

    inbuf[strlen(inbuf) - 1] = '\0';	/* Remove training newline */

    if (donesystem == 0) {
	fprintf(stderr,"Running command %s\n\n",inbuf);
	system(inbuf); donesystem=1; continue;
    }

    if (port == NULL) {
      port = (char *) malloc(strlen(inbuf) + 2);
      strcpy(port, inbuf); continue;
    }

    recname[cnt] = (char *) malloc(strlen(inbuf) + 2);
    strcpy(recname[cnt], inbuf); cnt++;
  }
  fprintf(stderr,"Tape records are:\n");
  for (i=0; i<cnt; i++) fprintf(stderr,"  %2d %s\n", i, recname[i]);
  fprintf(stderr,"\n");

  fclose(in);
}

/* Use POSIX terminal commands to
 * set the serial line to raw mode.
 */
void setraw(int fd, char *portname, int dosave)
{
  struct termios t;

  /* Get the device's terminal attributes */
  if (tcgetattr(fd, &t) == -1) {
    fprintf(stderr, "Error getting %s attributes: %s\n",
						 portname, strerror(errno));
    exit(1);
  }
  if (dosave) memcpy(&oldterm,&t,sizeof(t));	/* Save the old settings */

  /* Set raw - code stolen from 4.4BSD libc/termios.c */
  t.c_iflag &= ~(IMAXBEL | IXOFF | INPCK | BRKINT | PARMRK | ISTRIP |
		 INLCR | IGNCR | ICRNL | IXON | IGNPAR);
  t.c_iflag |= IGNBRK;
  t.c_oflag &= ~OPOST;
  t.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL | ICANON | ISIG | IEXTEN |
		 NOFLSH | TOSTOP | PENDIN);
  t.c_cflag &= ~(CSIZE | PARENB);
  t.c_cflag |= CS8 | CREAD;
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;

  /* Set the device's terminal attributes */
  if (tcsetattr(fd, TCSANOW, &t) == -1) {
    fprintf(stderr, "Error setting %s attributes: %s\n",
						portname, strerror(errno));
    exit(1);
  }
}

/* Reset the terminal settings and
 * exit the process
 */
void termexit(int how)
{
  tcsetattr(ttyfd, TCSANOW, &oldterm);
  exit(how);
}


/* Open the named port and set it to raw mode.
 * Someone else deals with such things as
 * baud rate, clocal and crtscts.
 */
void open_port()
{
  fprintf(stderr,"Opening port %s .... ", port); fflush(stderr);
  portfd = open(port, O_RDWR);
  if (portfd == -1) {
    fprintf(stderr, "Error opening device %s: %s\n", port, strerror(errno));
    exit(1);
  }
  fprintf(stderr,"Port open\n");
  setraw(portfd,port,0);
}

void server_loop()
{
  char ch;
  int i;
  int esc_num=0;
  int in_tape_mode=0;		/* Are we in tape mode or console mode */
  fd_set fdset;

  ch='\r'; write(portfd,&ch,1);	/* Send a \r to wake ODT up if it is there */
  FD_ZERO(&fdset);
  while (1) {
    FD_SET(ttyfd, &fdset);
    FD_SET(portfd, &fdset);	/* Wait for chars in stdin or serial line */

    i=select(portfd+1, &fdset, NULL, NULL, NULL);
    if (i<1) continue;

				/* Console input */
    if (FD_ISSET(ttyfd, &fdset)) {
	read(ttyfd,&ch,1);
	if (!in_tape_mode) {
	  if (ch=='') esc_num++;	/* Exit when two ESCs consecutively */
	  else esc_num=0;
	  if (esc_num==2) termexit(0);
	  write(portfd,&ch,1);
	}
    }
				/* Get a command from the client */
    if (FD_ISSET(portfd, &fdset)) {
      if (get_command(&vtcmd)==0) { in_tape_mode=0; continue; }

      in_tape_mode=1;
      do_command();		/* Do the command */
      send_reply();		/* Send the reply */
    }
  }
}



int main(int argc, char *argv[])
{
  fprintf(stderr,"Virtual tape server, $Revision: 2.3.1.5 $ \n");

  if ((argc==2) && (!strcmp(argv[1], "-odt"))) havesentbootcode=0;
  read_config(); open_port();
  setraw(ttyfd,"standard input",1); 
  server_loop();
  exit(0);
}
