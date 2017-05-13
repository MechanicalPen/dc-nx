
#include "shim.h"
#include <string.h>
#include <zlib.h>
#include "cave_icon.h"

#define MAX_VMU_SIZE (128 * 1024)

int vm_file;
char vmu_title[32];

static bool vmu_avail[4*2];

bool vmfile_search(const char *fname, int *vm)
{
    struct vmsinfo info;
    struct superblock super;
    struct vms_file file;
    static bool inited = false;

    if(!inited) {
	
		memset(vmu_avail, false, sizeof(vmu_avail));
	
		int mask = getimask();
		setimask(15);
		struct mapledev *pad=locked_get_pads();
		for (int i=0; i<4; ++i, ++pad) {
			if (pad[i].present & (1<<0)) {
				vmu_avail[i] = true;
			}
			if (pad[i].present & (1<<1)) {
				vmu_avail[i+4] = true;
			}
		}
		setimask(mask);
	
		inited = true;
    }
	
    for (int x=0; x<4; x++) {
		for (int y=0; y<2; y++) {
	    
			if (vmu_avail[x+y*4]) {
		
				int res = x*6 + y + 1;
		
				if (vmsfs_check_unit(res, 0, &info))
					if (vmsfs_get_superblock(&info, &super))
						if (vmsfs_open_file(&super, fname, &file)) {
#ifndef NOSERIAL	
							printf("%s Found on %c%d\n", fname, 'A'+res/6,res%6);	
#endif	
							*vm = res;
							return true;
						}
			}
		}
    }
	
    return false;
}

bool vmfile_exists(const char *fname)
{
    return vmfile_search(fname, &vm_file);
}

bool save_to_vmu(int unit, const char *filename, const char *buf, int buf_len)
{
    struct vms_file_header header;
    struct vmsinfo info;
    struct superblock super;
    struct vms_file file;
    int free_cnt;
    time_t long_time;
    struct tm *now_time;
    struct timestamp stamp;
    unsigned char compressed_buf[MAX_VMU_SIZE];
    int compressed_len;
    
    memset(compressed_buf, 0, sizeof(compressed_buf));
    compressed_len = buf_len + 512;
    
    if (compress((Bytef*)compressed_buf, (uLongf*)&compressed_len,
		 (Bytef*)buf, buf_len) != Z_OK) {
	
	return false;
    }
    
    if (!vmsfs_check_unit(unit, 0, &info)) {
	return false;
    }
    if (!vmsfs_get_superblock(&info, &super)) {
	return false;
    }
    free_cnt = vmsfs_count_free(&super);
    
    if (vmsfs_open_file(&super, filename, &file))
	free_cnt += file.blks;
    
    if (((128+512+compressed_len+511)/512) > free_cnt) {
	return false;
    }
    
    memset(&header, 0, sizeof(header));
    strncpy(header.shortdesc, "NXEngine SAVE",sizeof(header.shortdesc));
    strncpy(header.longdesc, "NXEngine v1.0.0.6", sizeof(header.longdesc));
    strncpy(header.id, "NXEngine", sizeof(header.id));
    memcpy(header.palette, cave_icon, sizeof(header.palette));
    header.numicons = 1;
    
    time(&long_time);
    now_time = localtime(&long_time);
    stamp.year = now_time->tm_year + 1900;
    stamp.month = now_time->tm_mon + 1;
    stamp.wkday = now_time->tm_wday;
    stamp.day = now_time->tm_mday;
    stamp.hour = now_time->tm_hour;
    stamp.minute = now_time->tm_min;
    stamp.second = now_time->tm_sec;
    
    if (!vmsfs_create_file(&super, filename, &header, cave_icon+sizeof(header.palette), NULL, compressed_buf, compressed_len, &stamp)) {
	
#ifndef NOSERIAL
	fprintf(stderr,"%s",vmsfs_describe_error());
#endif
	return false;
    }

    return true;
}

bool load_from_vmu(int unit, const char *filename, char *buf, int *buf_len)
{
    struct vmsinfo info;
    struct superblock super;
    struct vms_file file;
    unsigned char compressed_buf[MAX_VMU_SIZE];
    unsigned int compressed_len;
    
    if (!vmsfs_check_unit(unit, 0, &info)) {
		return false;
    }
    if (!vmsfs_get_superblock(&info, &super)) {
		return false;
    }
    if (!vmsfs_open_file(&super, filename, &file)) {
		return false;
    }
    
    memset(compressed_buf, 0, sizeof(compressed_buf));
    compressed_len = file.size;
    
    if (!vmsfs_read_file(&file, (Bytef*)compressed_buf, compressed_len)) {
	
		return false;
    }
    
    if (!*buf_len)
	*buf_len = MAX_VMU_SIZE;
    
    if (uncompress((Bytef*)buf, (uLongf*)buf_len,(const Bytef*)compressed_buf, (uLong)compressed_len) != Z_OK) {
	
	return false;
    }

    return true;
}


bool delete_file_vmu(int unit, const char *filename)
{
    struct vmsinfo info;
    struct superblock super;
    
    if (!vmsfs_check_unit(unit, 0, &info)) {
		return false;
    }
    if (!vmsfs_get_superblock(&info, &super)) {
		return false;
    }
    if (!vmsfs_delete_file(&super, filename)) {
		return false;
    }

#ifndef NOSERIAL
    printf("delete %s on %d", filename,unit);
#endif
	
    return true;
}

bool rename_vmu_file(const char *oldpath, const char *newpath)
{
    int vm;
    struct dir_iterator iter;
    struct dir_entry d;
    
    struct vmsinfo info;
    struct superblock super;
    
    if (!vmfile_search(oldpath, &vm))
		return false;
    
    if (!vmsfs_check_unit(vm, 0, &info)) {
		return false;
    }

    if (!vmsfs_get_superblock(&info, &super)) {
		return false;
    }
    
    vmsfs_open_dir(&super, &iter);
    
	if (!vmsfs_next_named_dir_entry(&iter, &d, oldpath)) {
		if (d.entry[0]) {
			char *dst = (char *)d.entry+4;
			memset(dst, 0, 12);
			strncpy(dst, newpath, 12);

#ifndef NOSERIAL
			printf("Rename: %s -> %s on %c%d\n", oldpath, newpath, 'A'+ vm/6, vm%6);
#endif

			return true;
		}
	}

	return false;
}


int remove(const char *fname)
{
	return -1;
}
