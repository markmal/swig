#!/bin/sh
DoExitAsm ()
{ echo "An error occurred while assembling $1"; exit 1; }
DoExitLink ()
{ echo "An error occurred while linking $1"; exit 1; }
echo Linking runme
OFS=$IFS
IFS="
"
ld -b elf64-x86-64 -m elf_x86_64  --dynamic-linker=/lib64/ld-linux-x86-64.so.2   -s -L. -o runme runme_link.res
if [ $? != 0 ]; then DoExitLink runme; fi
IFS=$OFS
