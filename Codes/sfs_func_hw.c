//
// Simple FIle System
// Student Name :
// Student Number :
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* optional */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
/***********/

#include "sfs_types.h"
#include "sfs_func.h"
#include "sfs_disk.h"
#include "sfs.h"

void dump_directory();

/* BIT operation Macros */
/* a=target variable, b=bit number to act upon 0-n */
#define BIT_SET(a,b) ((a) |= (1<<(b)))
#define BIT_CLEAR(a,b) ((a) &= ~(1<<(b)))
#define BIT_FLIP(a,b) ((a) ^= (1<<(b)))
#define BIT_CHECK(a,b) ((a) & (1<<(b)))

static struct sfs_super spb;	// superblock
static struct sfs_dir sd_cwd = { SFS_NOINO }; // current working directory

void error_message(const char *message, const char *path, int error_code) {
	switch (error_code) {
	case -1:
		printf("%s: %s: No such file or directory\n",message, path); return;
	case -2:
		printf("%s: %s: Not a directory\n",message, path); return;
	case -3:
		printf("%s: %s: Directory full\n",message, path); return;
	case -4:
		printf("%s: %s: No block available\n",message, path); return;
	case -5:
		printf("%s: %s: Not a directory\n",message, path); return;
	case -6:
		printf("%s: %s: Already exists\n",message, path); return;
	case -7:
		printf("%s: %s: Directory not empty\n",message, path); return;
	case -8:
		printf("%s: %s: Invalid argument\n",message, path); return;
	case -9:
		printf("%s: %s: Is a directory\n",message, path); return;
	case -10:
		printf("%s: %s: Is not a file\n",message, path); return;
	default:
		printf("unknown error code\n");
		return;
	}
}

void sfs_mount(const char* path)
{
	if( sd_cwd.sfd_ino !=  SFS_NOINO )
	{
		//umount
		disk_close();
		printf("%s, unmounted\n", spb.sp_volname);
		bzero(&spb, sizeof(struct sfs_super));
		sd_cwd.sfd_ino = SFS_NOINO;
	}

	printf("Disk image: %s\n", path);

	disk_open(path);
	disk_read( &spb, SFS_SB_LOCATION );

	printf("Superblock magic: %x\n", spb.sp_magic);

	assert( spb.sp_magic == SFS_MAGIC );
	
	printf("Number of blocks: %d\n", spb.sp_nblocks);
	printf("Volume name: %s\n", spb.sp_volname);
	printf("%s, mounted\n", spb.sp_volname);
	
	sd_cwd.sfd_ino = 1;		//init at root
	sd_cwd.sfd_name[0] = '/';
	sd_cwd.sfd_name[1] = '\0';
}

void sfs_umount() {

	if( sd_cwd.sfd_ino !=  SFS_NOINO )
	{
		//umount
		disk_close();
		printf("%s, unmounted\n", spb.sp_volname);
		bzero(&spb, sizeof(struct sfs_super));
		sd_cwd.sfd_ino = SFS_NOINO;
	}
}

void sfs_touch(const char* path)
{
	//skeleton implementation

	struct sfs_inode si;
	disk_read( &si, sd_cwd.sfd_ino );

	//for consistency
	assert( si.sfi_type == SFS_TYPE_DIR );

	//we assume that cwd is the root directory and root directory is empty which has . and .. only
	//unused DISK2.img satisfy these assumption
	//for new directory entry(for new file), we use cwd.sfi_direct[0] and offset 2
	//becasue cwd.sfi_directory[0] is already allocated, by .(offset 0) and ..(offset 1)
	//for new inode, we use block 6 
	// block 0: superblock,	block 1:root, 	block 2:bitmap 
	// block 3:bitmap,  	block 4:bitmap 	block 5:root.sfi_direct[0] 	block 6:unused
	//
	//if used DISK2.img is used, result is not defined
	
	//buffer for disk read
	struct sfs_dir sd[SFS_DENTRYPERBLOCK];

	//block access
	disk_read( sd, si.sfi_direct[0] );

	//allocate new block
	int newbie_ino = 6;

	sd[2].sfd_ino = newbie_ino;
	strncpy( sd[2].sfd_name, path, SFS_NAMELEN );

	disk_write( sd, si.sfi_direct[0] );

	si.sfi_size += sizeof(struct sfs_dir);
	disk_write( &si, sd_cwd.sfd_ino );

	struct sfs_inode newbie;

	bzero(&newbie,SFS_BLOCKSIZE); // initalize sfi_direct[] and sfi_indirect
	newbie.sfi_size = 0;
	newbie.sfi_type = SFS_TYPE_FILE;

	disk_write( &newbie, newbie_ino );
}

void sfs_cd(const char* path)
{
	// cd : Root dir로 이동하는 경우
	if(path == NULL)
	{
		struct sfs_inode temp_inode;
		disk_read(&temp_inode, SFS_ROOT_LOCATION);

		struct sfs_dir temp_dir[SFS_DENTRYPERBLOCK];
		disk_read(temp_dir, temp_inode.sfi_direct[0]);

		sd_cwd.sfd_ino = temp_dir[0].sfd_ino;
	}

	// cd [path] : path로 이동하는 경우
	else
	{
		int i = 0;
		int found_token = 0;
		struct sfs_inode temp_inode;
		disk_read(&temp_inode, sd_cwd.sfd_ino);
		struct sfs_dir temp_dir[SFS_DENTRYPERBLOCK];

		while(temp_inode.sfi_direct[i] != 0 && !found_token)
		{
			// block access
			disk_read(temp_dir, temp_inode.sfi_direct[i]);

			// 해당 block의 direct ptr array 접근하여 하위 폴더 및 파일명 탐색
			int j;
			for(j = 0; j < SFS_DENTRYPERBLOCK; j++)
			{
				struct sfs_inode search_inode;
				disk_read(&search_inode, temp_dir[j].sfd_ino);

				// path와 같은 이름의 폴더 혹은 파일을 찾은 경우
				if(!strcmp(temp_dir[j].sfd_name, path))
				{
					found_token = 1;

					struct sfs_inode found_inode;
					disk_read(&found_inode, temp_dir[j].sfd_ino);

					// 파일인 경우 : [error] Not a directory
					if(found_inode.sfi_type == SFS_TYPE_FILE)
						error_message("cd", path, -2);

					// 폴더인 경우
					else
						sd_cwd.sfd_ino = temp_dir[j].sfd_ino;
				}
			}
			i++;
		}

		// path와 같은 이름 못찾은 경우 : [error] No such file or directory
		if(!found_token)
			error_message("cd", path, -1);
	}
}

void sfs_ls(const char* path)
{
	struct sfs_inode temp_inode;
	disk_read(&temp_inode, sd_cwd.sfd_ino);
	
	struct sfs_dir temp_dir[SFS_DENTRYPERBLOCK];

	// ls : 현재 dir의 하위 폴더 및 파일 출력.
	if(path == NULL)
	{
		// inode의 sfi_direct배열 출력
		printf("**************************************************************************\n");
		int j = 0;
		while(temp_inode.sfi_direct[j] != 0)
		{
			// block access
			disk_read(temp_dir, temp_inode.sfi_direct[j]);

			// 해당 block의 direct ptr array 접근하여 하위 폴더 및 파일명 출력
			int i;
			for(i = 0; i < SFS_DENTRYPERBLOCK; i++)
			{
				struct sfs_inode loop_inode;
				disk_read(&loop_inode, temp_dir[i].sfd_ino);
				if (loop_inode.sfi_type == SFS_TYPE_DIR)
				{
					strcat(temp_dir[i].sfd_name, "/");
					printf("%-8s", temp_dir[i].sfd_name);
				}
				else if (loop_inode.sfi_type == SFS_TYPE_FILE)
					printf("%-8s", temp_dir[i].sfd_name);

				else
					break;

				if(strlen(temp_dir[i].sfd_name) >= 8)
					printf("     ");
			}
			j++;
		}
		printf("\n");
		printf("**************************************************************************\n");
	}

	// ls [path] : 경로를 사용한 경우로 경로의 하위 폴더 및 파일 출력.
	else
	{
		printf("**************************************************************************\n");
		int j = 0;
		int found_token = 0;
		while(temp_inode.sfi_direct[j] != 0 && !found_token)
		{
			// block access
			disk_read(temp_dir, temp_inode.sfi_direct[j]);

			// 해당 block의 direct ptr array 접근하여 하위 폴더 및 파일명 출력
			int i;
			for(i = 0; i < SFS_DENTRYPERBLOCK; i++)
			{
				struct sfs_inode search_inode;
				disk_read(&search_inode, temp_dir[i].sfd_ino);

				// path와 같은 이름의 dir 혹은 file을 찾은 경우
				if(!strcmp(temp_dir[i].sfd_name, path))
				{
					found_token = 1;
					
					struct sfs_inode found_inode;
					disk_read(&found_inode, temp_dir[i].sfd_ino);

					struct sfs_dir found_dir[SFS_DENTRYPERBLOCK];

					int k = 0;
					while(found_inode.sfi_direct[k] != 0)
					{
						disk_read(found_dir, found_inode.sfi_direct[k]);

						int l;
						for(l = 0; l < SFS_DENTRYPERBLOCK; l++)
						{
							struct sfs_inode loop_inode;
							disk_read(&loop_inode, found_dir[l].sfd_ino);
							if (loop_inode.sfi_type == SFS_TYPE_DIR)
							{
								strcat(found_dir[l].sfd_name, "/");
								printf("%-8s", found_dir[l].sfd_name);
							}
							else if (loop_inode.sfi_type == SFS_TYPE_FILE)
								printf("%-8s", found_dir[l].sfd_name);

							else
								break;

							if(strlen(found_dir[l].sfd_name) >= 8)
								printf("     ");
						}
						k++;
					}
				}
			}
			j++;
		}

		// path와 같은 이름 못찾은 경우 : [error] No such file or directory
		if(!found_token)
			error_message("ls", path, -1);

		else
			printf("\n");
		printf("**************************************************************************\n");		
	}
}

void sfs_mkdir(const char* org_path) 
{
	printf("Not Implemented\n");
}

void sfs_rmdir(const char* org_path) 
{
	printf("Not Implemented\n");
}

void sfs_mv(const char* src_name, const char* dst_name) 
{
	printf("Not Implemented\n");
}

void sfs_rm(const char* path) 
{
	printf("Not Implemented\n");
}

void sfs_cpin(const char* local_path, const char* path) 
{
	printf("Not Implemented\n");
}

void sfs_cpout(const char* local_path, const char* path) 
{
	printf("Not Implemented\n");
}

void dump_inode(struct sfs_inode inode) {
	int i;
	struct sfs_dir dir_entry[SFS_DENTRYPERBLOCK];

	printf("size %d type %d direct ", inode.sfi_size, inode.sfi_type);
	for(i=0; i < SFS_NDIRECT; i++) {
		printf(" %d ", inode.sfi_direct[i]);
	}
	printf(" indirect %d",inode.sfi_indirect);
	printf("\n");

	if (inode.sfi_type == SFS_TYPE_DIR) {
		for(i=0; i < SFS_NDIRECT; i++) {
			if (inode.sfi_direct[i] == 0) break;
			disk_read(dir_entry, inode.sfi_direct[i]);
			dump_directory(dir_entry);
		}
	}

}

void dump_directory(struct sfs_dir dir_entry[]) {
	int i;
	struct sfs_inode inode;
	for(i=0; i < SFS_DENTRYPERBLOCK;i++) {
		printf("%d %s\n",dir_entry[i].sfd_ino, dir_entry[i].sfd_name);
		disk_read(&inode,dir_entry[i].sfd_ino);
		if (inode.sfi_type == SFS_TYPE_FILE) {
			printf("\t");
			dump_inode(inode);
		}
	}
}

void sfs_dump() {
	// dump the current directory structure
	struct sfs_inode c_inode;

	disk_read(&c_inode, sd_cwd.sfd_ino);
	printf("cwd inode %d name %s\n",sd_cwd.sfd_ino,sd_cwd.sfd_name);
	dump_inode(c_inode);
	printf("\n");

}
