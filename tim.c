#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tim.h"
#include "tga.h"

extern FILE *lg;
#define lprintf(x...) printf(x); fprintf(lg, x); fflush(lg);

static FILE *fd;
static int tim_size;

static FILE *tg;
static TGA_FILEHEADER tga;
static unsigned char *tga_buf; 

static TIM_FILE_HEADER tim_head;
static TIM_CLUT_HEADER tim_clut;
static TIM_IMG_HEADER tim_img;

static int tim_clut_pos;
static int tim_img_pos;

static void create_tga(const char *name, int width, int height, char bpp);
static void write_to_tga(unsigned char *data, int x, int y, int width, int height);
static void close_tga();

int open_tim(const char *name, const char *obj_name)
{
	char tim_name[256];
	
	sprintf(tim_name, "%s.TIM", name);
	
	if (fd != NULL)
		fclose(fd);

	fd = fopen(tim_name, "rb");
	
	if (fd == NULL) {
		return 0;
	}
	
	fseek(fd, 0, SEEK_END);
    tim_size = ftell(fd);
    
    fseek(fd, 0, SEEK_SET);
    
    fread(&tim_head, sizeof(TIM_FILE_HEADER), 1, fd);
    
    if (tim_head.id != 0x10) {
    	lprintf("Unsupported file!\n");
    	return 0;
	}
	
	fread(&tim_clut, sizeof(TIM_CLUT_HEADER), 1, fd);
	tim_clut_pos = ftell(fd);
	fseek(fd, sizeof(TIM_FILE_HEADER) + tim_clut.clut_length, SEEK_SET);
	fread(&tim_img, sizeof(TIM_IMG_HEADER), 1, fd);
	tim_img_pos = ftell(fd);

#ifdef SINGLE_TEX
	sprintf(tim_name, "%s", name);
#else
	sprintf(tim_name, "%s_%s", obj_name, name);
#endif
	
	create_tga(tim_name, tim_img.width * 4, tim_img.height, 32);
	
	return tim_size;
}

int tim_width()
{
	return tim_img.width * 4;
}

int tim_height()
{
	return tim_img.height;
}

void close_tim()
{
	if (fd != NULL) {
		fclose(fd);
		fd = NULL;
	}

	close_tga();
}

static unsigned int *img_data;
static unsigned short *clut_data;

void read_tim(unsigned char *uv, int x, int y, int stp)
{
	int i, j;
	unsigned int x_max = 0, x_min = 256, y_max = 0, y_min = 256;
	unsigned int width, height, half, pos;
	unsigned char b;
	unsigned short c;
	float conv = 8.226;
	unsigned int d;
	
	for (i = 0; i < 3; i++) {
		if(uv[2 * i] > x_max) {
			x_max = uv[2 * i];
		}
		
		if(uv[2 * i] < x_min) {
			x_min = uv[2 * i];
		}
		
		if(uv[2 * i + 1] > y_max) {
			y_max = uv[2 * i + 1];      
		}
		
		if(uv[2 * i + 1] < y_min) {
			y_min = uv[2 * i + 1];
		}
	}
	
	if (x_max >= tim_width())
		x_max = tim_width() - 1;
		
	if (y_max >= tim_height())
		y_max = tim_height() - 1;
	
	width = x_max - x_min + 1;
	height = y_max - y_min + 1;
	
	if (width == 1 || height == 1)
		return;
	
	//printf("width %d, height %d, x %d, y %d, clut x %d, clut y %d, stp %d\n", width, height, x_min, y_min, x, y, stp);
	//system("pause");
	
	if ((width + x_min) > tim_width() || (height + y_min) > tim_height()) {
		lprintf("Wrong uv: width %d, height %d, x %d, y %d, clut: x %d, y %d\n", width, height, x_min, y_min, x, y);
		lprintf("Tim width %d, height %d\n", tim_width(), tim_height());
		for(i = 0; i < 4; i++)
			lprintf("uv%d %d %d\n", i, uv[i * 2], uv[i * 2 + 1]);
		
		//system("pause");
		return;
	}

	img_data = (unsigned int *)malloc(width * height * 4);
	clut_data = (unsigned short *)malloc(tim_clut.width * 2);

	if (img_data == NULL || clut_data == NULL) {
		lprintf("Failed to allocate memory!\n");
		return;
	}
	
	fseek(fd, tim_clut_pos + tim_clut.width * 2 * y + x * 2, SEEK_SET);
	fread(clut_data, tim_clut.width * 2, 1, fd);

	for (i = 0; i < height; i++) {
		fseek(fd, tim_img_pos + tim_img.width * 2 * (i + y_min) + x_min/2, SEEK_SET);
		
		half = 0;
		
		if (x_min % 2 > 0) {
			b = fgetc(fd);
			half = 1;
		}

		for (j = half; j < width + half; j++) {
			
			if (j % 2 == 0) {
				b = fgetc(fd);
				c = clut_data[b & 0x0F] & 0x83E0;
				c |= (clut_data[b & 0x0F] & 0x7C00) >> 10;
				c |= (clut_data[b & 0x0F] & 0x001F) << 10;
			} else {
				c = clut_data[b >> 4] & 0x83E0;
				c |= (clut_data[b >> 4] & 0x7C00) >> 10;
				c |= (clut_data[b >> 4] & 0x001F) << 10;
			}
			
			//Playstations RGB values aren't linear to normal RGB values (fix me?)
			d = (int)((float)(c & 0x001F) * conv);
			d |= (int)((float)((c & 0x03E0) >> 5) * conv) << 8;
			d |= (int)((float)((c & 0x7C00) >> 10) * conv) << 16;

			if (stp) {
				if (c == 0x0000)
					d = 0x00000000;
				else if (c == 0x8000)
					d = 0x7F000000;
				else if (c & 0x8000)
					d |= 0x7F000000;
				else 
					d |= 0xFF000000;
			} else {
				if (c == 0x0000)
					d = 0x00000000;
				else
					d |= 0xFF000000;
			}
	
			img_data[i * width + j - half] = d;
		}
	}

	write_to_tga((unsigned char *)img_data, x_min, y_min, width, height);
	
	free(img_data);
	free(clut_data);
	
	img_data = NULL;
	clut_data = NULL;
}

static void create_tga(const char *name, int width, int height, char bpp)
{
	char tga_name[256];
	int tga_ex = 0;

	if (tg != NULL)
		fclose(tg);
	
	sprintf(tga_name, "%s.TGA", name);
	
	memset(&tga, 0, sizeof(TGA_FILEHEADER));
		
	tga.image_type = 2;
    tga.pixel_depth = bpp;
    tga.width = width;
    tga.height = height;
	
    tga_buf = malloc(width * height * 4);
    memset(tga_buf, 0, (width * height * 4));
    
	tg = fopen(tga_name, "rb");
	
	if (tg != NULL) {
		//printf("tga exist \n");
		fseek(tg, sizeof(TGA_FILEHEADER), SEEK_SET);
    	fread(tga_buf, (width * height * 4), 1, tg);
		tga_ex = 1;
	} else {
		lprintf("Create tga: width %d height %d bpp %d\n", width, height, bpp);
	}

	fclose(tg);

	tg = fopen(tga_name, "wb");

    fwrite(&tga, sizeof(TGA_FILEHEADER), 1, tg);
}

static void write_to_tga(unsigned char *data, int x, int y, int width, int height)
{
	int i;
		
	for (i = 0; i < height; i++)
		memcpy((tga_buf + (tga.width * 4) * (tga.height - (y + 1 + i)) + x * 4), data + width * 4 * i, width * 4);

}

static void close_tga()
{
	if (tg != NULL) {
		fseek(tg, sizeof(TGA_FILEHEADER), SEEK_SET);
		fwrite(tga_buf, (tga.width * tga.height * 4), 1, tg);
		free(tga_buf);
		fclose(tg);
		tg = NULL;
	}
}

