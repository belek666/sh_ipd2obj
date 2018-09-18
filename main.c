#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "tim.h"

typedef struct {
        unsigned short id;     	// file id, 0x0630 for plm/ilm
        unsigned char flag;		// 0x01 when file is loaded by game
        unsigned char tex_num;  // number of used textures
        int tex_name_offset;	// start offset of texture file name table (24 bytes for each file name)
        int obj_num;			// number of objects in file
        int obj_start_offset;	// offset to object list
        int unk_data_offset;	// offset to some kind of data, maybe object's index or drawing order
} PLM_FILE_HEADER;

typedef struct {
        char name[8];			// object name
        unsigned char mesh_num;	// number of meshes in object/model
		unsigned char b;		// unknown
		unsigned char c;		// unknown
		unsigned char d;		// unknown, rendering setting? 
								// bit 0 -  turns on shading when set
								// bit 3-4 - turns off textures when set
								// bit 5 - turns on semitransparency when set 
        int data_offset;		// offset to object data
} PLM_OBJ_HEADER;

typedef struct {
        unsigned char pack_num; // number of polygon packets
		unsigned char vert_num; // number of vertices
		unsigned char num_c;    // unknown
		unsigned char num_d;	// unknown
        int pack_offset;		// offset to polygon packets
        int vert_xy_offset;		// offset to xy vertices table
        int vert_z_offset;		// offset to z vertices table
        int normal_offset; 		// unknown, 4 byte long parts (size = num_c * 4)
        int end_offset;			// offset to end of object's data and/or to next object
} PLM_DATA_HEADER;

typedef struct {
        unsigned char u0, v0;		// X and Y coordinate of texture
		unsigned short cba;			// bits  0-5 - upper 6 bits of 10 bits of X coordinate value for CLUT
									// bits  6-14 - 9 bits of Y coordinate value for CLUT
		unsigned char u1, v1;		// X and Y coordinate of texture
		unsigned char unk1;			// unknown
		unsigned char tex_num : 7;	// texture number? (0 - first texture, 0x7F - no texture ?)
		unsigned char unk2 : 1; 	// semitransparency ?
		unsigned char u2, v2;		// X and Y coordinate of texture
		unsigned char u3, v3;		// X and Y coordinate of texture
        unsigned char faces[4]; 	// faces element index
        unsigned char normals[4];  	// normal element index? (max index value = num_d)
} PLM_PACK_HEADER;

typedef struct {
    unsigned char id;			// 0x14
    unsigned char flag;			// 0x01 when file is loaded by game
    char x_pos;					// same as is in file name
    char y_pos;					// same as is in file name
    int plm_offset;				// offset to plm side of ipd file
    unsigned char obj_num; 		// number of objects on the list
    unsigned char pos_num;		// number objs pos header data 
    unsigned char unk1_num;		// drawing distance table size?
    unsigned char unk2;
    char pad1[8];
    int obj_name_offset;		// offset to obj name list
    int obj_data_offset;		// offset to positon data header
	char unk1_data[52];			// drawing distance global table?
	int unkdata_offset;			// looks like obj indices (pos from the list); 1-byte * unk1_num; drawing distance table per obj?
} IPD_FILE_HEADER;

typedef struct {				// all offsets += sizeof(IPD_FILE_HEADER) (0x54)
	int col_x_pos;				// global col map x position
	int col_y_pos;				// global col map y position
	int col_z_pos;				// global col map z position
	int unk1_offset;			// offset to col 3d map vertices?
	int unk2_offset;
	int unk3_offset;
	int unk4_offset;
	int unk1_data;
	int unk5_offset;
	unsigned short unk6_size;
	unsigned short unk7_size;
	int unk6_offset;
	int unk7_offset;
} IPD_COL_HEADER;

typedef struct {
	int flag;						// 0 - mesh from inside IPD file; 1 - mesh from _GLB.PLM file 
	char name[8];					// object name
	int unk;						// here goes address when file is loaded by game
} IPD_OBJNAME_DATA;

//pos_data_offset
typedef struct {
	unsigned char obj_num; 		// number of objects
	unsigned char unk1_num;     // sub objects (tree leaves positon data)?
	unsigned char unk2_num;		// drawing distance data length?
	unsigned char unk3_num;		// always 0 ?
	int unk2;
	int unk3;
	int data_offset;		// offset to obj positioning data
	int unk1_offset;		// if unk1_num != 0 offset to unknown (position data 8 bytes - 1 vertex)
	int unk2_offset;  		// unknown2 data offset, 8 byte per pack?
} IPD_POS_HEADER;

typedef struct {
	int obj_id; 				// object index number from list (0 - first entry from list)
	short rt11, rt12, rt13;		// rotation matrix
	short rt21, rt22, rt23;
	short rt31, rt32, rt33;
	short pad;
	int tx; 					// translation vector, short (2 bytes) actualy, FF 80 - 'max -'; FF 7F - 'max +'
	int ty;
	int tz;
} IPD_OBJ_DATA;

FILE *lg;
#define lprintf(x...) printf(x); fprintf(lg, x); fflush(lg);

static IPD_FILE_HEADER ipdm;
static PLM_FILE_HEADER plmm;

static FILE *fd;
static int ipd_size;

static int open_main_file(const char *name);
static void close_main_file();

static void extract_ipd(const char *name);
static void extract_plm(const char *name);
static int extract_object(const char *obj_name, FILE *fdp, int plm_offset, IPD_OBJ_DATA *pst, char x_pos, char y_pos);

static void create_file(const char *name, const char *ext);
static void write_to_file(unsigned char *data, int size);
static void close_file();
static int file_pos();
static void create_mtl(const char *name);
static void add_tex_to_mtl(const char *name, const char *obj);
static void close_mtl();

static float scale = 0.003906;
static short map_max = 10246;

//#define IGNORE_4TH

int main(int argc, char *argv[]) 
{
	int ret;
	int i;
	char ipd_name[256];
	char log_name[256];

	if (argc > 1) {
		
		for (i = 1; i < argc; i++) {
			
    		printf("File name: %s \n", argv[i]);
      
    		ret = open_main_file(argv[i]);
      
    		if(ret == 0)
      			goto end;

			strncpy(ipd_name, strrchr(argv[i], '\\') + 1, strrchr(argv[i], '.') - strrchr(argv[i], '\\') - 1);
			
			ipd_name[strrchr(argv[i], '.') - strrchr(argv[i], '\\') - 1] = 0;
    	
    		sprintf(log_name, "%s.LOG", ipd_name);
    		lg = fopen(log_name, "w");

			if (ret == 1)
    			extract_ipd(ipd_name);
    		else if (ret == 2)
    			extract_plm(ipd_name);
end:    	
    		close_main_file();
			fclose(lg);
		}
	} else {
    	printf("No file dropped on exe! \n");
    	printf("To use drop file on this program. \n");
  	}
  
	system("PAUSE");
	return 0;
}

static int open_main_file(const char *name)
{
	int i;

    fd = fopen(name, "rb");
    
    if (fd == NULL) {
    	printf("Failed to open %s\n", name);
    	return 0;
	}
    
    fseek(fd, 0, SEEK_END);
    ipd_size = ftell(fd);
    
    fseek(fd, 0, SEEK_SET);
   
    fread(&ipdm, sizeof(IPD_FILE_HEADER), 1, fd);
    
	if (ipdm.id == 0x14) { //ipd file
		return 1;
	} else if (ipdm.id == 0x30) {
		fseek(fd, 0, SEEK_SET);
		fread(&plmm, sizeof(PLM_FILE_HEADER), 1, fd);
		if (plmm.id == 0x0630)
			return 2;
	}
	
	printf("Unsupported file!\n");
	
	return 0;
}

static void close_main_file()
{
	if (fd != NULL) {
		fclose(fd);
	}
}

static int obj_cnt[256];
static char obj_usg[256];
static int vt_index; 
static int v_index;
static int vn_index;

static void extract_ipd(const char *name)
{
	int i, j, k;
	IPD_FILE_HEADER ipd;
	IPD_POS_HEADER pos;
	IPD_OBJ_DATA dta;
	IPD_OBJNAME_DATA objn;
	PLM_FILE_HEADER tplm;
	PLM_OBJ_HEADER tobj;
	IPD_OBJ_DATA subd;
	short txyz[4];
	FILE *nfd;
	char temp[256];
	char ext[256];
	int ret;
	
	int obj_pos;

	lprintf("Extracting %s\n", name);
	
	memset(obj_cnt, 0, 4 * 256);
	memset(obj_usg, 0, 256);

	create_file(name, "OBJ");
	create_mtl(name);
	
	sprintf(temp, "mtllib %s.MTL\n", name);
    write_to_file(temp, strlen(temp));
	
	vt_index = 1;
	v_index = 0;
	vn_index = 1;
	
	memset(ext, 0, sizeof(ext));
	strncpy(ext, name, strlen(name) - 4);
	sprintf(temp, "%s_GLB.PLM", ext);
	lprintf("Opening PLM file %s \n", temp);
	nfd = fopen(temp, "rb");

	lprintf("Number of positioning data %d\n", ipdm.pos_num);
	
	for (i = 0; i < ipdm.pos_num; i++) {
		fseek(fd, ipdm.obj_data_offset + sizeof(IPD_POS_HEADER) * i, SEEK_SET);
		fread(&pos, sizeof(IPD_POS_HEADER), 1, fd);
	
		for (j = 0; j < pos.obj_num; j++) {
			fseek(fd, pos.data_offset + sizeof(IPD_OBJ_DATA) * j, SEEK_SET);
			fread(&dta, sizeof(IPD_OBJ_DATA), 1, fd);
		
			lprintf("***********************************\n");
			lprintf("Object id %d hpos 0x%04X dpos 0x%04X\n", dta.obj_id, ipdm.obj_data_offset + sizeof(IPD_POS_HEADER) * i, pos.data_offset + sizeof(IPD_OBJ_DATA) * j);
		
			fseek(fd, ipdm.obj_name_offset + 16 * dta.obj_id, SEEK_SET);
			fread(&objn, sizeof(IPD_OBJNAME_DATA), 1, fd);
			
			if (pos.unk1_num > 0 && strcmp(objn.name, "TREE02") == 0) {				

				memset(&subd, 0, sizeof(IPD_OBJ_DATA));
				fseek(fd, pos.unk1_offset, SEEK_SET);
				fread((char *)&txyz, 8, 1, fd);
				
				subd.rt11 = 0x1000;
				subd.rt22 = 0x1000;
				subd.rt33 = 0x1000;
				
				subd.tx = txyz[0];
				subd.ty = txyz[1];
				subd.tz = txyz[2];
								
				extract_object("LEAF_1", nfd, 0, &subd, ipdm.x_pos, ipdm.y_pos);
				//extract_object("LEAF_2", nfd, 0, &subd, ipdm.x_pos, ipdm.y_pos);
			}

			ret = -1;

			if (objn.flag == 0) { //obj from ipd
				ret = extract_object(objn.name, fd, ipdm.plm_offset, &dta, ipdm.x_pos, ipdm.y_pos);

				if (ret > -1) {
					obj_usg[ret] = 1;
				}
			} else if (objn.flag == 1) { //obj from glb plm
				ret = extract_object(objn.name, nfd, 0, &dta, ipdm.x_pos, ipdm.y_pos);
			} else {
				lprintf("Unknown obj flag %d\n", objn.flag);
			}

			if (ret < 0) {
				lprintf("Object %s not found\n", objn.name);
			}
		}
	}
	
	fseek(fd, ipdm.plm_offset, SEEK_SET);
	fread(&tplm, sizeof(PLM_FILE_HEADER), 1, fd);
	
	lprintf("\n***********************************\n");
	lprintf("Searching for unused meshes\n");
	lprintf("***********************************\n\n");

	for (i = 0; i < tplm.obj_num; i++) {
		if (obj_usg[i] == 0) {
			fseek(fd, ipdm.plm_offset + tplm.obj_start_offset + sizeof(PLM_OBJ_HEADER) * i, SEEK_SET);
        	fread(&tobj, sizeof(PLM_OBJ_HEADER), 1, fd);
        
        	strncpy(temp, tobj.name, 8);
    		temp[8] = 0;
    		
    		memset(&subd, 0, sizeof(IPD_OBJ_DATA));
    		subd.rt11 = 0x1000;
			subd.rt22 = 0x1000;
			subd.rt33 = 0x1000;
			
    		lprintf("Object %s not used by ipd\n", temp);
    		extract_object(temp, fd, ipdm.plm_offset, &subd, ipdm.x_pos, ipdm.y_pos);
		}
	}

	lprintf("v_index %d vt_index %d vn_index %d\n", v_index, vt_index, vn_index);
	
	close_mtl();
	close_file();
	fclose(nfd);
}

static void extract_plm(const char *name) 
{
	int i;
	PLM_OBJ_HEADER obj;
	char obj_name[9];
	char temp[256];

	lprintf("Extracting %s\n", name);
	
	memset(obj_cnt, 0, 4 * 256);

	create_file(name, "OBJ");
	create_mtl(name);
	
	sprintf(temp, "mtllib %s.MTL\n", name);
    write_to_file(temp, strlen(temp));
	
	vt_index = 1;
	v_index = 0;
	vn_index = 1;
	
	for (i = 0; i < plmm.obj_num; i++) {
		fseek(fd, plmm.obj_start_offset + sizeof(PLM_OBJ_HEADER) * i, SEEK_SET);
        fread(&obj, sizeof(PLM_OBJ_HEADER), 1, fd);
        
        strncpy(obj_name, obj.name, 8);
    	obj_name[8] = 0;
    	
    	extract_object(obj_name, fd, 0, NULL, 0, i * map_max / 20);
	}
	
	close_mtl();
	close_file();
}

static int extract_object(const char *obj_name, FILE *fdp, int plm_offset, IPD_OBJ_DATA *pst, char x_pos, char y_pos)
{
	int i, j, k, m;
	PLM_FILE_HEADER plm;
	PLM_OBJ_HEADER obj;
    PLM_DATA_HEADER dta;
    PLM_PACK_HEADER pack;
	char tex_names[20][24];
	char obj_fix_name[9];
	char temp[256];
	short x, y, z;
	int try_cnt;
    int no_tex;
    int y_fix = 0;
    float fx, fy, fz;
	unsigned char uv_data[8];
	int old_tex_num = 0x7F;

	lprintf("Searching for object %s\n", obj_name);
	
	fseek(fdp, plm_offset, SEEK_SET);
	fread(&plm, sizeof(PLM_FILE_HEADER), 1, fdp);
	
	if (plm.id == 0x0630) { // plm/ilm file
    	fseek(fdp, plm.tex_name_offset + plm_offset, SEEK_SET); 	
    		
		for (i = 0; i < plm.tex_num; i++) {
    		fread(&tex_names[i], 24, 1, fdp);
		}
	} else {
		lprintf("This is not plm file\n");
		return -1;
	}
	
	for (i = 0; i < plm.obj_num; i++) {
		fseek(fdp, plm.obj_start_offset + plm_offset + sizeof(PLM_OBJ_HEADER) * i, SEEK_SET);
        fread(&obj, sizeof(PLM_OBJ_HEADER), 1, fdp);
        
		strncpy(obj_fix_name, obj.name, 8);
    	obj_fix_name[8] = 0;
    	
    	if (obj_name == NULL || strcmp(obj_fix_name, obj_name) == 0) {
    		for (m = 0; m < obj.mesh_num; m++) {
				fseek(fdp, obj.data_offset + plm_offset + sizeof(PLM_DATA_HEADER) * m, SEEK_SET);
        		fread(&dta, sizeof(PLM_DATA_HEADER), 1, fdp);

    			lprintf("Object %s found\n", obj_fix_name);
        		lprintf("Vertices: %d \n", dta.vert_num);
        		
        		/*if (!strncmp(obj_fix_name, "PAD_NEAR", 8)) {
					y_fix = 0xC0;
				} else {
					y_fix = 0;
				}*/

        		sprintf(temp, "o %s_%d%d%d\n", obj_fix_name, obj_cnt[i]/100, (obj_cnt[i]/10 - obj_cnt[i]/100), obj_cnt[i]%10);
        		write_to_file(temp, strlen(temp));
        	
				obj_cnt[i]++;

        		// vertices
        		for (j = 0; j < dta.vert_num; j++) {
        			fseek(fdp, dta.vert_xy_offset + plm_offset + j * 4, SEEK_SET);
        			fread(&x, 2, 1, fdp);
        			fread(&y, 2, 1, fdp);
        	
        			fseek(fdp, dta.vert_z_offset + plm_offset + j * 2, SEEK_SET);
        			fread(&z, 2, 1, fdp);
        			
        			if (pst != NULL) {
     	     			fx = ((pst->tx + pst->rt11*x/0x1000 + pst->rt12*y/0x1000 + pst->rt13*z/0x1000) + (map_max * x_pos)) * scale;
     					fy = ((pst->ty + pst->rt21*x/0x1000 + pst->rt22*y/0x1000 + pst->rt23*z/0x1000) ) * scale;
     					fz = ((pst->tz + pst->rt31*x/0x1000 + pst->rt32*y/0x1000 + pst->rt33*z/0x1000) + (map_max * y_pos)) * scale;
     				} else {
     					fx = (x + (map_max * x_pos)) * scale;
     					fy = y * scale;
     					fz = (z + (map_max * y_pos)) * scale;
					}

        			sprintf(temp, "v %f %f %f\n", fx * -1, fy * -1, fz * 1);
        			write_to_file(temp, strlen(temp));
				}
			
				try_cnt = 0;
    			no_tex = 0;
    			old_tex_num = 0x7F;
			
				// texture cordinates
				for (j = 0; j < dta.pack_num; j++) {
        			fseek(fdp, dta.pack_offset + plm_offset + j * 20, SEEK_SET);
        			fread(&pack, sizeof(PLM_PACK_HEADER), 1, fdp);

        			x = (pack.cba & 0x003F) << 4;
        			y = (pack.cba & 0x7FC0) >> 6;

					pack.v0 += y_fix;
					pack.v1 += y_fix;
					pack.v2 += y_fix;
					pack.v3 += y_fix;

					uv_data[0] = pack.u0;
					uv_data[1] = pack.v0;
					uv_data[2] = pack.u1;
					uv_data[3] = pack.v1;
					uv_data[4] = pack.u2;
					uv_data[5] = pack.v2;
					uv_data[6] = pack.u3;
					uv_data[7] = pack.v3;
					
					//lprintf("tex num %d stp %d unk1 %d clut x %d clut y %d face %d\n", pack.tex_num, pack.unk2, pack.unk1, x, y, pack.faces[3]);
			
					/*if (strcmp(obj_fix_name, "THR9402") == 0  && obj_cnt[i] == 1 + 1) {
						for(k = 0; k < 4; k++)
							lprintf("uv%d %d %d\n", k, uv_data[k * 2], uv_data[k * 2 + 1]);
						
						system("pause");
					}*/
			
					if (pack.tex_num == 0x7F) {
						lprintf("No texture\n");
    					no_tex = 1;
    					old_tex_num = 0x7F;
					} else if (pack.tex_num >= plm.tex_num) {
						lprintf("Wrong tex num %d\n", pack.tex_num);
						no_tex = 1;
						old_tex_num = 0x7F;
					} else if (pack.tex_num != old_tex_num) {
						old_tex_num = pack.tex_num;
						close_tim();
						no_tex = 0;
try_ag:					
						if (open_tim(tex_names[pack.tex_num], obj_fix_name) == 0) {
							lprintf("Failed to open %s.TIM\n", tex_names[pack.tex_num]);
							system("pause");
							try_cnt++;
			
							if (try_cnt > 4) {
								no_tex = 1;
								old_tex_num = 0x7F;
								try_cnt = 0;
							} else {
								goto try_ag;
							}
						}
						lprintf("Using %s.TIM \n", tex_names[pack.tex_num]);
					}
					
					/*if (strcmp(tex_names[pack.tex_num], "THR9401H") == 0) {
						for(k = 0; k < 4; k++)
							lprintf("uv%d %d %d\n", k, uv_data[k * 2], uv_data[k * 2 + 1]);
						
						system("pause");
					}*/
			
					if (no_tex == 0) {
						read_tim(uv_data, x, y, pack.unk2);
					
						sprintf(temp, "vt %f %f\n", (float)(pack.u0 + 0) / tim_width(), 1 - (float)(pack.v0 + 0) / tim_height());
        				write_to_file(temp, strlen(temp));
        				sprintf(temp, "vt %f %f\n", (float)(pack.u1 + 0) / tim_width(), 1 - (float)(pack.v1 + 0) / tim_height());
        				write_to_file(temp, strlen(temp));
        				sprintf(temp, "vt %f %f\n", (float)(pack.u2 + 0) / tim_width(), 1 - (float)(pack.v2 + 0) / tim_height());
        				write_to_file(temp, strlen(temp));
        	
        				if (pack.faces[3] != 0xFF) {
        					
        					uv_data[0] = pack.u1;
							uv_data[1] = pack.v1;
							uv_data[2] = pack.u2;
							uv_data[3] = pack.v2;
							uv_data[4] = pack.u3;
							uv_data[5] = pack.v3;
#ifndef IGNORE_4TH
							read_tim(uv_data, x, y, pack.unk2);
#endif
        					sprintf(temp, "vt %f %f\n", (float)(pack.u3 + 0) / tim_width(), 1 - (float)(pack.v3 + 0) / tim_height());
        					write_to_file(temp, strlen(temp));
						}
					}
				}
			
				//vertex normal - not sure if correct
				for (j = 0; j < dta.pack_num; j++) {
        			fseek(fdp, dta.pack_offset + plm_offset + j * 20, SEEK_SET);
        			fread(&pack, sizeof(PLM_PACK_HEADER), 1, fdp);
        	
        			for (k = 0; k < 3; k++) {
        				fseek(fdp, dta.normal_offset + plm_offset + pack.normals[k] * 4, SEEK_SET);
        			
        				x = y = z = 0;
        			
        				fread(&x, 1, 1, fdp);
        				fread(&y, 1, 1, fdp);
        				fread(&z, 1, 1, fdp);
        	
        				fx = (x * scale);
        				fy = (y * scale);
        				fz = (z * scale);

        				sprintf(temp, "vn %f %f %f\n", fx * -1, fy * -1, fz * 1);
        				write_to_file(temp, strlen(temp));
        			}

        			if (pack.faces[3] != 0xFF) {
        				fseek(fdp, dta.normal_offset + plm_offset + pack.normals[3] * 4, SEEK_SET);
        			
        				x = y = z = 0;
        			
        				fread(&x, 1, 1, fdp);
        				fread(&y, 1, 1, fdp);
        				fread(&z, 1, 1, fdp);
        	
        				fx = (x * scale);
        				fy = (y * scale);
        				fz = (z * scale);

        				sprintf(temp, "vn %f %f %f\n", fx * -1, fy * -1, fz * 1);
        				write_to_file(temp, strlen(temp));
        			}
    			}
    		
    			old_tex_num = 0x7F;

				// faces
				for (j = 0; j < dta.pack_num; j++) {
        			fseek(fdp, dta.pack_offset + plm_offset + j * 20, SEEK_SET);
        			fread(&pack, sizeof(PLM_PACK_HEADER), 1, fdp);
        	
        			if (pack.tex_num != 0x7F && pack.tex_num < plm.tex_num) {
    					if (pack.tex_num != old_tex_num) {
#ifdef SINGLE_TEX
							sprintf(temp, "usemtl %s_TEX\n", tex_names[pack.tex_num]);
#else    					
    						sprintf(temp, "usemtl %s_%s_TEX\n", obj_fix_name, tex_names[pack.tex_num]);
#endif
    						write_to_file(temp, strlen(temp));
    						add_tex_to_mtl(tex_names[pack.tex_num], obj_fix_name);
    						old_tex_num = pack.tex_num;
						}
						sprintf(temp, "f %d/%d/%d %d/%d/%d %d/%d/%d \n", pack.faces[2] + 1 + v_index, vt_index + 2, vn_index + 2, pack.faces[1] + 1 + v_index, vt_index + 1, vn_index + 1, pack.faces[0] + 1 + v_index, vt_index + 0, vn_index + 0);
						write_to_file(temp, strlen(temp));
						if (pack.faces[3] != 0xFF) {
							sprintf(temp, "f %d/%d/%d %d/%d/%d %d/%d/%d \n", pack.faces[3] + 1 + v_index, vt_index + 3, vn_index + 3, pack.faces[1] + 1 + v_index, vt_index + 1, vn_index + 1, pack.faces[2] + 1 + v_index, vt_index + 2, vn_index + 2);
        					write_to_file(temp, strlen(temp));
        					vt_index++;
        					vn_index++;
						}
						vt_index += 3;
    				} else {
    					sprintf(temp, "f %d//%d %d//%d %d//%d \n", pack.faces[2] + 1 + v_index, vn_index + 2, pack.faces[1] + 1 + v_index, vn_index + 1, pack.faces[0] + 1 + v_index, vn_index + 0);
    					write_to_file(temp, strlen(temp));
    					if (pack.faces[3] != 0xFF) {
							sprintf(temp, "f %d//%d %d//%d %d//%d \n", pack.faces[3] + 1 + v_index, vn_index + 3, pack.faces[1] + 1 + v_index, vn_index + 1, pack.faces[2] + 1 + v_index, vn_index + 2);
        					write_to_file(temp, strlen(temp));
        					vn_index++;
						}
						old_tex_num = 0x7F;
					}

					vn_index += 3;
				}
			
				v_index += dta.vert_num;
				close_tim();
			}
			
			if (obj_name != NULL)
				return i;
		}
	}
	
	return -1;
}

static FILE *out;

static void create_file(const char *name, const char *ext)
{
	char file_name[256];
	
	if (out != NULL)
		fclose(out);
		
	sprintf(file_name, "%s.%s", name, ext);
	
	out = fopen(file_name, "wb");
}

static void write_to_file(unsigned char *data, int size)
{
	if (out != NULL)
		fwrite(data, size, 1, out);
}

static int file_pos()
{
	int ret = 0;
	
	if (out != NULL)
		ret = ftell(out);
		
	return ret;
}

static void close_file()
{
	if (out != NULL)
		fclose(out);
}

static FILE *mtl;
static char mtl_name[256];
static int mtl_cnt = 0;

static void create_mtl(const char *name)
{
	if (mtl != NULL)
		fclose(mtl);
	
	mtl_cnt = 0;

	sprintf(mtl_name, "%s.MTL", name);
	
	mtl = fopen(mtl_name, "w+b");
}

static void add_tex_to_mtl(const char *name, const char *obj)
{
	char tex_name[256];
	char temp[512];
	int i;
#ifdef SINGLE_TEX
	sprintf(tex_name, "%s.TGA", name);
#else
	sprintf(tex_name, "%s_%s.TGA", obj, name);
#endif

	if (mtl_cnt == 0) {
#ifdef SINGLE_TEX
		sprintf(temp, "newmtl %s_TEX \n \t map_Kd %s\n", name, tex_name);
#else
		sprintf(temp, "newmtl %s_%s_TEX \n \t map_Kd %s\n", obj, name, tex_name);
#endif
		fwrite(temp, strlen(temp), 1, mtl);
		mtl_cnt++;
	} else {
		fseek(mtl, 0, SEEK_SET);
		for (i = 0; i < mtl_cnt * 2; i++) {
			fgets(temp, 512, mtl);
			
			if (strncmp(temp + 7, name, strlen(name)) == 0) {
				break;
			}
		}

		if (i == mtl_cnt * 2) {
#ifdef SINGLE_TEX
			sprintf(temp, "newmtl %s_TEX \n \t map_Kd %s\n", name, tex_name);
#else
			sprintf(temp, "newmtl %s_%s_TEX \n \t map_Kd %s\n", obj, name, tex_name);
#endif
			fseek(mtl, 0, SEEK_END);
			fwrite(temp, strlen(temp), 1, mtl);
			mtl_cnt++;
		}
	}
}

static void close_mtl()
{
	if (mtl != NULL) {
		fclose(mtl);
		mtl = NULL;
	}
}


