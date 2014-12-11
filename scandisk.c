#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"
#include <ctype.h>

//adapting linked list methods from moodle
struct node {
    int clusternum;
    int orphansize;
    struct node *next; 
};
void list_clear(struct node *list) {
    while (list != NULL) {
        struct node *tmp = list;
        list = list->next;
        free(tmp);
    }
}
void list_append(int orphanClust, struct node **head, int num) {
    struct node *new = malloc(sizeof(struct node));
    new->clusternum = orphanClust;
    new->next = NULL;
    new->orphansize = num;
    // handle special case of the list being empty
    if (*head == NULL) {
        *head = new;
        return;
    }
    struct node *tmp = *head;
    while (tmp->next != NULL) {
        tmp = tmp->next;
    }
    tmp->next = new;
}

//taken from dos_cp
void write_dirent(struct direntry *dirent, char *filename, 
		  uint16_t start_cluster, uint32_t size)
{
    char *p, *p2;
    char *uppername;
    int len, i;
    /* clean out anything old that used to be here */
    memset(dirent, 0, sizeof(struct direntry));
    /* extract just the filename part */
    uppername = strdup(filename);
    p2 = uppername;
    for (i = 0; i < strlen(filename); i++) 
    {
	if (p2[i] == '/' || p2[i] == '\\') 
	{
	    uppername = p2+i+1;
	}
    }
    /* convert filename to upper case */
    for (i = 0; i < strlen(uppername); i++) 
    {
	uppername[i] = toupper(uppername[i]);
    }
    /* set the file name and extension */
    memset(dirent->deName, ' ', 8);
    p = strchr(uppername, '.');
    memcpy(dirent->deExtension, "___", 3);
    if (p == NULL) 
    {
	fprintf(stderr, "No filename extension given - defaulting to .___\n");
    }
    else 
    {
	*p = '\0';
	p++;
	len = strlen(p);
	if (len > 3) len = 3;
	memcpy(dirent->deExtension, p, len);
    }

    if (strlen(uppername)>8) 
    {
	uppername[8]='\0';
    }
    memcpy(dirent->deName, uppername, strlen(uppername));
    free(p2);
    /* set the attributes and file size */
    dirent->deAttributes = ATTR_NORMAL;
    putushort(dirent->deStartCluster, start_cluster);
    putulong(dirent->deFileSize, size);
    /* could also set time and date here if we really
       cared... */
}
void create_dirent(struct direntry *dirent, char *filename, 
		   uint16_t start_cluster, uint32_t size,
		   uint8_t *image_buf, struct bpb33* bpb)
{
    while (1) 
    {
	if (dirent->deName[0] == SLOT_EMPTY) 
	{
	    write_dirent(dirent, filename, start_cluster, size);
	    dirent++;

	    /* make sure the next dirent is set to be empty, just in
	       case it wasn't before */
	    memset((uint8_t*)dirent, 0, sizeof(struct direntry));
	    dirent->deName[0] = SLOT_EMPTY;
	    return;
	}

	if (dirent->deName[0] == SLOT_DELETED) 
	{
	    write_dirent(dirent, filename, start_cluster, size);
	    return;
	}
	dirent++;
    }
}



//adapting methods given in example dos_ls.c
void print_indent(int indent)
{
    int i;
    for (i = 0; i < indent*4; i++)
	printf(" ");
}
int clusterNeeded(long file_size){
	return((file_size+511)/512);
}
void free_cluster(uint8_t *image_buf, struct bpb33* bpb,uint16_t cluster, int * clusts)
{ 
 
 while (is_valid_cluster(cluster, bpb))
 {
           uint16_t next = get_fat_entry(cluster, image_buf, bpb);
	   set_fat_entry(cluster,FAT12_MASK&CLUST_FREE, image_buf, bpb);
	   clusts[next]--;
	   cluster = next;
  } 
  if(is_end_of_file(cluster))
  {	//if old end of file, need to set to free!!!
	set_fat_entry(cluster, FAT12_MASK&CLUST_FREE, image_buf,bpb);
  }
} 
//EDIT THIS AND MAKE MORE SIMPLE
uint16_t print_dirent(struct direntry *dirent, int indent,uint8_t *image_buf, struct bpb33* bpb, int *clusts)
{
    uint16_t followclust = 0;

    int i;
    char name[9];
    char extension[4];
    uint32_t size;
    uint16_t file_cluster;
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    if (name[0] == SLOT_EMPTY)
    {
	return followclust;
    }

    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED)
    {
	return followclust;
    }

    if (((uint8_t)name[0]) == 0x2E)
    {
	// dot entry ("." or "..")
	// skip it
        return followclust;
    }

    /* names are space padded - remove the spaces */
    for (i = 8; i > 0; i--) 
    {
	if (name[i] == ' ') 
	    name[i] = '\0';
	else 
	    break;
    }

    /* remove the spaces from extensions */
    for (i = 3; i > 0; i--) 
    {
	if (extension[i] == ' ') 
	    extension[i] = '\0';
	else 
	    break;
    }

    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN)
    {
	// ignore any long file name extension entries
	//
	// printf("Win95 long-filename entry seq 0x%0x\n", dirent->deName[0]);
    }
    else if ((dirent->deAttributes & ATTR_VOLUME) != 0) 
    {
	printf("Volume: %s\n", name);
    } 
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) 
    {
        // don't deal with hidden directories; MacOS makes these
        // for trash directories and such; just ignore them.
	if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN)
        {
	     print_indent(indent);
    	    printf("%s/ (directory)\n", name);
            file_cluster = getushort(dirent->deStartCluster);
            followclust = file_cluster;
        }
    }
    else 
    {
        /*
         * a "regular" file entry
         * print attributes, size, starting cluster, etc.
         */
	int ro = (dirent->deAttributes & ATTR_READONLY) == ATTR_READONLY;
	int hidden = (dirent->deAttributes & ATTR_HIDDEN) == ATTR_HIDDEN;
	int sys = (dirent->deAttributes & ATTR_SYSTEM) == ATTR_SYSTEM;
	int arch = (dirent->deAttributes & ATTR_ARCHIVE) == ATTR_ARCHIVE;

	size = getulong(dirent->deFileSize);
	print_indent(indent);
	printf("%s.%s (%u bytes) (starting cluster %d) %c%c%c%c\n", 
	       name, extension, size, getushort(dirent->deStartCluster),
	       ro?'r':' ', 
               hidden?'h':' ', 
               sys?'s':' ', 
               arch?'a':' ');
       int chain = 0;
       uint16_t cluster = getushort(dirent->deStartCluster);
       uint16_t target = 0;
       while (is_valid_cluster(cluster, bpb))
      {
      clusts[cluster] ++;
      uint16_t prev = cluster;
      cluster = get_fat_entry(cluster, image_buf, bpb);
       if(clusterNeeded(size)==1 && chain==0)
       {
         //special case for if clusters needed ==1
         target = prev;
       }
       if(cluster == prev)
      { 
         printf("CLUSTER POINTS TO ITSELF IN A LOOP\n");
         set_fat_entry(cluster, FAT12_MASK& CLUST_EOFS, image_buf, bpb);
         chain++;
         break;
      }
      if(cluster==(FAT12_MASK&CLUST_BAD))
      {
        target = cluster;
        set_fat_entry(cluster, FAT12_MASK & CLUST_FREE, image_buf, bpb);
        set_fat_entry(prev, FAT12_MASK & CLUST_EOFS, image_buf, bpb);
        printf("FOUND A CLUSTER MARKED BAD\n");
        chain++;
        break;
      }
        chain++; 
      if((clusterNeeded(size)==chain+1))
	//first cluster that sets off inconsistency
	{
	 target = cluster;
	}
     } 

     if(clusterNeeded(size)<chain)
     {	
       printf("NUMBER OF CLUSTERS IS LONGER THAN SIZE SPECIFIED IN METADATA\n");
       printf("SIZE: %d CLUSTERS,CHAIN LENGTH: %d CLUSTERS \n", clusterNeeded(size), chain);
       cluster = get_fat_entry(target,image_buf,bpb);
       set_fat_entry(target, FAT12_MASK&CLUST_EOFS, image_buf, bpb);
       free_cluster(image_buf, bpb, cluster, clusts);
      }
    if(clusterNeeded(size)>chain)
    {
        printf("INCONSISTENT METADATA\n");
        printf("SIZE: %d CLUSTERS ,CHAIN LENGTH: %d CLUSTERS\n", clusterNeeded(size), chain);
	int change = chain*512;
        putulong(dirent->deFileSize, change);
    }
 } 

 return followclust;
}
void search_FAT(uint8_t *image_buf, struct bpb33* bpb, int * clusts){
    //create linked list of orphans
    struct node *orphans = NULL;
    int orphan_size = 0;
    for (int i =2; i<bpb->bpbSectors;i++){
	orphan_size=0;
        uint16_t cluster = get_fat_entry(i, image_buf, bpb);
    	if(cluster!= (FAT12_MASK &CLUST_BAD) && cluster != CLUST_FREE && clusts[i]==0){
		printf("FOUND AN ORPHAN AT CLUSTER %d \n", i);
    		clusts[i] = 1;
		orphan_size = 1;	
		//keep looking through chain until eof
		if(is_valid_cluster(cluster, bpb)){
	             int place_in_array = cluster;		
		     cluster = get_fat_entry(cluster, image_buf, bpb);
		     while (is_valid_cluster(cluster, bpb)){
			  orphan_size++;
			  clusts[place_in_array]++;
			  place_in_array = cluster;
	    	    	 cluster = get_fat_entry(cluster, image_buf, bpb);
		     }
                      orphan_size++;
              }
              list_append(i, &orphans, orphan_size); 
     	}
     }
     //have to create a while loop for iterating through the LL
     struct node* temp = orphans;//for iterating
     char * prename = "orphans";
     char * ext = ".dat";
     char orph = '1';
     char filename[12];
     while(temp != NULL){
	struct direntry *dirent = (struct direntry*)root_dir_addr(image_buf, bpb);
        int i = 0;
	for (; i<7; i++)
        {
          filename[i] =prename[i];
        }
        filename[i] = orph;
        i++;
        for(int j = 0;j<4;j++)
        {
          filename[i] = ext[j];
          i++;
        }
   	orphan_size =(temp->orphansize)*512;
  	create_dirent(dirent,filename, temp->clusternum, orphan_size, image_buf, bpb);
        temp= temp->next;
        orph++;
     }
     list_clear(orphans);	
}



void follow_dir(uint16_t cluster, int indent, uint8_t *image_buf, struct bpb33* bpb,int * clusts){
    while (is_valid_cluster(cluster, bpb)){
        struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
        int numDirEntries = (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) / sizeof(struct direntry);
        int i = 0;
	for ( ; i < numDirEntries; i++){  
              uint16_t followclust = print_dirent(dirent, indent, image_buf, bpb, clusts);
              if (followclust){   
                 follow_dir(followclust, indent+1, image_buf, bpb, clusts);
	      }
              dirent++;
       }
       cluster = get_fat_entry(cluster, image_buf, bpb);
    }   
}


void traverse_root(uint8_t *image_buf, struct bpb33* bpb, int * clusts)
{
    uint16_t cluster = 0;
    struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
    int i = 0;
    for ( ; i < bpb->bpbRootDirEnts; i++){
        uint16_t followclust = print_dirent(dirent, 0, image_buf, bpb, clusts);
        if (is_valid_cluster(followclust, bpb)){
	   		clusts[followclust] ++;
           	        follow_dir(followclust, 1, image_buf, bpb, clusts);
        }
        dirent++;
    }
}

void usage(char *progname) {
    fprintf(stderr, "usage: %s <imagename>\n", progname);
    exit(1);
}


int main(int argc, char** argv) {
    uint8_t *image_buf;
    int fd;
    struct bpb33* bpb;
    if (argc < 2) {
	usage(argv[0]);
    }
 
    image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf);
    u_int16_t sectors = (bpb->bpbSectors) - (2*(bpb->bpbFATsecs))- 15;
    int clusters = (sectors/(bpb->bpbSecPerClust)) +2;
    int * refs =  malloc(sizeof(int) * clusters);
    memset(refs, 0, (sizeof(int)*clusters));
    //call to iterate through directory
     traverse_root(image_buf, bpb,refs);
    //function to search for orphan clusters in the FAT
     search_FAT(image_buf, bpb,refs);

   
    free(refs);
    unmmap_file(image_buf, &fd);
     free(bpb);
    return 0;
}
