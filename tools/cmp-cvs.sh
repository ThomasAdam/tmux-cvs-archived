# $Id$

for i in *.[ch]; do
    diff -u $i /usr/src/usr.bin/tmux/$i
done
