# NTSC-CRT
NTSC video signal encoding / decoding emulation by EMMIR 2018-2023
================================================================

![alt text](/scube.png?raw=true)
![alt text](/kc.png?raw=true)

The result of going down a very deep rabbit hole.
I learned a lot about analog signal processing, television, and the NTSC standard in the process.
Written to be compatible with C89.

Just like King's Crook (my from-scratch 3D game), this code follows the same restrictions:

1. Everything must be done in software, no explicit usage of hardware acceleration.
2. No floating point types or literals, everything must be integer only.
3. No 3rd party libraries, only C standard library and OS libraries for window, input, etc.
4. No languages used besides C.
5. No compiler specific features and no SIMD.
6. Single threaded.

This program performs relatively well and can be easily used in real-time applications
to emulate NTSC output.

================================================================
Feature List:

- Somewhat realistic/accurate NTSC image output with bandlimited luma/chroma
- VSYNC and HSYNC
- Signal noise (optional)
- Interlaced and progressive scan
- Monochrome and full color

Note the command line program provided does not let you mess with all the settings
like black/white point, brightness, saturation, and contrast.

In the ntsc_crt.c file, there are two main()'s.
One is for a command line program and the other uses my FW library (found here https://github.com/LMP88959/PL3D-KC)
to provide real-time NTSC emulation with adjustable parameters.

================================================================

Compiling:
```
  cd NTSC-CRT
  
  cc -O3 -o ntsc *.c

```

```
usage: ./a.out -m|o|f|p|h outwidth outheight noise infile outfile
sample usage: ./a.out -op 640 480 24 in.ppm out.ppm
sample usage: ./a.out - 832 624 0 in.ppm out.ppm
-- NOTE: the - after the program name is required
------------------------------------------------------------
  m : monochrome
  o : do not prompt when overwriting files
  f : odd field (only meaningful in progressive mode)
  p : progressive scan (rather than interlaced)
  h : print help

by default, the image will be full color and interlaced
```
If you have any questions feel free to leave a comment on YouTube OR
join the King's Crook Discord server :)

YouTube: https://www.youtube.com/@EMMIR_KC/videos

Discord: https://discord.gg/hdYctSmyQJ

itch.io: https://kingscrook.itch.io/kings-crook

## License
There is no explicit license but feel free to use the code.
If you release anything with it, a comment in your code/README
saying where you got this code would be a nice gesture but it’s not mandatory.
Thank you for your interest!
