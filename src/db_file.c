/* aide, Advanced Intrusion Detection Environment
 *
 * Copyright (C) 1999-2007,2010-2013,2016,2018-2020 Rami Lehti, Pablo Virolainen,
 * Mike Markley, Richard van den Berg, Hannes von Haugwitz
 * $Header$
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "aide.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#include <errno.h>

#include "types.h"
#include "base64.h"
#include "db_file.h"
#include "gen_list.h"
#include "conf_yacc.h"
#include "util.h"
#include "commandconf.h"
/*for locale support*/
#include "locale-aide.h"
/*for locale support*/

#ifdef WITH_MHASH
#include <mhash.h>
#endif

#ifdef WITH_ZLIB
#include <zlib.h>
#endif

#define BUFSIZE 16384

#include "md.h"

#ifdef WITH_ZLIB
#define ZBUFSIZE 16384

static int dofprintf( const char* s,...)
#ifdef __GNUC__
        __attribute__ ((format (printf, 1, 2)));
#else
        ;
#endif

/* FIXME get rid of this */
void handle_gzipped_input(int out,gzFile* gzp){

  int nread=0;
  int err=0;
  int* buf=malloc(ZBUFSIZE);
  buf[0]='\0';
  error(200,"handle_gzipped_input(),%d\n",out);
  while(!gzeof(*gzp)){
    if((nread=gzread(*gzp,buf,ZBUFSIZE))<0){
      error(0,_("gzread() failed: gzerr=%s!\n"),gzerror(*gzp,&err));
      exit(1);
    } else {
      int tmp = 0;
      
      /* gzread returns 0 even if uncompressed bytes were read */
      if(nread==0){
        tmp = strlen((char*)buf);
      } else {
        tmp = nread;
      }
      if (write(out, buf,nread) != tmp)
      {
        error(0,_("write() failed: %s\n"), strerror(errno));
        exit(1);
      }
      
      error(240,"nread=%d,strlen(buf)=%lu,errno=%s,gzerr=%s\n",
	    nread,(unsigned long)strlen((char*)buf),strerror(errno),
	    gzerror(*gzp,&err));
      buf[0]='\0';
    }
  }
  close(out);
  error(240,"handle_gzipped_input() exiting\n");
  exit(0);
  /* NOT REACHED */
  return;
}
#endif


int dofflush(void)
{

  int retval;
#ifdef WITH_ZLIB
  if(conf->gzip_dbout){
    /* Should not flush using gzip, it degrades compression */
    retval=Z_OK;
  }else {
#endif
    retval=fflush(conf->db_out); 
#ifdef WITH_ZLIB
  }
#endif

  return retval;
}

int dofprintf( const char* s,...)
{
  char buf[3];
  int retval;
  char* temp=NULL;
  va_list ap;
  
  va_start(ap,s);
  retval=vsnprintf(buf,3,s,ap);
  va_end(ap);
  
  temp=(char*)malloc(retval+2);
  if(temp==NULL){
    error(0,"Unable to alloc %i bytes\n",retval+2);
    return -1;
  }  
  va_start(ap,s);
  retval=vsnprintf(temp,retval+1,s,ap);
  va_end(ap);
  
  if (conf->mdc_out) {
      update_md(conf->mdc_out,temp ,retval);
  }

#ifdef WITH_MHASH
  if(conf->do_dbnewmd)
    mhash(conf->dbnewmd,(void*)temp,retval);
#endif

#ifdef WITH_ZLIB
  if(conf->gzip_dbout){
    retval=gzwrite(conf->db_gzout,temp,retval);
  }else{
#endif
    /* writing is ok with fwrite with curl.. */
    retval=fwrite(temp,1,retval,conf->db_out);
#ifdef WITH_ZLIB
  }
#endif
  free(temp);

  return retval;
}



int db_file_read_spec(int db){
  
  int i=0;
  int* db_osize=0;
  ATTRIBUTE** db_order=NULL;
  DB_ATTR_TYPE seen_attrs = 0LLU;

  switch (db) {
  case DB_OLD: {
    db_osize=&(conf->db_in_size);
    db_order=&(conf->db_in_order);
    db_lineno=&db_in_lineno;
    break;
  }
  case DB_NEW: {
    db_osize=&(conf->db_new_size);
    db_order=&(conf->db_new_order);
    db_lineno=&db_new_lineno;
    break;
  }
  }

  *db_order = malloc(1*sizeof(ATTRIBUTE));
  
  while ((i=db_scan())!=TNEWLINE){
    switch (i) {
      
    case TID : {
      DB_ATTR_TYPE l;
      *db_order = realloc(*db_order, ((*db_osize)+1)*sizeof(ATTRIBUTE));
      if(*db_order == NULL) {
          return RETFAIL;
      }
      (*db_order)[*db_osize]=attr_unknown;
      for (l=0;l<num_attrs;l++){
          if (attributes[l].db_name && strcmp(attributes[l].db_name,dbtext)==0) {
              if (ATTR(l)&seen_attrs) {
                  error(0,"Field %s redefined in @@dbspec\n",dbtext);
                  (*db_order)[*db_osize]=attr_unknown;
              } else {
                  (*db_order)[*db_osize]=l;
                  seen_attrs |= ATTR(l);
              }
              (*db_osize)++;
              break;
          }
      }

      if(l==attr_unknown){
          error(0,"Unknown field %s in database\n",dbtext);
          (*db_order)[*db_osize]=attr_unknown;
          (*db_osize)++;
      }
      break;
    }
    
    case TDBSPEC : {
      error(0,"Only one @@dbspec in input database.\n");
      return RETFAIL;
      break;
    }
    
    default : {
      error(0,"Aide internal error while reading input database.\n");
      return RETFAIL;
    }
    }
  }

  /* Lets generate attr from db_order if database does not have attr */
  conf->attr=-1;

  for (i=0;i<*db_osize;i++) {
    if ((*db_order)[i]==attr_attr) {
      conf->attr=1;
    }
  }
  if (conf->attr==DB_ATTR_UNDEF) {
    conf->attr=0;
    error(0,"Database does not have attr field.\nComparison may be incorrect\nGenerating attr-field from dbspec\nIt might be a good Idea to regenerate databases. Sorry.\n");
    for(i=0;i<*db_osize;i++) {
      conf->attr|=1LL<<(*db_order)[i];
    }
  }
  return RETOK;
}

char** db_readline_file(int db){
  
  char** s=NULL;
  
  int i=0;
  int r;
  int a=0;
  int token=0;
  int gotbegin_db=0;
  int gotend_db=0;
  int* domd=NULL;
#ifdef WITH_MHASH
  MHASH* md=NULL;
  char** oldmdstr=NULL;
#endif
  int* db_osize=0;
  ATTRIBUTE** db_order=NULL;
  FILE* db_filep=NULL;
  url_t* db_url=NULL;

  switch (db) {
  case DB_OLD: {
#ifdef WITH_MHASH
    md=&(conf->dboldmd);
    oldmdstr=&(conf->old_dboldmdstr);
#endif
    domd=&(conf->do_dboldmd);
    
    db_osize=&(conf->db_in_size);
    db_order=&(conf->db_in_order);
    db_filep=conf->db_in;
    db_url=conf->db_in_url;
    db_lineno=&db_in_lineno;
    break;
  }
  case DB_NEW: {
#ifdef WITH_MHASH
    md=&(conf->dbnewmd);
    oldmdstr=&(conf->old_dbnewmdstr);
#endif
    domd=&(conf->do_dbnewmd);
    
    db_osize=&(conf->db_new_size);
    db_order=&(conf->db_new_order);
    db_filep=conf->db_new;
    db_url=conf->db_new_url;
    db_lineno=&db_new_lineno;
    break;
  }
  }
  
  if (*db_osize==0) {
    db_buff(db,db_filep);
    
    token=db_scan();
    while((token!=TDBSPEC && token!=TEOF)){

      switch(token){
      case TUNKNOWN: {
	continue;
      }
      case TBEGIN_DB: {
	token=db_scan();
	gotbegin_db=1;
	continue;
      }
      case TNEWLINE: {
	if(gotbegin_db){
	  *domd=1;
	  token=db_scan();
	  continue;
	}else {
	  token=TEOF;
	  break;
	}
      }
      case TGZIPHEADER: {
	error(0,"Gzipheader found inside uncompressed db!\n");
	return NULL;
      }
      default: {
	/* If it is anything else we quit */
	/* Missing dbspec */
	token=TEOF;
	break;
      }
      }
    }

    if(FORCEDBMD&&!gotbegin_db){
      error(0,"Database %i does not have checksum!\n",db);
      return NULL;
    }

    if (token!=TDBSPEC) {
      /*
       * error.. must be a @@dbspec line
       */
      
      switch (db_url->type) {
      case url_file : {
	error(0,"File database must have one db_spec specification\n");
	break;
      }

      case url_stdin : {
	error(0,"Pipe database must have one db_spec specification\n");
	break;
      }

      case url_fd: {
	error(0,"FD database must have one db_spec specification\n");
	break;
      }
#ifdef WITH_CURL
      case url_http:
      case url_https:
      case url_ftp: {
	error(0,"CURL database must have one db_spec specification %i\n",token);
	break;
      }
#endif
	
      default : {
	error(0,"db_readline_file():Unknown or unsupported db in type.\n");
	
	break;
      }
      
      }
      return s;
    }
    
    /*
     * Here we read da spec
     */
    
    if (db_file_read_spec(db)!=0) {
      /* somethin went wrong */
      return s;
    }
    
  }else {
    /* We need to switch the buffer cleanly*/
    db_buff(db,NULL);
  }

  s=(char**)malloc(sizeof(char*)*num_attrs);

  /* We NEED this to avoid Bus errors on Suns */
  for(ATTRIBUTE a=0; a<num_attrs; a++){
    s[a]=NULL;
  }
  
  for(i=0;i<*db_osize;i++){
    switch (r=db_scan()) {
      
    case TDBSPEC : {
      
      error(0,"Database file can have only one db_spec.\nTrying to continue on line %li\n",*db_lineno);      
      break;
    }
    case TNAME : {
      if ((*db_order)[i]!=attr_unknown) {
	s[*db_order[i]]=(char*)strdup(dbtext);
      }
      break;
    }
    
    case TID : {
      if ((*db_order)[i]!=attr_unknown) {
	s[(*db_order)[i]]=(char*)strdup(dbtext);
      }
      break;
    }
    
    case TNEWLINE : {
      
      if (i==0) {
	i--;
	break;
      }
      if(gotend_db){
	return NULL;
      }
      /*  */

      error(0,"Not enough parameters in db:%li. Trying to continue.\n",
	    *db_lineno);
      for(a=0;a<i;a++){
	free(s[(*db_order)[a]]);
	s[(*db_order)[a]]=NULL;
      }
      i=0;
      break;

    }

    case TBEGIN_DB : {
      error(0,_("Corrupt db. Found @@begin_db inside db. Please check\n"));
      return NULL;
      break;
    }

    case TEND_DB : {
      gotend_db=1;
      token=db_scan();
      if(token!=TSTRING){
	error(0,_("Corrupt db. Checksum garbled\n"));
	abort();
      } else { /* FIXME: this probably isn't right */
#ifdef WITH_MHASH
	if(*md){
	  byte* dig=NULL;
	  char* digstr=NULL;
	  
	  *oldmdstr=strdup(dbtext);
	  
	  mhash(*md,NULL,0);
	  dig=(byte*)
	    malloc(sizeof(byte)*mhash_get_block_size(conf->dbhmactype));
	  mhash_deinit(*md,(void*)dig);
	  digstr=encode_base64(dig,mhash_get_block_size(conf->dbhmactype));
	  if(strncmp(digstr,*oldmdstr,strlen(digstr))!=0){
	    error(0,_("Db checksum mismatch for db:%i\n"),db);
	    abort();
	  }
	}
        else
        {
	  error(0,"@@end_db found without @@begin_db in db:%i\n",db);
	  abort();
	}
#endif
      }
      token=db_scan();
      if(token!=TNEWLINE){
	error(0,_("Corrupt db. Checksum garbled\n"));
	abort();
      }	
      break;
    }

    case TEND_DBNOMD : {
      gotend_db=1;
      if(FORCEDBMD){
        error(0,"Database %i does not have checksum!\n",db);
	abort();
      }
      break;
    }

    case TEOF : {
      if(gotend_db){
	return NULL;
      }	
      /* This can be the first token on a line */
      if(i>0){
	error(0,"Not enough parameters in db:%li\n",*db_lineno);
      };
      for(a=0;a<i;a++){
	free(s[(*db_order)[a]]);
      }
      free(s);
      return NULL;
      break;
    }
    case TERROR : {
      error(0,"There was an error in the database file on line:%li.\n",*db_lineno);
      break;
    }
    
    default : {
      
      error(0,"Not implemented in db_readline_file %i\n\"%s\"",r,dbtext);
      
      free(s);
      s=NULL;
      i=*db_osize;
      break;
    }
    }
    
  }
  

  /*
   * If we don't get newline after reading all cells we print an error
   */
  a=db_scan();

  if (a!=TNEWLINE&&a!=TEOF) {
    error(0,"Newline expected in database. Reading until end of line\n");
    do {
      
      error(0,"Skipped value %s\n",dbtext);
      
      /*
       * Null statement
       */ 
      a=db_scan();
    }while(a!=TNEWLINE&&a!=TEOF);
    
  }
  
  return s;
  
}

int db_writechar(char* s,FILE* file,int i)
{
  char* r=NULL;
  int retval=0;

  (void)file;
  
  if(i) {
    dofprintf(" ");
  }

  if(s==NULL){
    retval=dofprintf("0");
    return retval;
  }
  if(s[0]=='\0'){
    retval=dofprintf("0-");
    return retval;
  }
  if(s[0]=='0'){
    retval=dofprintf("00");
    if(retval<0){
      return retval;
    }
    s++;
  }
  
  if (!i && s[0]=='#') {
    dofprintf("# ");
    r=CLEANDUP(s+1);
  } else {
    r=CLEANDUP(s);
  }
  
  retval=dofprintf("%s",r);
  free(r);
  return retval;
}

static int db_writelong(long i,FILE* file,int a)
{
  (void)file;
  
  if(a) {
    dofprintf(" ");
  }
  
  return dofprintf("%li",i);
  
}

static int db_writelonglong(long long i,FILE* file,int a)
{
  (void)file;
  
  if(a) {
    dofprintf(" ");
  }
  
  return dofprintf("%lli",i);
  
}


int db_write_byte_base64(byte*data,size_t len,FILE* file,int i,
                         DB_ATTR_TYPE th, DB_ATTR_TYPE attr )
{
  char* tmpstr=NULL;
  int retval=0;
  
  (void)file;  
  if (data && !len)
    len = strlen((const char *)data);
  
  if (data!=NULL&&th&attr) {
    tmpstr=encode_base64(data,len);
  } else {
    tmpstr=NULL;
  }
  if(i){
    dofprintf(" ");
  }

  if(tmpstr){
    retval=dofprintf("%s", tmpstr);
    free(tmpstr);
    return retval;
  }else {
    return dofprintf("0");
  }
  return 0;

}

int db_write_time_base64(time_t i,FILE* file,int a)
{
  static char* ptr=NULL;
  char* tmpstr=NULL;
  int retval=0;

  (void)file;
  
  if(a){
    dofprintf(" ");
  }

  if(i==0){
    retval=dofprintf("0");
    return retval;
  }


  ptr=(char*)malloc(sizeof(char)*TIMEBUFSIZE);
  if (ptr==NULL) {
    error(0,"\nCannot allocate memory.\n");
    abort();
  }
  memset((void*)ptr,0,sizeof(char)*TIMEBUFSIZE);

  sprintf(ptr,"%li",i);


  tmpstr=encode_base64((byte *)ptr,strlen(ptr));
  retval=dofprintf("%s", tmpstr);
  free(tmpstr);
  free(ptr);

  return retval;

}

int db_writeoct(long i, FILE* file,int a)
{
  (void)file;
  
  if(a) {
    dofprintf(" ");
  }
  
  return dofprintf("%lo",i);
  
}

int db_writespec_file(db_config* dbconf)
{
  int retval=1;
  struct tm* st;
  time_t tim=time(&tim);
  st=localtime(&tim);

  retval=dofprintf("@@begin_db\n");
  if(retval==0){
    return RETFAIL;
  }

#ifdef WITH_MHASH
  void*key=NULL;
  int keylen=0;
  /* From hereon everything must MD'd before write to db */
  if((key=get_db_key())!=NULL){
    keylen=get_db_key_len();
    dbconf->do_dbnewmd=1;
    if( (dbconf->dbnewmd=
	 mhash_hmac_init(dbconf->dbhmactype,
			 key,
			 keylen,
			 mhash_get_hash_pblock(dbconf->dbhmactype)))==
	MHASH_FAILED){
      error(0, "mhash_hmac_init() failed for db write. Aborting\n");
      abort();
    }
  }
  
  
#endif

  if(dbconf->database_add_metadata) {
      retval=dofprintf(
             "# This file was generated by Aide, version %s\n"
             "# Time of generation was %.4u-%.2u-%.2u %.2u:%.2u:%.2u\n",
             AIDEVERSION,
             st->tm_year+1900, st->tm_mon+1, st->tm_mday,
             st->tm_hour, st->tm_min, st->tm_sec
             );
      if(retval==0){
        return RETFAIL;
      }
  }
  if(dbconf->config_version){
    retval=dofprintf(
		     "# The config version used to generate this file was:\n"
		     "# %s\n", dbconf->config_version);
    if(retval==0){
      return RETFAIL;
    }
  }
  retval=dofprintf("@@db_spec ");
  if(retval==0){
    return RETFAIL;
  }
  for (ATTRIBUTE i = 0 ; i < num_attrs ; ++i) {
      if (attributes[i].db_name && attributes[i].attr&conf->db_out_attrs) {
          retval=dofprintf("%s ", attributes[i].db_name);
          if(retval==0){
              return RETFAIL;
          }
      }
  }
  retval=dofprintf("\n");
  if(retval==0){
    return RETFAIL;
  }
  return RETOK;
}

#ifdef WITH_ACL
int db_writeacl(acl_type* acl,FILE* file,int a)
{
#ifdef WITH_POSIX_ACL
  if(a) {
    dofprintf(" ");
  }
  
  if (acl==NULL) {
    dofprintf("0");
  } else {    
    dofprintf("POSIX"); /* This is _very_ incompatible */

    dofprintf(",");
    if (acl->acl_a)
      db_write_byte_base64((byte*)acl->acl_a, 0, file,0,1,1);
    else
      dofprintf("0");
    dofprintf(",");
    if (acl->acl_d)
      db_write_byte_base64((byte*)acl->acl_d, 0, file,0,1,1);
    else
      dofprintf("0");
  }
#endif
#ifndef WITH_ACL
  if(a) { /* compat. */
    dofprintf(" ");
  }
  
  dofprintf("0");
#endif
  
  return RETOK;
}
#endif


#define WRITE_HASHSUM(x) \
case attr_ ##x : { \
    db_write_byte_base64(line->hashsums[hash_ ##x], \
        hashsums[hash_ ##x].length, \
        dbconf->db_out, i, \
        ATTR(attr_ ##x), line->attr); \
    break; \
}

int db_writeline_file(db_line* line,db_config* dbconf, url_t* url){

  (void)url;

  for (ATTRIBUTE i = 0 ; i < num_attrs ; ++i) {
    if (attributes[i].db_name && ATTR(i)&conf->db_out_attrs) {
    switch (i) {
    case attr_filename : {
      db_writechar(line->filename,dbconf->db_out,i);
      break;
    }
    case attr_linkname : {
      db_writechar(line->linkname,dbconf->db_out,i);
      break;
    }
    case attr_bcount : {
      db_writelonglong(line->bcount,dbconf->db_out,i);
      break;
    }

    case attr_mtime : {
      db_write_time_base64(line->mtime,dbconf->db_out,i);
      break;
    }
    case attr_atime : {
      db_write_time_base64(line->atime,dbconf->db_out,i);
      break;
    }
    case attr_ctime : {
      db_write_time_base64(line->ctime,dbconf->db_out,i);
      break;
    }
    case attr_inode : {
      db_writelong(line->inode,dbconf->db_out,i);
      break;
    }
    case attr_linkcount : {
      db_writelong(line->nlink,dbconf->db_out,i);
      break;
    }
    case attr_uid : {
      db_writelong(line->uid,dbconf->db_out,i);
      break;
    }
    case attr_gid : {
      db_writelong(line->gid,dbconf->db_out,i);
      break;
    }
    case attr_size : {
      db_writelonglong(line->size,dbconf->db_out,i);
      break;
    }
    case attr_perm : {
      db_writeoct(line->perm,dbconf->db_out,i);
      break;
    }
    WRITE_HASHSUM(md5)
    WRITE_HASHSUM(sha1)
    WRITE_HASHSUM(rmd160)
    WRITE_HASHSUM(tiger)
    WRITE_HASHSUM(crc32)
    WRITE_HASHSUM(crc32b)
    WRITE_HASHSUM(haval)
    WRITE_HASHSUM(gostr3411_94)
    WRITE_HASHSUM(sha256)
    WRITE_HASHSUM(sha512)
    WRITE_HASHSUM(whirlpool)
    case attr_attr : {
      db_writelonglong(line->attr, dbconf->db_out,i);
      break;
    }
#ifdef WITH_ACL
    case attr_acl : {
      db_writeacl(line->acl,dbconf->db_out,i);
      break;
    }
#endif
    case attr_xattrs : {
        xattr_node *xattr = NULL;
        size_t num = 0;
        
        if (!line->xattrs)
        {
          db_writelong(0, dbconf->db_out, i);
          break;
        }
        
        db_writelong(line->xattrs->num, dbconf->db_out, i);
        
        xattr = line->xattrs->ents;
        while (num < line->xattrs->num)
        {
          dofprintf(",");
          db_writechar(xattr->key, dbconf->db_out, 0);
          dofprintf(",");
          db_write_byte_base64(xattr->val, xattr->vsz, dbconf->db_out, 0, 1, 1);
          
          ++xattr;
          ++num;
        }
      break;
    }
    case attr_selinux : {
	db_write_byte_base64((byte*)line->cntx, 0, dbconf->db_out, i, 1, 1);
      break;
    }
#ifdef WITH_E2FSATTRS
    case attr_e2fsattrs : {
      db_writelong(line->e2fsattrs,dbconf->db_out,i);
      break;
    }
#endif
#ifdef WITH_CAPABILITIES
    case attr_capabilities : {
      db_write_byte_base64((byte*)line->capabilities, 0, dbconf->db_out, i, 1, 1);
      break;
    }
#endif
    default : {
      error(0,"Not implemented in db_writeline_file %i\n", i);
      return RETFAIL;
    }
    
    }
    
  }

  }

  dofprintf("\n");
  /* Can't use fflush because of zlib.*/
  dofflush();

  return RETOK;
}

int db_close_file(db_config* dbconf){
  
#ifdef WITH_MHASH
  byte* dig=NULL;
  char* digstr=NULL;

  if(dbconf->db_out
#ifdef WITH_ZLIB
     || dbconf->db_gzout
#endif
     ){

    /* Let's write @@end_db <checksum> */
    if (dbconf->dbnewmd!=NULL) {
      mhash(dbconf->dbnewmd, NULL ,0);
      dig=(byte*)malloc(sizeof(byte)*mhash_get_block_size(dbconf->dbhmactype));
      mhash_deinit(dbconf->dbnewmd,(void*)dig);
      digstr=encode_base64(dig,mhash_get_block_size(dbconf->dbhmactype));
      dbconf->do_dbnewmd=0;
      dofprintf("@@end_db %s\n",digstr);
      free(dig);
      free(digstr);
    } else {
      dofprintf("@@end_db\n");
    }
  }
#endif

#ifndef WITH_ZLIB
  if(fclose(dbconf->db_out)){
    error(0,"Unable to close database:%s\n",strerror(errno));
    return RETFAIL;
  }
#else
  if(dbconf->gzip_dbout){
    if(gzclose(dbconf->db_gzout)){
      error(0,"Unable to close gzdatabase:%s\n",strerror(errno));
      return RETFAIL;
    }
  }else {
    if(fclose(dbconf->db_out)){
      error(0,"Unable to close database:%s\n",strerror(errno));
      return RETFAIL;
    }
  }
#endif

  return RETOK;
}
// vi: ts=8 sw=8
