cd gba
make clean
make
cd ..
make clean
dd if=/dev/zero of=data/gba_mb.gba count=120448 bs=1
dd if=gba/gba_mb.gba of=data/gba_mb.gba count=120448 bs=1 conv=notrunc
make
pause