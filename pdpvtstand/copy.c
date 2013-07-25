/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)bcopy.c	1.1 (2.10BSD Berkeley) 7/25/87
 */

#include "../h/param.h"
#include "saio.h"

char module[] = "copy";			/* module name -- used by trap */
extern struct devsw devsw[];
extern  struct  iob     iob[];

int     Nolabelerr = 1;         /* Inhibit spurious label error msgs */

char buffer[DEV_BSIZE];
main()
{
	int c, o, i;
	char buf[50];
        struct  iob     *io;

	printf("Standalone copy program - known devices are \n");
        for(i=0;devsw[i].dv_name;i++) printf("%s ",devsw[i].dv_name);
        putchar('\n'); putchar('\n');

	printf("Tape record n from device xx is written as xx(0,0,n)\n");
	printf("Disk device xx is written as xx(0,0,0)\n\n");
	do {
	    	printf("Enter name of input record/device: ");
		gets(buf);
		i = open(buf, 0);
	} while (i <= 0);
        io = &iob[i - 3];
        devlabel(io, DEFAULTLABEL);	/* Stop spurious label warnings */
	do {
	    	printf("Enter name of output record/device: ");
		gets(buf);
		o = open(buf, 1);
	} while (o <= 0);
        io = &iob[o - 3];
        devlabel(io, DEFAULTLABEL);	/* Stop spurious label warnings */

	while ((c = read(i, buffer, DEV_BSIZE)) > 0)
		write(o, buffer, c);

	printf("Copying done. Either reset the system, or hit\n");
	printf("<return> to exit the standalone program.\n");
	gets(buf);
	exit(0);
}
