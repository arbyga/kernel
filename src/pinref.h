/**************************************************************************************

Copyright © 2008-2012 VMware, Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,  WITHOUT
WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
License for the specific language governing permissions and limitations
under the License.

Written by Mark Venguerov 2008 - 2012

**************************************************************************************/

/**
 * compressed PIN reference format descriptor
 */

#ifndef _PINREF_H_
#define _PINREF_H_

#include "affinity.h"
#include "utils.h"

using namespace AfyDB;

namespace AfyKernel
{

#define	XPINREFSIZE	64

/**
 * field flags
 */
#define PR_PID2		0x0001
#define	PR_ADDR		0x0002
#define	PR_ADDR2	0x0004
#define	PR_U1		0x0008
#define	PR_U2		0x0010
#define	PR_COUNT	0x0020
#define	PR_FCOLL	0x0040

/**
 * decompressed representation
 * used to assemble data for compression and to extract data after decomression
 */
struct PINRef
{
	const ushort	stID;
	PID				id;
	PID				id2;
	PageAddr		addr;
	PageAddr		addr2;
	uint32_t		u1;
	uint32_t		u2;
	uint32_t		count;
	uint16_t		def;

	PINRef(ushort storeID,const byte *p,size_t l);
	PINRef(ushort storeID,const PID& pid) : stID(storeID),id(pid),def(0) {}
	PINRef(ushort storeID,const PID& pid,const PageAddr& ad) : stID(storeID),id(pid),addr(ad),def(PR_ADDR) {}
	byte				enc(byte *buf) const;
	RC					dec(const byte *p,size_t l);
	static	bool		isColl(const byte *p,size_t l) {assert(l!=0); return (p[l-1]&0xA0)==0xA0;}
	static	void		changeFColl(byte *p,byte& l,bool fSet) {assert(l!=0); if ((*(p+=l-1)&0x80)==0) {if (fSet) *++p=0xA0,++l;} else if (fSet) *p|=0x20; else if ((*p&=~0x20)==0x80) --l;}
	static	uint32_t	getCount(const byte *p,size_t l) {assert(l!=0); uint32_t c=1; if (l!=0 && (*(p+=l-1)&0x90)==0x90) {if ((*p&0x40)!=0) --p; afy_dec32r(p,c);} return c;}
	static	RC			getPID(const byte *p,size_t l,ushort stID,PID& id,PageAddr *paddr=NULL);
	static	RC			adjustCount(byte *p,size_t& l,uint32_t cnt,byte *buf,bool fDec=false);
	static	int			cmpPIDs(const byte *p1,unsigned l1,const byte *p2,unsigned l2);
};

/**
 * compressed reference representaion
 */
struct EncPINRef
{
	mutable uint16_t	flags;
	byte				lref;
	byte				buf[XPINREFSIZE];
	template<int align> byte trunc() const {return (byte)ceil(int(sizeof(EncPINRef)-XPINREFSIZE+lref),align);}
	int					cmp(const EncPINRef& rhs) const {return PINRef::cmpPIDs(buf,lref,rhs.buf,rhs.lref);}
};

__forceinline int cmpPIDs(const PID& x,const PID& y) {int c=cmp3(x.pid&0xFFFFFFFFFFFFULL,y.pid&0xFFFFFFFFFFFFULL); if (c==0) c=cmp3((uint16_t)(x.pid>>48),(uint16_t)(y.pid>>48)); return c==0?cmp3(x.ident,y.ident):c;}
extern int __cdecl	cmpPIDs(const void *p1,const void *p2);

};

#endif
