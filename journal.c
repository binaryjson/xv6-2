#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "buf.h"
#include "fs.h"
#include "fsvar.h"
#include "file.h"
#include "fcntl.h"
#include "dev.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

struct inode *jip;
uint t_index;

struct buf *bp[20];
uint b_index;

int j_init()
{

  jip = create("./journal", 1, T_FILE, 0, 0);
  t_index = 0;
  /* read journal */
  cprintf("jip %x\n", jip);

  return 0;
}

// Allocate a disk block.
static uint
j_balloc(uint dev)
{
  int b, bi, m, i;
  //struct buf *bp;
  struct superblock sb;

  //  bp = 0;
  readsb(dev, &sb);
  for(b = 0; b < sb.size; b += BPB){
    /* check in dirty blocks */
    for(i = 0; i < b_index; i++)
      if(bp[i]->sector == BBLOCK(b, sb.ninodes)) {
	for(bi = 0; bi < BPB; bi++){
	  m = 1 << (bi % 8);
	  if((bp[i]->data[bi/8] & m) == 0){  // Is block free?
	    bp[i]->data[bi/8] |= m;  // Mark block in use on disk.
	    return b + bi;
	  }
	}
      }
    /* load new block out of mem */
    bp[b_index] = bread(dev, BBLOCK(b, sb.ninodes));
    for(bi = 0; bi < BPB; bi++){
      m = 1 << (bi % 8);
      if((bp[b_index]->data[bi/8] & m) == 0){  // Is block free?
	bp[b_index]->data[bi/8] |= m;  // Mark block in use on disk.
	/* keep dirty around, move index to next*/
	b_index++;
	return b + bi;
      }
    }
    //    panic("eh");
    brelse(bp[b_index]);
  }
  panic("balloc: out of blocks");
}

uint
j_lookup(struct inode *ip, uint bn)
{
  uchar found = 0;
  int i;
  uint addr, *a;

  //  struct buf *bp;
  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0){
      panic("fs fail");
    }
    return addr;
  }
  bn -= NDIRECT;
  if(bn < (NINDIRECT * NINDIRECT)){
    // Load double indirect block, allocating if necessary.
    if((addr = ip->addrs[INDIRECT]) == 0){
      panic("fs fail");
    }
    // check dirty blocks
    for(i = 0; i < b_index; i++){
      if(bp[i]->sector == addr){
	found = 1;
	a = (uint*)bp[i]->data;
	if((addr = a[(bn / NINDIRECT)]) == 0){
	  panic("fs fail");
	}
	break;
      }
    }
    if(!found) panic("oh shit");
    found = 0;
    for(i = 0;i < b_index; i++){
      if(bp[i]->sector == addr){
	found = 1;
	a = (uint*)bp[i]->data;
	if((addr = a[(bn % NINDIRECT)]) == 0){
	  panic("fs fail");
	}
	break;
      }
    }
    if(!found)    panic("oswhesn ");

    return addr;
  }

  panic("bmap: out of range");

}

uint
j_bmap(struct inode *ip, uint bn)
{
  uchar found;
  int i;
  uint addr, *a;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0){
      ip->addrs[bn] = addr = j_balloc(ip->dev);
    }
    return addr;
  }
  bn -= NDIRECT;
  if(bn < (NINDIRECT * NINDIRECT)){
 
    // Load double indirect block, allocating if necessary.
    if((addr = ip->addrs[INDIRECT]) == 0){
      ip->addrs[INDIRECT] = addr = j_balloc(ip->dev);
    }
    // check dirty blocks
    found = 0;
    for(i = 0; i < b_index; i++){
      if(bp[i]->sector == addr){
	found = 1;
	a = (uint*)bp[i]->data;
	if((addr = a[(bn / NINDIRECT)]) == 0){
	  a[(bn / NINDIRECT)] = addr = j_balloc(ip->dev);
	}
	break;
      }
    }
    if(!found){
      // load new block from mem    

      bp[b_index] = bread(ip->dev, addr);
      a = (uint*)bp[b_index]->data;

      b_index++;
      if((addr = a[(bn / NINDIRECT)]) == 0){	
	a[(bn / NINDIRECT)] = addr = j_balloc(ip->dev);
      }
    }
    //    brelse(bp);
    // Load indirect block, allocating if necessary.
    
    // check dirty blocks
    found = 0;
    for(i = 0;i < b_index; i++){
      if(bp[i]->sector == addr){
	found = 1;
	a = (uint*)bp[i]->data;
	if((addr = a[(bn % NINDIRECT)]) == 0){
	  a[(bn % NINDIRECT)] = addr = j_balloc(ip->dev);
	}
	break;
      }
    }
    if(!found){
      // load new block 
      bp[b_index] = bread(ip->dev, addr);
      a = (uint*)bp[b_index]->data;

      b_index++;
      if((addr = a[(bn % NINDIRECT)]) == 0){
	a[(bn % NINDIRECT)] = addr = j_balloc(ip->dev);
	//bwrite(bp);
      }
    }
    //brelse(bp);

    return addr;
  }

  panic("bmap: out of range");
}

int 
j_writei(struct inode *ip, char *src, uint off, uint n)
{
  uint tot, m, i, j;
  struct buf *tbp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
      return -1;
    return devsw[ip->major].write(ip, src, n);
  }

  if(off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    n = MAXFILE*BSIZE - off;

  /* REAL CODE STARTS HERE */

  b_index = 0; // new xfer, start keeping track of open bufs

  /* allocate all space needed */
  for(i=0, j=off; i<n; i+=m, j+=m){
    j_bmap(ip, j/BSIZE);
    m = min(n - i, BSIZE - j%BSIZE);
  }

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp[b_index] = bread(ip->dev, j_lookup(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(bp[b_index]->data + off%BSIZE, src, m);
    //    bwrite(bp);
    //    brelse(bp);

    b_index++;
  }
  
  for(i = 0; i < b_index; i++){
    bwrite(bp[i]);
    brelse(bp[i]);
  }
  
  if(n > 0 && off > ip->size){
    ip->size = off;
    iupdate(ip);
  }
  return n;

}
