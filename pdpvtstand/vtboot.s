/ Hand-entered code to load the first record off the virtual tape.
/ Copyright 1998/2000 Warren Toomey	wkt@cs.adfa.edu.au
/ Space optimised by John Holden at Sydney Uni

/ The following registers are used:
/	r0 holds the char to be sent or received
/	r1 holds the address of KL11 unit 1
/	r2 holds the count, from 0 to 512
/	r3 holds the base (where the next data char is to go)
/	r5 is used data pointer for outputing words

.. = 140000

.text

start:
	mov	pc,sp		/ Initialise the stack pointer
	clr	r3		/ Set the base to zero
	mov	$177560, r1	/ Set the base address of KL11 unit 1

outblk:
	mov	$datalst,r4	/ get data list
1:
	movb	(r4)+,r0	/ get item
	bmi	3f		/ list done
2:
	tstb	4(r1)		/ test ready bit
	bpl     2b		/ do nothing
	movb    r0,6(r1)	/ output character
	br	1b		/ and loop for next byte
3:
	inc	block		/ bump clock counter

	jsr	pc, getc	/ get the reply
	beq	4f		/ If not zero, jump to address 0, i.e exit
	mov	$6400, r3	/ All done, set boot device to 13,0
	clr	pc		/ and jump to location 0

4:
	mov 	$1000, r2	/ Set count to 512
5:
	jsr	pc, getc	/ Get a data character
	movb	r0, (r3)+	/ Save at base and increment it
	dec	r2		/ Decrement data count
	bne	5b		/ and loop if any left to get
	br	outblk		/ End of data, go back and get another block

getc:
	tstb	(r1)		/ test for ready bit
	bpl     getc		/ do nothing
	movb    2(r1),r0	/ get character
	rts	pc

.data
datalst:
	.byte	31.		/ command bytes
	.byte	42.		/ command bytes
	.byte	0		/ boot command
	.byte	0		/ record number
block:	0			/ block number
	-1			/ end of data marker. Assumes that there are
				/ never more than 127 boot blocks
