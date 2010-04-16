#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "fsvar.h"
#include "file.h"
#include "fcntl.h"

struct inode *jip;

int j_init()
{

  jip = create("./journal", 1, T_FILE, 0, 0);

  /* read journal */

  return 0;
}

int j_iupdate(struct inode *ip)
{

  struct buf *jbp;
  struct dinode *jdip;

  jbp = bread(ip->dev, IBLOCK(ip->inum));
  // jdip = (struct dinode*)jbp->data + ip->inum%IPB;
  jdip->type = ip->type;
  jdip->major = ip->major;
  jdip->minor = ip->minor;
  jdip->nlink = ip->nlink;
  jdip->size = ip->size;
  memmove(jdip->addrs, ip->addrs, sizeof(ip->addrs));
  bwrite(jbp);
  brelse(jbp);

  iupdate(ip);

  return 0;
}

int j_writei()
{
  uint tot, m;
  struct buf *jbuf;

  if(off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    n = MAXFILE*BSIZE - off;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    // bp = bread(ip->dev, bmap(ip, off/BSIZE, 1));
    m = min(n - tot, BSIZE - off%BSIZE);
    /* heres the magic */    
    

    memmove(bp->data + off%BSIZE, src, m);
    bwrite(bp);
    brelse(bp);
  }

  if(n > 0 && off > ip->size){
    ip->size = off;
    iupdate(ip);
  }
  return n;

}