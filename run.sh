#!/bin/bash

./configure --prefix=/usr/local/python25 --enable-shared
sed -i "38s/svnversion\ \$(srcdir)/\"\"/g" Makefile
make
cp ./libpython2.5.so.1.0 /usr/local/lib/
