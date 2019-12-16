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

int search_file(const char *path)
{
	int i = 0;
	struct sfs_inode temp_inode;
	disk_read(&temp_inode, sd_cwd.sfd_ino);
	struct sfs_dir temp_dir[SFS_DENTRYPERBLOCK];

	while(temp_inode.sfi_direct[i] != 0)
	{
		// block access
		disk_read(temp_dir, temp_inode.sfi_direct[i]);

		// 해당 block의 direct ptr array 접근하여 하위 폴더 및 파일명 탐색
		int j;
		for(j = 0; j < SFS_DENTRYPERBLOCK; j++)
		{
			// 찾은 경우 : return (inode #)
			if(!strcmp(temp_dir[j].sfd_name, path))
				return temp_dir[j].sfd_ino;
		}
		i++;
	}

	// 못찾은 경우 : return 0;
	return 0;
}

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
		int found_inode_idx = search_file(path);

		if(found_inode_idx)
		{
			struct sfs_inode found_inode;
			disk_read(&found_inode, found_inode_idx);

			// 파일인 경우 : [error] Not a directory
			if(found_inode.sfi_type == SFS_TYPE_FILE)
				error_message("cd", path, -2);

			// 폴더인 경우
			else
				sd_cwd.sfd_ino = found_inode_idx;
		}

		else
			error_message("cd", path, -1);
	}
}

void sfs_ls(const char* path)
{
	struct sfs_inode temp_inode;
	disk_read(&temp_inode, sd_cwd.sfd_ino);

	//BIT_CHECK(temp_bit,100);
	//printf("%d\n", temp_inode.sfi_size);
	//printf("비트맵 사이즈 in bit : %d\n", SFS_BITMAPSIZE(spb.sp_nblocks));
	//printf("비트맵 사이즈 in blk : %d\n", SFS_BITBLOCKS(spb.sp_nblocks));

	//printf("%d\n", SFS_DENTRYPERBLOCK); = 8
	
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

					// path가 dir인 경우
					if(found_inode.sfi_type == SFS_TYPE_DIR)
					{
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

					// path가 file인 경우
					else
					{
						printf("%s", temp_dir[i].sfd_name);						
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
	struct sfs_inode parent_inode;
	disk_read(&parent_inode, sd_cwd.sfd_ino);

	//for consistency
	assert( parent_inode.sfi_type == SFS_TYPE_DIR );

	// bitmap blk : 2 ~ 2+SFS_BITBLOCKS-1 까지.
	// 위 idx를 가지는 blk을 앞에서부터 bit 탐색해서 2개의 0에 해당하는 idx를 각각 새로운 inode, dir block idx로 한다.
	char temp_bit[SFS_BLOCKSIZE];
	int num_bitmap = SFS_BITMAPSIZE(spb.sp_nblocks);
	num_bitmap = SFS_BITBLOCKS(spb.sp_nblocks);

	int free_idx[2] = {0,0};


	int count = 0;
	int i;
	for(i = 0; i < num_bitmap && count < spb.sp_nblocks && !free_idx[1]; i++)
	{
		disk_read(temp_bit, 2 + i);
		int j;
		for(j = 0; j < 512 && count < spb.sp_nblocks && !free_idx[1]; j++)
		{
			int k;
			for(k = 0; k < 8 && count < spb.sp_nblocks && !free_idx[1]; k++)
			{
				if(!BIT_CHECK(temp_bit[j], k))
				{
					if(!free_idx[0])
					{
						free_idx[0] = count;
					}
					
					else if(!free_idx[1])
					{
						free_idx[1] = count;
					}
					//printf("%d  ", count);
				}
				count++;
			}
		}
	}

	// Disk Block이 꽉 찬 경우 : [error] No block available
	if(!free_idx[0])
		error_message("mkdir", org_path, -4);

	// Direct Entry가 꽉 찬 경우	: [error] Directory full
	else if(parent_inode.sfi_size/sizeof(struct sfs_dir) == SFS_DENTRYPERBLOCK * SFS_NDIRECT)
		error_message("mkdir", org_path, -3);

	else
	{
		// 이미 해당 path가 존재하는 경우 : [error] Already exists
		if(search_file(org_path))
			error_message("mkdir", org_path, -6);

		else
		{
			// 부모 노드의 dir entry에 추가해주기
			// block 추가할 각 idx 계산하기
			printf("size : %d\n", parent_inode.sfi_size);
			int inode_direct_idx = parent_inode.sfi_size / 512;
			int direct_entry_idx = (parent_inode.sfi_size - inode_direct_idx * 512) / 64;
			
			/*
			printf("inode ptr index : %d\n", inode_direct_idx);
			printf("dir index : %d\n", direct_entry_idx);

			struct sfs_dir test_dir[SFS_DENTRYPERBLOCK];
			disk_read(test_dir, parent_inode.sfi_direct[0]);
			int q;
			printf("-----------------\n");
			for(q = 0; q < 8; q++)
			{
				printf("%d\n", test_dir[q].sfd_ino);
			}
			printf("-----------------\n");
			*/

			struct sfs_dir new_dir[SFS_DENTRYPERBLOCK];	// 새로운 dir entry 할당할 변수
			disk_read(new_dir, parent_inode.sfi_direct[inode_direct_idx]);
			new_dir[direct_entry_idx].sfd_ino = free_idx[0];	// inode 할당
			strcpy(new_dir[direct_entry_idx].sfd_name, org_path);	// dir 이름 할당
			disk_write(new_dir, parent_inode.sfi_direct[inode_direct_idx]);

			/*
			struct sfs_dir test2_dir[SFS_DENTRYPERBLOCK];
			disk_read(test2_dir, parent_inode.sfi_direct[inode_direct_idx]);
			printf("-----------------\n");
			for(q = 0; q < 8; q++)
			{
				printf("%d\n", test2_dir[q].sfd_ino);
			}
			printf("-----------------\n");
			*/

			// 부모 노드의 sfi_size 수정
			parent_inode.sfi_size += sizeof(struct sfs_dir);
			disk_write(&parent_inode, sd_cwd.sfd_ino);

			// 빈 block의 idx로 inode 할당 및 초기화
			struct sfs_inode new_inode;	// 새로운 inode block 할당할 변수
		
			bzero(&new_inode, SFS_BLOCKSIZE);
			new_inode.sfi_size += 2 * sizeof(struct sfs_dir);
			new_inode.sfi_type = SFS_TYPE_DIR;
			new_inode.sfi_direct[0] = free_idx[1];	// dir block idx 할당

			disk_write(&new_inode, free_idx[0]);	

			// 빈 block의 idx로 dir block 할당 및 초기화
			struct sfs_dir new_dir_block[SFS_DENTRYPERBLOCK];	// 새로운 dir 블록 할당할 변수
			disk_read(new_dir_block, new_inode.sfi_direct[0]);
			new_dir_block[0].sfd_ino = free_idx[0];	// ? : 자기 inode idx 
			strncpy( new_dir_block[0].sfd_name, ".", SFS_NAMELEN );
			new_dir_block[1].sfd_ino = sd_cwd.sfd_ino;	// .. : 부모 inode idx
			strncpy( new_dir_block[1].sfd_name, "..", SFS_NAMELEN );


			int i;
			for(i = 2; i < SFS_DENTRYPERBLOCK; i++)
			{
				new_dir_block[i].sfd_ino = SFS_NOINO;
			}
			disk_write(&new_dir_block, new_inode.sfi_direct[0]);

			// 할당 받은 나머지 dir block entry 모두 SFS_NOINO로 초기화
			for(i = 1; i < SFS_NDIRECT; i++)
			{
				struct sfs_dir temp_block[SFS_DENTRYPERBLOCK];
				disk_read(temp_block, new_inode.sfi_direct[i]);

				int j;
				for(j = 1; j < SFS_DENTRYPERBLOCK; j++)
					temp_block[j].sfd_ino = SFS_NOINO;
				disk_write(temp_block, new_inode.sfi_direct[i]);
			}
			

			// 할당한 bitmap의 bit 정보 1로 바꿔주기
			int count = 0;
			free_idx[0] = 0;
			free_idx[1] = 0;
			for(i = 0; i < num_bitmap && count < spb.sp_nblocks && !free_idx[1]; i++)
			{
				disk_read(temp_bit, 2 + i);
				int j;
				for(j = 0; j < 512 && count < spb.sp_nblocks && !free_idx[1]; j++)
				{
					int k;
					for(k = 0; k < 8 && count < spb.sp_nblocks && !free_idx[1]; k++)
					{
						if(!BIT_CHECK(temp_bit[j], k))
						{
							if(!free_idx[0])
							{
								free_idx[0] = count;
								BIT_SET(temp_bit[j], k);
								disk_write(temp_bit, 2 + i);
							}
							
							else if(!free_idx[1])
							{
								free_idx[1] = count;
								BIT_SET(temp_bit[j], k);
								disk_write(temp_bit, 2 + i);
							}
						}
						count++;
					}
				}
			}
		}
	}
	printf("Not Implemented\n");
}

void sfs_rmdir(const char* org_path) 
{
	printf("Not Implemented\n");
}

void sfs_mv(const char* src_name, const char* dst_name) 
{
	int found_inode_idx = search_file(src_name);

	// 존재하지 않는 파일명을 바꾸려는 경우 : [error] No such file or directory
	if(!found_inode_idx)
		error_message("mv", src_name, -1);

	// 이미 존재하는 파일명으로 바꾸려는 경우 : [error] Already exists
	else if(search_file(dst_name))
		error_message("mv", dst_name, -6);

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
				// 찾은 경우 이름 변경
				if(!strcmp(temp_dir[j].sfd_name, src_name))
				{
					strcpy(temp_dir[j].sfd_name, dst_name);
					disk_write(temp_dir, temp_inode.sfi_direct[i]);

					found_token = 1;
					break;
				}
			}
			i++;
		}	
	}
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
