#! /bin/bash

PWD=$(pwd)

copy ()
{
    f=$1
    echo "Copy $f to arch"
    rm -f arch/`basename $f .c`.o
    cp -f $f arch
}

mkdir -p /tmp/arch
find  /tmp/arch -name '*.c' -delete
./mkarch -i site -o /tmp/arch -p site/

for f in /tmp/arch/__*.c
do
    b=`basename $f`
    if [ ! -f arch/$b ]
    then
	copy $f
    fi
    cmp -s $f arch/$b || copy $f
done
