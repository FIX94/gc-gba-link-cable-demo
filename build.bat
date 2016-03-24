cd gba
make clean
make
cd ..
make clean
mv -f gba/gba_mb.gba data/gba_mb.gba
make
pause