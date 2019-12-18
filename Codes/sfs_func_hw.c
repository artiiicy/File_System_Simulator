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

	//there is already exists file which name is path : [error] 
	if(search_file(path))
	{
		error_message("touch", path, -6);
		return;
	}

	//we assume that cwd is the root directory and root directory is empty which has . and .. only
	//unused DISK2.img satisfy these assumption
	//for new directory entry(for new file), we use cwd.sfi_direct[0] and offset 2
	//becasue cwd.sfi_directory[0] is already allocated, by .(offset 0) and ..(offset 1)
	//for new inode, we use block 6 
	// block 0: superblock,	block 1:root, 	block 2:bitmap 
	// block 3:bitmap,  	block 4:bitmap 	block 5:root.sfi_direct[0] 	block 6:unused
	//
	//if used DISK2.img is used, result is not defined
	
	char temp_bit[SFS_BLOCKSIZE];
	int num_bitmap = SFS_BITMAPSIZE(spb.sp_nblocks);
	num_bitmap = SFS_BITBLOCKS(spb.sp_nblocks);

	// find idx for new i-node block AND set bitmap
	int count = 0;
	int new_idx = 0;
	int i;
	for(i = 0; i < num_bitmap && count < spb.sp_nblocks && !new_idx; i++)
	{
		disk_read(temp_bit, 2 + i);
		int j;
		for(j = 0; j < 512 && count < spb.sp_nblocks && !new_idx; j++)
		{
			int k;
			for(k = 0; k < 8 && count < spb.sp_nblocks && !new_idx; k++)
			{
				if(!BIT_CHECK(temp_bit[j], k))
				{
					new_idx = count;
					BIT_SET(temp_bit[j], k);
					disk_write(temp_bit, 2 + i);
				}
				count++;
			}
		}
	}

	// Disk Block이 꽉 찬 경우 : [error] No block available
	if(!new_idx)
	{
		error_message("touch", path, -4);
		return;
	}

	// Direct Entry가 꽉 찬 경우	: [error] Directory full
	else if(si.sfi_size/sizeof(struct sfs_dir) == SFS_DENTRYPERBLOCK * SFS_NDIRECT)
	{
		error_message("touch", path, -3);

		// find idx for new i-node block AND set bitmap
		int count = 0;
		int i;
		for(i = 0; i < num_bitmap && count < spb.sp_nblocks; i++)
		{
			disk_read(temp_bit, 2 + i);
			int j;
			for(j = 0; j < 512 && count < spb.sp_nblocks; j++)
			{
				int k;
				for(k = 0; k < 8 && count < spb.sp_nblocks; k++)
				{
					if(count == new_idx)
					{
						BIT_CLEAR(temp_bit[j], k);
						disk_write(temp_bit, 2 + i);
						return;
					}
					count++;
				}
			}
		}
	}

	else
	{
		// block 추가할 각 idx 계산하기
		int inode_direct_idx = si.sfi_size / SFS_BLOCKSIZE;
		int direct_entry_idx = (si.sfi_size - inode_direct_idx * SFS_BLOCKSIZE) / sizeof(struct sfs_dir);

		// 새로운 dir ptr를 배정해야 하는 경우
		if(si.sfi_size % 512 == 0)
		{
			// bitmap에서 0인 block idx 찾아서 1로 만든다.
			int count = 0;
			int temp_idx = 0;
			int i;
			for(i = 0; i < num_bitmap && count < spb.sp_nblocks && !temp_idx; i++)
			{
				disk_read(temp_bit, 2 + i);
				int j;
				for(j = 0; j < 512 && count < spb.sp_nblocks && !temp_idx; j++)
				{
					int k;
					for(k = 0; k < 8 && count < spb.sp_nblocks && !temp_idx; k++)
					{
						if(!BIT_CHECK(temp_bit[j], k))
						{
							temp_idx = count;
							BIT_SET(temp_bit[j], k);
							disk_write(temp_bit, 2 + i);
						}
						count++;
					}
				}
			}

			// block 추가 할당이 안되는 경우 fail : [error] No block available
			if(!temp_idx)
			{
				error_message("touch", path, -4);

				int count = 0;
				int new_idx = 0;
				int i;
				for(i = 0; i < num_bitmap && count < spb.sp_nblocks && !new_idx; i++)
				{
					disk_read(temp_bit, 2 + i);
					int j;
					for(j = 0; j < 512 && count < spb.sp_nblocks && !new_idx; j++)
					{
						int k;
						for(k = 0; k < 8 && count < spb.sp_nblocks && !new_idx; k++)
						{
							if(count == new_idx)
							{
								BIT_CLEAR(temp_bit[j], k);
								disk_write(temp_bit, 2 + i);
								return;
							}
							count++;
						}
					}
				}
			}

			// 새로 할당한 dir inode에 쓰기
			si.sfi_direct[inode_direct_idx] = temp_idx;
			disk_write(&si, sd_cwd.sfd_ino);
		}

		//buffer for disk read
		struct sfs_dir sd[SFS_DENTRYPERBLOCK];

		//block access /////////////
		disk_read( sd, si.sfi_direct[inode_direct_idx] );

		//allocate new block
		int newbie_ino = new_idx;

		sd[direct_entry_idx].sfd_ino = newbie_ino;
		strncpy( sd[direct_entry_idx].sfd_name, path, SFS_NAMELEN );

		// 새로 dir ptr배정한 경우 나머지 0으로 초기화
		if(si.sfi_size % 512 == 0)
		{
			int s;
			for(s = 1; s < 8; s++)
			{
				bzero(&sd[direct_entry_idx + s], sizeof(struct sfs_dir));
			}
		}
		disk_write( sd, si.sfi_direct[inode_direct_idx] );

		si.sfi_size += sizeof(struct sfs_dir);
		disk_write( &si, sd_cwd.sfd_ino );

		struct sfs_inode newbie;

		bzero(&newbie,SFS_BLOCKSIZE); // initalize sfi_direct[] and sfi_indirect
		newbie.sfi_size = 0;
		newbie.sfi_type = SFS_TYPE_FILE;

		disk_write( &newbie, newbie_ino );
	}
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
		strncpy(sd_cwd.sfd_name, temp_dir[0].sfd_name, SFS_NAMELEN);
	}
	
	// cd [path] : path로 이동하는 경우
	else
	{
		int found_inode_idx = 0;
		int i = 0;
		struct sfs_inode temp_inode;
		disk_read(&temp_inode, sd_cwd.sfd_ino);
		struct sfs_dir temp_dir[SFS_DENTRYPERBLOCK];

		while(temp_inode.sfi_direct[i] != 0 && !found_inode_idx)
		{
			// block access
			disk_read(temp_dir, temp_inode.sfi_direct[i]);

			// 해당 block의 direct ptr array 접근하여 하위 폴더 및 파일명 탐색
			int j;
			for(j = 0; j < SFS_DENTRYPERBLOCK && !found_inode_idx; j++)
			{
				// 찾은 경우
				if(!strcmp(temp_dir[j].sfd_name, path))
				{
					found_inode_idx = temp_dir[j].sfd_ino;

					struct sfs_inode found_inode;
					disk_read(&found_inode, found_inode_idx);

					// 파일인 경우 : [error] Not a directory
					if(found_inode.sfi_type == SFS_TYPE_FILE)
						error_message("cd", path, -2);

					// 폴더인 경우
					else
					{
						sd_cwd.sfd_ino = found_inode_idx;
						strncpy(sd_cwd.sfd_name, temp_dir[j].sfd_name, SFS_NAMELEN);
					}
				}
			}
			i++;
		}

		// 못 찾은 경우 : [error] No such file or directory
		if(!found_inode_idx)
			error_message("cd", path, -1);
		/*
		
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
			{
				sd_cwd.sfd_ino = found_inode_idx;
				strncpy(sd_cwd.sfd_name, path, SFS_NAMELEN);
			}
		}

		else
			error_message("cd", path, -1);
		*/
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
		int j = 0;
		while(j < SFS_NDIRECT)
		{
			if(temp_inode.sfi_direct[j] != 0)
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
						continue;

					if(strlen(temp_dir[i].sfd_name) >= 8)
						printf("     ");
				}
			}
			j++;
		}
		printf("\n");
	}

	// ls [path] : 경로를 사용한 경우로 경로의 하위 폴더 및 파일 출력.
	else
	{
		int j = 0;
		int found_token = 0;
		//while(temp_inode.sfi_direct[j] != 0 && !found_token)

		while(j < SFS_NDIRECT)
		{
			if(temp_inode.sfi_direct[j] != 0 && !found_token)
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
					if(!strcmp(temp_dir[i].sfd_name, path) && temp_dir[i].sfd_ino)
					{
						found_token = 1;
						
						struct sfs_inode found_inode;
						disk_read(&found_inode, temp_dir[i].sfd_ino);

						// path가 dir인 경우
						if(found_inode.sfi_type == SFS_TYPE_DIR)
						{
							struct sfs_dir found_dir[SFS_DENTRYPERBLOCK];

							int k = 0;
							while(k < SFS_DENTRYPERBLOCK)
							{
								if(found_inode.sfi_direct[k] != 0)
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
											continue;

										if(strlen(found_dir[l].sfd_name) >= 8)
											printf("     ");
									}
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
			}
			
			j++;
		}

		// path와 같은 이름 못찾은 경우 : [error] No such file or directory
		if(!found_token)
			error_message("ls", path, -1);

		else
			printf("\n");
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
	if(!free_idx[0] || !free_idx[1])
	{
		error_message("mkdir", org_path, -4);
		return;
	}

	// Direct Entry가 꽉 찬 경우	: [error] Directory full
	else if(parent_inode.sfi_size/sizeof(struct sfs_dir) == SFS_DENTRYPERBLOCK * SFS_NDIRECT)
	{
		error_message("mkdir", org_path, -3);
		return;
	}

	else
	{
		// 이미 해당 path가 존재하는 경우 : [error] Already exists
		if(search_file(org_path))
		{
			error_message("mkdir", org_path, -6);
			return;
		}

		else
		{
			// 부모 노드의 dir entry에 추가해주기
			// block 추가할 각 idx 계산하기
			int inode_direct_idx = parent_inode.sfi_size / SFS_BLOCKSIZE;
			int direct_entry_idx = (parent_inode.sfi_size - inode_direct_idx * SFS_BLOCKSIZE) / sizeof(struct sfs_dir);
			
			
			
			/*
			printf("inode ptr index : %d\n", inode_direct_idx);
			printf("dir index : %d\n", direct_entry_idx);

			struct sfs_dir test_dir[SFS_DENTRYPERBLOCK];
			disk_read(test_dir, parent_inode.sfi_direct[inode_direct_idx]);
			int q;
			printf("-----------------\n");
			for(q = 0; q < 8; q++)
			{
				printf("%d\n", test_dir[q].sfd_ino);
			}
			printf("-----------------\n");
			*/

			// 새로운 dir ptr를 배정해야 하는 경우
			if(parent_inode.sfi_size % 512 == 0)
			{
				char temp_bit[SFS_BLOCKSIZE];
				int num_bitmap = SFS_BITMAPSIZE(spb.sp_nblocks);
				num_bitmap = SFS_BITBLOCKS(spb.sp_nblocks);

				// bitmap에서 0이 3개 이상인지 확인한다.
				int temp_idx[3] = {0, 0, 0};
				int last_temp = 0;

				int count = 0;
				int i;
				for(i = 0; i < num_bitmap && count < spb.sp_nblocks && last_temp != 3; i++)
				{
					disk_read(temp_bit, 2 + i);
					int j;
					for(j = 0; j < 512 && count < spb.sp_nblocks && last_temp != 3; j++)
					{
						int k;
						for(k = 0; k < 8 && count < spb.sp_nblocks && last_temp != 3; k++)
						{
							if(!BIT_CHECK(temp_bit[j], k))
							{
								if(!temp_idx[0])
								{
									temp_idx[0] = count;
									last_temp++;
								}

								else if(!temp_idx[1])
								{
									temp_idx[1] = count;
									last_temp++;
								}

								else if(!temp_idx[2])
								{
									temp_idx[2] = count;
									last_temp++;
								}
							}
							count++;
						}
					}
				}

				// block이 꽉 찬 경우
				if(!temp_idx[0] || !temp_idx[1] ||!temp_idx[2])
				{
					error_message("mkdir", org_path, -4);
					return;
				}

				else
				{
					// 할당할 block의 bitmap의 bit 정보 1로 바꿔주기
					int count = 0;
					int last_temp = 0;
					for(i = 0; i < num_bitmap && count < spb.sp_nblocks && last_temp != 3; i++)
					{
						disk_read(temp_bit, 2 + i);
						int j;
						for(j = 0; j < 512 && count < spb.sp_nblocks && last_temp != 3; j++)
						{
							int k;
							for(k = 0; k < 8 && count < spb.sp_nblocks && last_temp != 3; k++)
							{
								if(count == temp_idx[0])
								{
									BIT_SET(temp_bit[j], k);
									disk_write(temp_bit, 2 + i);
									last_temp++;
								}

								else if(count == temp_idx[1])
								{
									BIT_SET(temp_bit[j], k);
									disk_write(temp_bit, 2 + i);
									last_temp++;
								}

								else if(count == temp_idx[2])
								{
									BIT_SET(temp_bit[j], k);
									disk_write(temp_bit, 2 + i);
									last_temp++;
								}
								count++;
							}
						}
					}
				}
				
				free_idx[0] = temp_idx[1];
				free_idx[1] = temp_idx[2];

				// 찾은 block idx를 새로운 dir ptr block으로 할당
				parent_inode.sfi_direct[inode_direct_idx] = temp_idx[0];
				disk_write(&parent_inode, sd_cwd.sfd_ino);
			}

			else if(parent_inode.sfi_size % 512 != 0)
			{
				char temp_bit[SFS_BLOCKSIZE];
				int num_bitmap = SFS_BITMAPSIZE(spb.sp_nblocks);
				num_bitmap = SFS_BITBLOCKS(spb.sp_nblocks);

				// 할당한 bitmap의 bit 정보 1로 바꿔주기
				int count = 0;
				int last_temp = 0;
				int i;
				for(i = 0; i < num_bitmap && count < spb.sp_nblocks && last_temp != 2; i++)
				{
					disk_read(temp_bit, 2 + i);
					int j;
					for(j = 0; j < 512 && count < spb.sp_nblocks && last_temp != 2; j++)
					{
						int k;
						for(k = 0; k < 8 && count < spb.sp_nblocks && last_temp != 2; k++)
						{
							if(count == free_idx[0])
							{
								BIT_SET(temp_bit[j], k);
								disk_write(temp_bit, 2 + i);
								last_temp++;
							}

							else if(count == free_idx[1])
							{
								BIT_SET(temp_bit[j], k);
								disk_write(temp_bit, 2 + i);
								last_temp++;
							}
							count++;
						}
					}
				}
			}

			struct sfs_dir new_dir[SFS_DENTRYPERBLOCK];	// 새로운 dir entry 할당할 변수
			disk_read(new_dir, parent_inode.sfi_direct[inode_direct_idx]);
			new_dir[direct_entry_idx].sfd_ino = free_idx[0];	// inode 할당
			strncpy(new_dir[direct_entry_idx].sfd_name, org_path, SFS_NAMELEN);	// dir 이름 할당

			if(parent_inode.sfi_size % 512 == 0)
			{
				int s;
				for(s = 1; s < 8; s++)
				{
					bzero(&new_dir[direct_entry_idx + s], sizeof(struct sfs_dir));
					//new_dir[direct_entry_idx + s].sfd_ino = SFS_NOINO;
					//strcpy(new_dir[direct_entry_idx + s].sfd_name, '\0');
				}
			}
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
			new_dir_block[0].sfd_ino = free_idx[0];	// 자기 inode idx 
			strncpy( new_dir_block[0].sfd_name, ".", SFS_NAMELEN );
			new_dir_block[1].sfd_ino = sd_cwd.sfd_ino;	// .. : 부모 inode idx
			strncpy( new_dir_block[1].sfd_name, "..", SFS_NAMELEN );


			int i;
			for(i = 2; i < SFS_DENTRYPERBLOCK; i++)
			{
				bzero(&new_dir_block[i], sizeof(struct sfs_dir));
				//new_dir_block[i].sfd_ino = SFS_NOINO;
				//strcpy(new_dir[i].sfd_name, '\0');
			}
			disk_write(new_dir_block, new_inode.sfi_direct[0]);

			// 할당 받은 나머지 dir block entry 모두 SFS_NOINO로 초기화
			for(i = 1; i < SFS_NDIRECT; i++)
			{
				struct sfs_dir temp_block[SFS_DENTRYPERBLOCK];
				disk_read(temp_block, new_inode.sfi_direct[i]);

				int j;
				for(j = 1; j < SFS_DENTRYPERBLOCK; j++)
				{
					bzero(&temp_block[j], sizeof(struct sfs_dir));
					//temp_block[j].sfd_ino = SFS_NOINO;
					//strcpy(temp_block[j].sfd_name, '\0');
				}
				disk_write(temp_block, new_inode.sfi_direct[i]);
			}
		}
	}
}

void sfs_rmdir(const char* org_path) 
{
	int found_inode_idx = 0;
	int i = 0;
	struct sfs_inode parent_inode;
	disk_read(&parent_inode, sd_cwd.sfd_ino);
	struct sfs_dir parent_dir[SFS_DENTRYPERBLOCK];

	while(parent_inode.sfi_direct[i] != 0 && !found_inode_idx)
	{
		// block access
		disk_read(parent_dir, parent_inode.sfi_direct[i]);

		// 해당 block의 direct ptr array 접근하여 하위 폴더 및 파일명 탐색
		int j;
		for(j = 0; j < SFS_DENTRYPERBLOCK && !found_inode_idx; j++)
		{
			// 찾은 경우
			if(!strcmp(parent_dir[j].sfd_name, org_path))
			{
				found_inode_idx = parent_dir[j].sfd_ino;

				struct sfs_inode found_inode;
				disk_read(&found_inode, found_inode_idx);

				// 파일인 경우 : [error] Not a directory
				if(found_inode.sfi_type == SFS_TYPE_FILE)
					error_message("rmdir", org_path, -2);

				// 폴더인 경우
				else
				{
					struct sfs_inode found_inode;
					disk_read(&found_inode, parent_dir[j].sfd_ino);

					// 빈 디렉토리가 아닌 경우 : [error] Directory not empty
					if(found_inode.sfi_size != sizeof(struct sfs_dir) * 2)
					{
						error_message("rmdir", org_path, -7);
						return;
					}

					else
					{
						// 찾은 폴더의 dir block 초기화
						int found_dir_idx = found_inode.sfi_direct[0];

						struct sfs_dir found_dir[SFS_DENTRYPERBLOCK];
						disk_read(found_dir, found_dir_idx);

						bzero(found_dir, SFS_BLOCKSIZE);

						disk_write(found_dir, found_dir_idx);

						// 찾은 폴더의 inode block 초기화
						bzero(&found_inode, SFS_BLOCKSIZE);
						disk_write(&found_inode, found_inode_idx);
						
						// 부모 노드의 dir entry에서 삭제
						bzero(&parent_dir[j], sizeof(struct sfs_dir));
						disk_write(parent_dir, parent_inode.sfi_direct[i]);

						// 부모 노드의 size 재조정
						parent_inode.sfi_size -= sizeof(struct sfs_dir);
						disk_write(&parent_inode, sd_cwd.sfd_ino);


						// bitmap 수정
						char temp_bit[SFS_BLOCKSIZE];
						int num_bitmap = SFS_BITMAPSIZE(spb.sp_nblocks);
						int temp_token = 0;
						num_bitmap = SFS_BITBLOCKS(spb.sp_nblocks);	

						int count = 0;
						for(i = 0; i < num_bitmap && count < spb.sp_nblocks && temp_token != 2; i++)
						{
							disk_read(temp_bit, 2 + i);
							int j;
							for(j = 0; j < 512 && count < spb.sp_nblocks && temp_token != 2; j++)
							{
								int k;
								for(k = 0; k < 8 && count < spb.sp_nblocks && temp_token != 2; k++)
								{
									if(count == found_inode_idx)
									{
										BIT_CLEAR(temp_bit[j], k);
										disk_write(temp_bit, 2 + i);
										temp_token++;
									}

									else if(count == found_dir_idx)
									{
										BIT_CLEAR(temp_bit[j], k);
										disk_write(temp_bit, 2 + i);
										temp_token++;
									}

									count++;
								}
							}
						}

					}
				}
			}
		}
		i++;
	}

	// 못 찾은 경우 : [error] No such file or directory
	if(!found_inode_idx)
		error_message("rmdir", org_path, -1);

	printf("Not Implemented\n");
}

void sfs_mv(const char* src_name, const char* dst_name) // strcpy? strncpy??/ strncpy하면 diff 통과 못함
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
					//strncpy(temp_dir[j].sfd_name, dst_name, SFS_NAMELEN);
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
	int rm_idx = 0;
	int found_token = 0;
	int i = 0;
	struct sfs_inode temp_inode;
	disk_read(&temp_inode, sd_cwd.sfd_ino);
	struct sfs_dir temp_dir[SFS_DENTRYPERBLOCK];

	// search in direct ptr array
	while(i < SFS_NDIRECT && !found_token)
	{
		if(temp_inode.sfi_direct[i] != 0 && !found_token)
		{
			// block access
			disk_read(temp_dir, temp_inode.sfi_direct[i]);

			// 해당 block의 direct ptr array 접근하여 하위 폴더 및 파일명 탐색
			int j;
			for(j = 0; j < SFS_DENTRYPERBLOCK && !found_token; j++)
			{
				// 찾은 경우
				if(!strcmp(temp_dir[j].sfd_name, path) && temp_dir[j].sfd_ino)
				{
					struct sfs_inode is_if_file;
					disk_read(&is_if_file, temp_dir[j].sfd_ino);

					// file인 경우 dir entry에서 삭제
					if(is_if_file.sfi_type == SFS_TYPE_FILE)
					{
						found_token = 1;
						rm_idx = temp_dir[j].sfd_ino;
						//bzero(&temp_dir[j], sizeof(struct sfs_dir));
						temp_dir[j].sfd_ino = SFS_NOINO;

						disk_write(temp_dir, temp_inode.sfi_direct[i]);

						//부모노드 size 재조정
						temp_inode.sfi_size -= sizeof(struct sfs_dir);
						disk_write(&temp_inode, sd_cwd.sfd_ino);
					}

					// dir인 경우 : [error] Is a directory
					else
					{
						error_message("rm", path, -9);
						return;
					}
				}
			}
		}
		i++;
	}

	// search in indirect ptr array
	if(!found_token && temp_inode.sfi_indirect)
	{
		u_int32_t dir_ptr[SFS_DBPERIDB];

		disk_read(dir_ptr, temp_inode.sfi_indirect);

		int i;
		for(i = 0; i < SFS_DBPERIDB && !found_token; i++)
		{
			struct sfs_dir temp_dir[SFS_DENTRYPERBLOCK];
			disk_read(temp_dir, dir_ptr[i]);

			// 해당 block의 direct ptr array 접근하여 하위 폴더 및 파일명 탐색
			int j;
			for(j = 0; j < SFS_DENTRYPERBLOCK && !found_token; j++)
			{
				// 찾은 경우
				if(!strcmp(temp_dir[j].sfd_name, path) && temp_dir[j].sfd_ino)
				{
					struct sfs_inode is_if_file;
					disk_read(&is_if_file, temp_dir[j].sfd_ino);

					// file인 경우 dir entry에서 삭제
					if(is_if_file.sfi_type == SFS_TYPE_FILE)
					{
						found_token = 1;
						rm_idx = temp_dir[j].sfd_ino;
						//bzero(&temp_dir[j], sizeof(struct sfs_dir));
						temp_dir[j].sfd_ino = SFS_NOINO;
						disk_write(temp_dir, dir_ptr[i]);

						//부모노드 size 재조정
						temp_inode.sfi_size -= sizeof(struct sfs_dir);
						disk_write(&temp_inode, sd_cwd.sfd_ino);

						//indirect 초기화
						//bzero(&dir_ptr[i], sizeof(u_int32_t));
						dir_ptr[i] = 0;
						disk_write(dir_ptr, temp_inode.sfi_indirect);
					}

					// dir인 경우 : [error] Is a directory
					else
					{
						error_message("rm", path, -9);
						return;
					}
				}
			}
		}
	}

	if(found_token)
	{
		// inode 초기화
		struct sfs_inode rm_inode;
		disk_read(&rm_inode, rm_idx);

		// 삭제할 inode에 dir block이 할당되어 있다면 dir block 삭제
		if(rm_inode.sfi_size != 0)
		{
			/*
			int rm_dir_idx = rm_inode.sfi_direct[0];
			struct sfs_dir rm_dir[SFS_DENTRYPERBLOCK];
			disk_read(rm_dir, rm_dir_idx);
			//bzero(rm_dir, sizeof(struct sfs_dir));
			rm_dir[0].sfd_ino = SFS_NOINO;
			disk_write(rm_dir, rm_dir_idx);
			*/
			int s;
			for(s = 0; s < 15; s++)
			{
				
				int rm_dir_idx = rm_inode.sfi_direct[s];
				rm_inode.sfi_direct[s] = SFS_NOINO;

				if(rm_dir_idx)
				{
					// 삭제한 dir block에 해당하는 bitmap 해제
					char temp_bit[SFS_BLOCKSIZE];
					int num_bitmap = SFS_BITMAPSIZE(spb.sp_nblocks);
					num_bitmap = SFS_BITBLOCKS(spb.sp_nblocks);

					int temp_token = 0;
					int count = 0;
					for(i = 0; i < num_bitmap && count < spb.sp_nblocks && !temp_token; i++)
					{
						disk_read(temp_bit, 2 + i);
						int j;
						for(j = 0; j < 512 && count < spb.sp_nblocks && !temp_token; j++)
						{
							int k;
							for(k = 0; k < 8 && count < spb.sp_nblocks && !temp_token; k++)
							{
								if(count == rm_dir_idx)
								{
									BIT_CLEAR(temp_bit[j], k);
									temp_token = 1;
									disk_write(temp_bit, 2 + i);
								}

								count++;
							}
						}
					}
				}
			}
			if(rm_inode.sfi_indirect)
			{
				u_int32_t dir_ptr[SFS_DBPERIDB];
				disk_read(dir_ptr, rm_inode.sfi_indirect);

				int p;
				for(p = 0; p < SFS_DBPERIDB; p++)
				{
					if(dir_ptr[p])
					{
						// 해당하는 bitmap 해제
						char temp_bit[SFS_BLOCKSIZE];
						int num_bitmap = SFS_BITMAPSIZE(spb.sp_nblocks);
						num_bitmap = SFS_BITBLOCKS(spb.sp_nblocks);

						int temp_token = 0;
						int count = 0;
						for(i = 0; i < num_bitmap && count < spb.sp_nblocks && !temp_token; i++)
						{
							disk_read(temp_bit, 2 + i);
							int j;
							for(j = 0; j < 512 && count < spb.sp_nblocks && !temp_token; j++)
							{
								int k;
								for(k = 0; k < 8 && count < spb.sp_nblocks && !temp_token; k++)
								{
									if(count == dir_ptr[p])
									{
										BIT_CLEAR(temp_bit[j], k);
										temp_token = 1;
										disk_write(temp_bit, 2 + i);
									}

									count++;
								}
							}
						}
					}
				}

				// 해당하는 bitmap 해제
				char temp_bit[SFS_BLOCKSIZE];
				int num_bitmap = SFS_BITMAPSIZE(spb.sp_nblocks);
				num_bitmap = SFS_BITBLOCKS(spb.sp_nblocks);

				int temp_token = 0;
				int count = 0;
				for(i = 0; i < num_bitmap && count < spb.sp_nblocks && !temp_token; i++)
				{
					disk_read(temp_bit, 2 + i);
					int j;
					for(j = 0; j < 512 && count < spb.sp_nblocks && !temp_token; j++)
					{
						int k;
						for(k = 0; k < 8 && count < spb.sp_nblocks && !temp_token; k++)
						{
							if(count == rm_inode.sfi_indirect)
							{
								BIT_CLEAR(temp_bit[j], k);
								temp_token = 1;
								disk_write(temp_bit, 2 + i);
							}

							count++;
						}
					}
				}
			}

			rm_inode.sfi_indirect = SFS_NOINO;
			disk_write(&rm_inode, rm_idx);
		}


		//bzero(&rm_inode, SFS_BLOCKSIZE);
		//disk_write(&rm_inode, rm_idx);

		// 삭제한 inode에 해당하는 bitmap 해제
		char temp_bit[SFS_BLOCKSIZE];
		int num_bitmap = SFS_BITMAPSIZE(spb.sp_nblocks);
		num_bitmap = SFS_BITBLOCKS(spb.sp_nblocks);

		int temp_token = 0;
		int count = 0;
		for(i = 0; i < num_bitmap && count < spb.sp_nblocks && !temp_token; i++)
		{
			disk_read(temp_bit, 2 + i);
			int j;
			for(j = 0; j < 512 && count < spb.sp_nblocks && !temp_token; j++)
			{
				int k;
				for(k = 0; k < 8 && count < spb.sp_nblocks && !temp_token; k++)
				{
					if(count == rm_idx)
					{
						BIT_CLEAR(temp_bit[j], k);
						temp_token = 1;
						disk_write(temp_bit, 2 + i);
					}
					count++;
				}
			}
		}
	}
	// 삭제할 file이 존재하지 않는 경우 : [error] No such file or directory
	else
		error_message("rm", path, -1);
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