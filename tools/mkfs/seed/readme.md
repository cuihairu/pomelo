# pomelo disk

This tiny image is laid out by tools/mkfs using the format from
servers/fs/format.h: one superblock sector, four inode-table sectors,
then data blocks. Try `ls`, `cat readme.md`, or make your own file
with `write note.txt pomelo is a fruit`.
