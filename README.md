# Color-Gamut-Plotter Qt Version
Another take in color gamut plotting but with Qt frameworks instead.

![Screnshot](qtgamutplot.PNG)

Written in C++ with Qt 5.15.2 and build with MingW 64-bit kit.

Image loading is currently handled with QImage.<br>
Color transformation handled with lcms2.<br>
3D plotting handled with Qt Data Visualization.<br>
2D plotting handled with custom QWidget inspired by Krita Tongue Widget.<br>

lcms2 and JPEG XL dependency borrowed from Krita.

To build:
- This branch have a different build steps than the main branch...
- Build 3rdparty dependencies first and install it to the main build folder
- Configure main project into main build folder where deps are installed
- Build project
