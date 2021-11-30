
Debian
====================
This directory contains files used to package trumpcoind/trumpcoin-qt
for Debian-based Linux systems. If you compile trumpcoind/trumpcoin-qt yourself, there are some useful files here.

## trumpcoin: URI support ##


trumpcoin-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install trumpcoin-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your trumpcoin-qt binary to `/usr/bin`
and the `../../share/pixmaps/trumpcoin128.png` to `/usr/share/pixmaps`

trumpcoin-qt.protocol (KDE)

