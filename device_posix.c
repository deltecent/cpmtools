/* #includes */ /*{{{C}}}*//*{{{*/
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"
/*}}}*/

/* Device_open           -- Open an image file                      */ /*{{{*/
const char *Device_open(struct Device *this, const char *filename, int mode, const char *deviceOpts)
{
  if (deviceOpts != NULL)
  {
    return "POSIX driver accepts no options (build compiled without libdsk)";
  }
  this->fd=open(filename,mode);
  this->opened=(this->fd==-1?0:1);
  return ((this->fd==-1)?strerror(errno):(const char*)0);
}
/*}}}*/
/* Device_setGeometry    -- Set disk geometry                       */ /*{{{*/
const char *Device_setGeometry(struct Device *this, int secLength, int sectrk, int tracks, off_t offset, const char *libdskGeometry)
{
  this->secLength=secLength;
  this->sectrk=sectrk;
  this->tracks=tracks;
  this->offset=offset;
  return NULL;
}
/*}}}*/
/* Hard sector support: MITS 88-DCDD format */ /*{{{*/
/* An Altair 8" floppy stores each CP/M sector in a 137 byte slot, 32 slots
 * per track.  The slot layout differs between the system tracks (0-5) and
 * the data tracks (6 and up, wrapping every 128 tracks on 8 MB disks):
 *
 *   system: [0]=track|0x80 [1]=0 [2]=1 [3..130]=data [131]=0xff [132]=sum
 *   data:   [0]=track|0x80 [1]=sector [2,3,5,6]=Altair DOS use [4]=sum
 *           [7..134]=data [135]=0xff [136]=0
 *
 * where sum is the 8 bit sum of the data (plus bytes 2,3,5,6 on data tracks).
 * The sector number handed to Device_readSector/Device_writeSector is the
 * CP/M BIOS sector before the Altair skew; data tracks store it at
 * physical slot (sector*17) mod 32.
 */
#define HS_SLOT      137
#define HS_DATATRACK 6
#define HS_PAYLOAD   128

static int hsIsDataTrack(int track)
{
  return (track&0x7f)>=HS_DATATRACK;
}

static off_t hsSlotOffset(const struct Device *this, int track, int sector)
{
  int phys=hsIsDataTrack(track) ? (sector*17)&31 : sector;

  return (off_t)(track*this->sectrk+phys)*HS_SLOT+this->offset;
}

static unsigned char hsSum(const unsigned char *p, int n)
{
  unsigned char sum=0;

  while (n--) sum=(unsigned char)(sum+*p++);
  return sum;
}

/* hsRead -- read slot into slot[HS_SLOT]; short reads are zero filled */
static const char *hsRead(const struct Device *this, int track, int sector, unsigned char *slot)
{
  ssize_t res;

  if (lseek(this->fd,hsSlotOffset(this,track,sector),SEEK_SET)==-1) return strerror(errno);
  if ((res=read(this->fd,slot,HS_SLOT))==-1) return strerror(errno);
  if (res<HS_SLOT) memset(slot+res,0,HS_SLOT-res);
  return NULL;
}

static const char *hsCheck(const unsigned char *slot, int track, int sector)
{
  static char msg[64];
  int sum,ok;

  if (slot[0]!=(unsigned char)((track&0xff)|0x80))
  {
    snprintf(msg,sizeof(msg),"bad track byte in hard sector %d/%d",track,sector);
    return msg;
  }
  if (hsIsDataTrack(track))
  {
    sum=(hsSum(slot+7,HS_PAYLOAD)+slot[2]+slot[3]+slot[5]+slot[6])&0xff;
    ok=slot[1]==sector && slot[135]==0xff && slot[4]==sum;
  }
  else
  {
    sum=hsSum(slot+3,HS_PAYLOAD);
    ok=slot[131]==0xff && slot[132]==sum;
  }
  if (!ok)
  {
    snprintf(msg,sizeof(msg),"bad header or checksum in hard sector %d/%d",track,sector);
    return msg;
  }
  return NULL;
}
/*}}}*/
/* Device_setHardSector  -- Select hard sector slot format          */ /*{{{*/
const char *Device_setHardSector(struct Device *this, int type)
{
  this->hardsector=DEVICE_HS_NONE;
  if (type==DEVICE_HS_NONE) return NULL;
  if (type!=DEVICE_HS_ALTAIR) return "unknown hard sector format";
  if (this->secLength!=HS_PAYLOAD || this->sectrk!=32)
  {
    return "hardsector altair requires seclen 128 and sectrk 32";
  }
  this->hardsector=type;
  return NULL;
}
/*}}}*/
/* Device_close          -- Close an image file                     */ /*{{{*/
const char *Device_close(struct Device *this)
{
  this->opened=0;
  return ((close(this->fd)==-1)?strerror(errno):(const char*)0);
}
/*}}}*/
/* Device_readSector     -- read a physical sector                  */ /*{{{*/
const char *Device_readSector(const struct Device *this, int track, int sector, char *buf)
{
  int res;

  assert(this);
  assert(sector>=0);
  assert(sector<this->sectrk);
  assert(track>=0);
  assert(track<this->tracks);
  assert(buf);
  if (this->hardsector)
  {
    unsigned char slot[HS_SLOT];
    const char *err;

    if ((err=hsRead(this,track,sector,slot)) || (err=hsCheck(slot,track,sector))) return err;
    memcpy(buf,slot+(hsIsDataTrack(track)?7:3),HS_PAYLOAD);
    return (const char*)0;
  }
  if (lseek(this->fd,(off_t)(((sector+track*this->sectrk)*this->secLength)+this->offset),SEEK_SET)==-1) 
  {
    return strerror(errno);
  }
  if ((res=read(this->fd, buf, this->secLength)) != this->secLength) 
  {
    if (res==-1)
    {
      return strerror(errno);
    }
    else
{
printf("len %d\n",this->secLength-res);
 memset(buf+res,0,this->secLength-res); /* hit end of disk image */
}
  }
  return (const char*)0;
}
/*}}}*/
/* Device_writeSector    -- write physical sector                   */ /*{{{*/
const char *Device_writeSector(const struct Device *this, int track, int sector, const char *buf)
{
  assert(sector>=0);
  assert(sector<this->sectrk);
  assert(track>=0);
  assert(track<this->tracks);
  if (this->hardsector)
  {
    unsigned char slot[HS_SLOT];
    const char *err;

    /* keep what is there: bytes 2,3,5,6 of a data sector are not ours */
    if ((err=hsRead(this,track,sector,slot))) return err;
    slot[0]=(unsigned char)((track&0xff)|0x80);
    if (hsIsDataTrack(track))
    {
      slot[1]=(unsigned char)sector;
      memcpy(slot+7,buf,HS_PAYLOAD);
      slot[135]=0xff;
      slot[136]=0;
      slot[4]=(unsigned char)(hsSum(slot+7,HS_PAYLOAD)+slot[2]+slot[3]+slot[5]+slot[6]);
    }
    else
    {
      slot[1]=0;
      slot[2]=1;
      memcpy(slot+3,buf,HS_PAYLOAD);
      slot[131]=0xff;
      slot[132]=hsSum(slot+3,HS_PAYLOAD);
      memset(slot+133,0,HS_SLOT-133);
    }
    if (lseek(this->fd,hsSlotOffset(this,track,sector),SEEK_SET)==-1) return strerror(errno);
    if (write(this->fd,slot,HS_SLOT)==HS_SLOT) return (const char*)0;
    return strerror(errno);
  }
  if (lseek(this->fd,(off_t)(((sector+track*this->sectrk)*this->secLength)+this->offset),SEEK_SET)==-1)
  {
    return strerror(errno);
  }
  if (write(this->fd, buf, this->secLength) == this->secLength) return (const char*)0;
  return strerror(errno);
}
/*}}}*/
