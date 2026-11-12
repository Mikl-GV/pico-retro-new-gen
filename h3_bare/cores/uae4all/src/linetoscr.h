
static void LNAME (int dpix, int stoppos)
{


/* CASO DUAL */

    unsigned short *buf = ((unsigned short *)xlinebuffer);
    int spix=src_pixel;
#if defined(DREAMCAST)
	    register int resto=(((unsigned)&buf[dpix])&0x1f);
	    if (resto)
		resto=0x20-resto;
	    if (resto>(stoppos-dpix))
		resto=(stoppos-dpix);
	    while (resto>0) {
	    	buf[dpix++]= (colors_for_drawing.acolors[pixdata.apixels[spix]]);
	    	spix += SRC_INC;
		resto-=2;
	    }
	    if (dpix >= stoppos){
		src_pixel = (int)spix;
		return;
	    }

	    unsigned int *d = (unsigned int *)(void *)
		    (0xe0000000 | (((unsigned long)&buf[dpix]) & 0x03ffffe0));
	    unsigned short *s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]];
	    unsigned n=((stoppos-dpix)>>4);
	    {
		    register unsigned tmp=n<<4;
		    resto=(stoppos-dpix)-tmp;
		    dpix+=tmp;
	    }
	    QACR0 = ((((unsigned int)&buf[dpix])>>26)<<2)&0x1c;
	    QACR1 = ((((unsigned int)&buf[dpix])>>26)<<2)&0x1c;
	    while(n--) {
//		    asm("pref @%0" : : "r" (s + 8));
		    register unsigned dato;

		    dato=*s; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]]; dato|=(((unsigned)*s)<<16);
		    d[0] = dato; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]];
		    dato=*s; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]]; dato|=(((unsigned)*s)<<16);
		    d[1] = dato; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]];
		    dato=*s; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]]; dato|=(((unsigned)*s)<<16);
		    d[2] = dato; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]];
		    dato=*s; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]]; dato|=(((unsigned)*s)<<16);
		    d[3] = dato; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]];
		    dato=*s; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]]; dato|=(((unsigned)*s)<<16);
		    d[4] = dato; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]];
		    dato=*s; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]]; dato|=(((unsigned)*s)<<16);
		    d[5] = dato; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]];
		    dato=*s; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]]; dato|=(((unsigned)*s)<<16);
		    d[6] = dato; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]];
		    dato=*s; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]]; dato|=(((unsigned)*s)<<16);
		    d[7] = dato; spix += SRC_INC; s = (unsigned short *)&colors_for_drawing.acolors[pixdata.apixels[spix]];
		    asm("pref @%0" : : "r" (d));
		    d+=8;
	    }
	    d = (unsigned int *)0xe0000000;
	    d[0] = d[8] = 0;

	    while (resto>0) {
		    buf[dpix++]= (colors_for_drawing.acolors[pixdata.apixels[spix]]);
		    spix += SRC_INC;
		    resto--;
	    }

#else
#if defined(HDOUBLE)
	/* r1259: lores — пиксель источника в два столбца буфера (640 px),
	 * индекс вывода = 2*dpix-относительно сдвинутой базы. */
	while (dpix < stoppos) {
	    unsigned short c = (colors_for_drawing.acolors[pixdata.apixels[spix]]);
	    buf[(dpix << 1)] = c;
	    buf[(dpix << 1) + 1] = c;
	    dpix++;
	    spix += SRC_INC;
	}
#else
	/* hires: 640 точек источника в 640 столбцов буфера, 1:1 */
	while (dpix < stoppos) {
	    buf[dpix << 1]= (colors_for_drawing.acolors[pixdata.apixels[spix]]);
	    dpix++;
	    spix += SRC_INC;
	}
#endif
#endif
    src_pixel = (int)spix;
}

#undef LNAME
#undef HDOUBLE
#undef SRC_INC
