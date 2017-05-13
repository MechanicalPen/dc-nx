
#include "../shim.h"
#include "bmploader.h"
#include "bmploader.fdh"

// load a bitmap file.
// header information is placed in hdr->
// returns a pointer to the raw image data.
// the data will be 8bpp for 1-bit, 4-bit, and 8-bit bitmaps,
// and 24bpp for 16, 24, and 32-bit bitmaps.
uint16_t *bmp_load(const char *fname, BMPHeader *hdr)
{
FILE *fp;
int i, x, y;
uint8_t ch;
uint16_t palette[256];

	stat("bmp_load: opening '%s'", fname);
	fp = SDLS_fopen(fname, "rb");
	if (!fp)
	{
		staterr("bmp_load: failed open of %s", fname);
		return NULL;
	}
	
	#define READ_ELEMENT(E)		fread(&E, sizeof(E), 1, fp);
	
	READ_ELEMENT(hdr->type);
	READ_ELEMENT(hdr->filesize);
	READ_ELEMENT(hdr->reserved);
	READ_ELEMENT(hdr->data_offset);
	READ_ELEMENT(hdr->info_size);
	READ_ELEMENT(hdr->bmWidth);
	READ_ELEMENT(hdr->bmHeight);
	READ_ELEMENT(hdr->planes);
	READ_ELEMENT(hdr->bpp);
	READ_ELEMENT(hdr->biCompression);
	READ_ELEMENT(hdr->biSizeImage);
	READ_ELEMENT(hdr->biXPelsPerMeter);
	READ_ELEMENT(hdr->biYPelsPerMeter);
	READ_ELEMENT(hdr->biColorsUsed);
	READ_ELEMENT(hdr->biColorsImportant);
	
	if (hdr->type != 'MB')
	{
		staterr("bmp_load: magick failed opening BMP file '%s'", fname);
		fclose(fp);
		return NULL;
	}

	/*stat("type = %08x", hdr->type);
	stat("filesize = %08x", hdr->filesize);
	stat("reserved = %08x", hdr->reserved);
	stat("data_offset = %08x", hdr->data_offset);
	stat("info_size = %08x", hdr->info_size);
	stat("planes %d", hdr->planes);
	stat("image size %dx%dx%d", hdr->bmWidth, hdr->bmHeight, hdr->bpp);
	stat("Number of colors used: %d", hdr->biColorsUsed);
	*/
	
	// load palette if needed
	if (hdr->bpp <= 8)
	{
		int ncolors = (1 << hdr->bpp);
		int topcolor;
		
		// i found some bmps have a 0 for this setting. now that's not right...
		if (hdr->biColorsUsed == 0 || hdr->biColorsUsed > ncolors)
			topcolor = (ncolors - 1);
		else
			topcolor = (hdr->biColorsUsed - 1);
		
		stat("Reading %d palette entries...", topcolor+1);
		for(i=0;i<=topcolor;i++)
		{
			uint8_t b = fgetc(fp);
			uint8_t g = fgetc(fp);
			uint8_t r = fgetc(fp);
			fgetc(fp);		// reserved entry
			
			palette[i] = (((r & 0xf8) << 8) | \
						  ((g & 0xfc) << 3) | \
						  ((b & 0xf8) >> 3));
		}
	}

	int line;
	switch(hdr->bpp)
	{
		case 1: line = hdr->bmWidth / 8; break;
		case 4: line = hdr->bmWidth / 2; break;
		case 8: line = hdr->bmWidth; break;
		
		default:
		{
			staterr("bmp_load: unsupported bitmap bpp %d", hdr->bpp);
			fclose(fp);
			return NULL;
		}
		break;
	}

	// calculate the padded width of the BMP on disk, since each line is padded to 4 pixels
	int bmp_pad_width = Pad4(line);
	int bmp_size_bytes = (bmp_pad_width * hdr->bmHeight);
	
	// read in the raw image data
	stat("Reading in %d bytes of raw image data...", bmp_size_bytes);
	uint8_t *inbuffer = (uint8_t *)malloc(bmp_size_bytes);
	
	fread(inbuffer, bmp_size_bytes, 1, fp);
	fclose(fp);
	
	
	stat("Converting image data...");
	
	// allocate space for the final 16bpp image after conversion
	// place lineptr starting at bottom line as image is stored upside-down in file
	uint16_t *outbuffer = (uint16_t *)malloc(hdr->bmWidth * hdr->bmHeight * 2);
	uint16_t *lineptr = &outbuffer[(hdr->bmHeight - 1) * hdr->bmWidth];
	
	switch(hdr->bpp)
	{
		case 1:					// monochrome (bitmap) mode--each pixel is one bit
		{
		    int sub_pointer = 0;
		
		    for(y=0;y<hdr->bmHeight;y++)
		    {
			uint16_t *outptr = lineptr;
			int sub_portion = 7;
			
			ch = inbuffer[sub_pointer];
			for(x=0;x<hdr->bmWidth;x++)
			{
			    uint8_t pixel = (ch & (1 << sub_portion)) >> sub_portion;	
			    *(outptr++) = palette[pixel];
			
			    if (!sub_portion)
			    {
				sub_portion = 7;
				ch = inbuffer[++sub_pointer];
			    }
			    else sub_portion--;
			}
			
			while(sub_pointer & 3) sub_pointer++;
			lineptr -= hdr->bmWidth;
		    }
		}
		break;
	    
		case 4:					// 16 color mode--each pixel is one nibble
		{
			int sub_pointer = 0;
			
			for(y=0;y<hdr->bmHeight;y++)
			{
				uint16_t *outptr = lineptr;
				int sub_portion = 0;
				
				for(x=0;x<hdr->bmWidth;x++)
				{
					if (!sub_portion)
					{
						ch = inbuffer[sub_pointer] >> 4;
						sub_portion = 1;
					}
					else
					{
						ch = inbuffer[sub_pointer++] & 0x0f;
						sub_portion = 0;
					}
					
					*(outptr++) = palette[ch];
				}
				
				while(sub_pointer & 3) sub_pointer++;
				lineptr -= hdr->bmWidth;
			}
		}
		break;
		
		case 8:		// 256-color
		{
			i = 0;
			for(y=0;y<hdr->bmHeight;y++)
			{
				uint16_t *outptr = lineptr;
				for(x=0;x<hdr->bmWidth;x++)
					*(outptr++) = palette[inbuffer[i++]];
				
				while(i & 3) i++;
				lineptr -= hdr->bmWidth;
			}
		}
		break;
	}
	
	free(inbuffer);
	return outbuffer;
}

// pads it's input value to a multiple of 4 bytes
static int Pad4(int invalue)
{
	int b = (invalue & 3);
	return b ? (invalue + (4 - b)) : invalue;
}


/*static uint16_t *convert_to_16bpp(uint8_t *inbuffer, int width, int height)
{
int npixels = (width * height);
uint16_t *outbuffer, *outptr;
uint8_t *inptr;
int outsize;

	outsize = (npixels * 2);
	outbuffer = (uint16_t *)malloc(outsize);
	
	inptr = inbuffer;
	outptr = outbuffer;
	
	for(int i=0;i<npixels;i++)
	{
		uint8_t r = *(inptr++);
		uint8_t g = *(inptr++);
		uint8_t b = *(inptr++);
		
		uint16_t color = ((r & 0xf8) << 8) | \
						 ((g & 0xfc) << 3) | \
						 ((b & 0xf8) >> 3);
		
		*(outptr++) = color;
	}
	
	return outbuffer;
}
*/

/*
	case 1:					// monochrome (bitmap) mode--each pixel is one bit
	{
		int sub_pointer = 0;
		
		for(y=0;y<hdr->bmHeight;y++)
		{
			uint8_t *outptr = lineptr;
			int sub_portion = 7;
			
			ch = inbuffer[sub_pointer];
			for(x=0;x<hdr->bmWidth;x++)
			{
				uint8_t pixel = (ch & (1 << sub_portion)) >> sub_portion;
				*(outptr++) = hdr->palette.entries[pixel].r;
				*(outptr++) = hdr->palette.entries[pixel].g;
				*(outptr++) = hdr->palette.entries[pixel].b;
				
				if (!sub_portion)
				{
					sub_portion = 7;
					ch = inbuffer[++sub_pointer];
				}
				else sub_portion--;
			}
			
			while(sub_pointer & 3) sub_pointer++;	// DWORD-align
			lineptr -= outpitch;
		}
	}
	break;

	case 24:	// 24-bpp
	case 32:	// 32-bpp
	{
		i = 0;
		for(y=0;y<hdr->bmHeight;y++)
		{
			uint8_t *outptr = lineptr;
			
			for(x=0;x<hdr->bmWidth;x++)
			{
				uint8_t b = inbuffer[i++];
				uint8_t g = inbuffer[i++];
				uint8_t r = inbuffer[i++];
				if (hdr->bpp == 32) i++;
				
				*(outptr++) = r;
				*(outptr++) = g;
				*(outptr++) = b;
			}
			
			while(i & 3) i++;
			lineptr -= outpitch;
		}
	}
*/


