enum TIM_BPP {
	bpp4 = 0,
	bpp8 = 1,
	bpp16 = 2,
	bpp24 = 3
};

typedef struct {
    unsigned char id;
    unsigned char ver;
    unsigned char pad1[2];
    unsigned char bpp : 2;
    unsigned char pad_a : 1;
    unsigned char clp : 1;
    unsigned char pad_b : 4; 
	unsigned char pad2[3];
} TIM_FILE_HEADER;

typedef struct {
	int clut_length;
	unsigned short x;
	unsigned short y;
	unsigned short width;
	unsigned short height;
} TIM_CLUT_HEADER;

typedef struct {
	unsigned char r : 5;
	unsigned char g : 5;
	unsigned char b : 5;
	unsigned char stp : 1;
} TIM_CLUT_COLOR;

typedef struct {
	int img_length;
	unsigned short x;
	unsigned short y;
	unsigned short width;
	unsigned short height;
} TIM_IMG_HEADER;

int open_tim(const char *name, const char *obj_name);
void close_tim();
int tim_width();
int tim_height();
void read_tim(unsigned char *uv, int x, int y, int stp);

#define SINGLE_TEX

