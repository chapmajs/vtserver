CFLAGS=-g -Wall
#LDFLAGS=-static

L2H= latex2html
L2HOPTS=  -long_titles 6 -no_subdir -split 0 -link 0 -toc_depth 0
L2HOPTS2= -html_version 3.2,math -info 0 -no_auto_link

vtserver: vtserver.o
	cc -o vtserver $(LDFLAGS) vtserver.o

clean:
	rm vtserver *.o

vtreadme.dvi:	vtreadme.tex
	latex vtreadme.tex

vtreadme.ps:	vtreadme.dvi
	dvips -o vtreadme.ps vtreadme.dvi

vtreadme.html:	vtreadme.tex vtreadme.dvi
	$(L2H) $(L2HOPTS) $(L2HOPTS2) vtreadme.tex

binaries:
	cp ../pdpvtstand/vtboot vtboot.pdp
	cp ../pdpvtstand/boot.dd .
	cp ../pdpvtstand/copy .
	cp ../pdpvtstand/disklabel .
	cp ../pdpvtstand/mkfs .
	cp ../pdpvtstand/restor .
	cp ../pdpvtstand/icheck .
