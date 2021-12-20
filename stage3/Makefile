# MIT License
# 
# Copyright (c) 2021 vzvca
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

HPARSERDIR=../http-parser-2.9.4

# -- debug on
CFLAGS+=-g3

# -- lib json-c
CFLAGS += $(shell pkg-config --cflags json-c)
LDFLAGS += $(shell pkg-config --libs json-c)

# -- lib sqlite3
LDFLAGS += -lsqlite3

# -- lib website arch
LDFLAGS += -Larch -larch 

OBJS=mbv.o mbtiles.o archrt.o

vpath http_% $(HPARSERDIR)

mbv: $(OBJS) http_parser.o arch/libarch.a
	$(CC) -o $@ $(OBJS) $(HPARSERDIR)/http_parser.o $(LDFLAGS)

http_parser.o: http_parser.c http_parser.h
	$(MAKE) -C ../http-parser-2.9.4 $@
	cp $(HPARSERDIR)/$@ .

arch/libarch.a: archsrc
	$(MAKE) -C arch -f ../Makefile.arch

mkarch.o: strhash.c mkarch.c 
mbv.o: strhash.c mbv.c
mbtiles.o: mbtiles.c

mkarch: mkarch.o
	$(CC) -o $@ $<

archsrc: mkarch
	./mkarchsrc.sh

clean:
	-@rm mbv
	-@rm *.o
	-@rm arch/*

.PHONY: archsrc clean
