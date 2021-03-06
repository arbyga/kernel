/**************************************************************************************

Copyright © 2004-2012 VMware, Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,  WITHOUT
WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
License for the specific language governing permissions and limitations
under the License.

Written by Mark Venguerov 2004-2012

**************************************************************************************/

#include "affinityimpl.h"
#include "queryprc.h"
#include "stmt.h"
#include "parser.h"
#include "expr.h"
#include "maps.h"
#include "blob.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

using namespace AfyKernel;

RC AfyKernel::copyV0(Value &v,MemAlloc *ma)
{
	try {
		ulong i; RC rc; Value w; size_t ll; assert(ma!=NULL);
		switch (v.type) {
		default: break;
		case VT_STRING: case VT_BSTR: case VT_URL: case VT_RESERVED2:
			if (v.str==NULL) break;
			ll=v.length+(v.type==VT_BSTR?0:1);
			if ((w.bstr=(byte*)ma->malloc(ll))==NULL) {v.type=VT_ERROR; return RC_NORESOURCES;}
			memcpy((byte*)w.bstr,v.bstr,v.length);
			if (v.type==VT_STRING||v.type==VT_URL) const_cast<char*>(w.str)[v.length]=0;
			v.bstr=w.bstr; v.flags=ma->getAType(); break;
		case VT_COLLECTION:
			if (v.nav!=NULL && (ma->getAType()!=SES_HEAP || (v.nav=v.nav->clone())==NULL)) {v.type=VT_ERROR; return RC_NORESOURCES;}
			v.flags=ma->getAType(); break;
		case VT_ARRAY: case VT_STRUCT:
			assert(v.varray!=NULL && v.length>0);
			if ((w.varray=(Value*)ma->malloc(v.length*sizeof(Value)))==NULL) {v.type=VT_ERROR; return RC_NORESOURCES;}
			for (i=0; i<v.length; i++)
				if ((rc=copyV(v.varray[i],const_cast<Value&>(w.varray[i]),ma))!=RC_OK) {freeV((Value*)w.varray,i,ma); v.type=VT_ERROR; return rc;}
			v.varray=w.varray; v.flags=ma->getAType(); break;
		case VT_RANGE:
			assert(v.range!=NULL && v.length==2);
			w.range=(Value*)ma->malloc(2*sizeof(Value));
			if (w.range==NULL) {v.type=VT_ERROR; return RC_NORESOURCES;}
			if ((rc=copyV(v.range[0],w.range[0],ma))!=RC_OK) {ma->free((Value*)w.range); v.type=VT_ERROR; return rc;}
			if ((rc=copyV(v.range[1],w.range[1],ma))!=RC_OK) {freeV(const_cast<Value&>(w.range[0])); ma->free((Value*)w.range); v.type=VT_ERROR; return rc;}
			v.range=w.range; v.flags=ma->getAType(); break;
		case VT_REFIDPROP: case VT_REFIDELT:
			w.refId=(RefVID*)ma->malloc(sizeof(RefVID));
			if (w.refId==NULL) {v.type=VT_ERROR; return RC_NORESOURCES;}
			*const_cast<RefVID*>(w.refId)=*v.refId; v.refId=w.refId; v.flags=ma->getAType(); break;
		case VT_STMT:
			if (v.stmt!=NULL && (v.stmt=((Stmt*)v.stmt)->clone(STMT_OP_ALL,ma,false))==NULL) {v.type=VT_ERROR; return RC_NORESOURCES;}
			v.flags=ma->getAType(); break;
		case VT_EXPR:
			if (v.expr!=NULL && (v.expr=Expr::clone((Expr*)v.expr,ma))==NULL) {v.type=VT_ERROR; return RC_NORESOURCES;}
			v.flags|=ma->getAType(); break;
		case VT_EXPRTREE:
			if (v.exprt!=NULL && (v.exprt=((ExprTree*)v.exprt)->clone())==NULL) {v.type=VT_ERROR; return RC_NORESOURCES;}
			v.flags=ma->getAType(); break;
		case VT_STREAM:
			v.stream.prefix=NULL;
			if (v.stream.is!=NULL && (ma->getAType()!=SES_HEAP || (v.stream.is=v.stream.is->clone())==NULL)) {v.type=VT_ERROR; return RC_NORESOURCES;}
			v.flags=ma->getAType(); break;
		}
		return RC_OK;
	} catch (RC rc) {return rc;} catch (...) {report(MSG_ERROR,"Exception in copyV(...)\n"); return RC_INTERNAL;}
}

RC AfyKernel::copyV(const Value *from,ulong nv,Value *&to,MemAlloc *ma)
{
	if (ma==NULL) ma=Session::getSession();
	if (from==NULL||nv==0) to=NULL;
	else if ((to=new(ma) Value[nv])==NULL) return RC_NORESOURCES;
	else for (ulong i=0; i<nv; i++) {
		RC rc=copyV(from[i],to[i],ma);
		if (rc!=RC_OK) {while (i--!=0) freeV(to[i]); ma->free(to); return rc;}
	}
	return RC_OK;
}

bool AfyKernel::operator==(const Value& lhs, const Value& rhs)
{
	ulong i;
	if (lhs.property!=rhs.property || lhs.eid!=rhs.eid || lhs.op!=rhs.op || lhs.meta!=rhs.meta) return false;		// flags???
	if (lhs.type!=rhs.type) switch (lhs.type) {
	default: return false;
	case VT_REF: return rhs.type==VT_REFID && lhs.pin->getPID()==rhs.id;
	case VT_REFID: return rhs.type==VT_REF && lhs.id==rhs.pin->getPID();
		//...
	}
	if (lhs.length!=rhs.length) return false;
	switch (lhs.type) {
	default: break;
	case VT_STRING: case VT_BSTR: case VT_URL: case VT_RESERVED2: return memcmp(lhs.bstr,rhs.bstr,lhs.length)==0;
	case VT_INT: case VT_UINT: case VT_URIID: case VT_IDENTITY: return lhs.ui==rhs.ui;
	case VT_INT64: case VT_UINT64: case VT_DATETIME: case VT_INTERVAL: return lhs.ui64==rhs.ui64;
	case VT_FLOAT: return lhs.f==rhs.f && lhs.qval.units==rhs.qval.units;
	case VT_DOUBLE: return lhs.d==rhs.d && lhs.qval.units==rhs.qval.units;
	case VT_BOOL: return lhs.b==rhs.b;
	case VT_REF: return lhs.pin->getPID()==rhs.pin->getPID();
	case VT_REFID: return lhs.id==rhs.id;
	case VT_REFPROP: return lhs.ref.pin->getPID()==rhs.ref.pin->getPID() && lhs.ref.pid==rhs.ref.pid && lhs.ref.vid==rhs.ref.vid;
	case VT_REFIDPROP: return lhs.refId->id==rhs.refId->id && lhs.refId->pid==rhs.refId->pid && lhs.refId->vid==rhs.refId->vid;
	case VT_REFELT: return lhs.ref.pin->getPID()==rhs.ref.pin->getPID() && lhs.ref.pid==rhs.ref.pid && lhs.ref.vid==rhs.ref.vid && lhs.ref.eid==rhs.ref.eid;
	case VT_REFIDELT: return lhs.refId->id==rhs.refId->id && lhs.refId->pid==rhs.refId->pid && lhs.refId->vid==rhs.refId->vid && lhs.refId->eid==rhs.refId->eid;
	case VT_STREAM: return lhs.stream.is==rhs.stream.is;
	case VT_EXPR: return false;		// ???
	case VT_STMT: return false;		// ???
	case VT_ARRAY: case VT_STRUCT:
		for (i=0; i<lhs.length; i++) if (lhs.varray[i]!=rhs.varray[i]) return false;
		break;
	case VT_COLLECTION:
		if (lhs.nav->count()!=rhs.nav->count()) return false;
		// ???
		return false;
	case VT_EXPRTREE: return *(ExprTree*)lhs.exprt==*(ExprTree*)rhs.exprt;
	case VT_VARREF:
		return lhs.refV.refN==rhs.refV.refN && lhs.refV.type==rhs.refV.type && ((lhs.flags^rhs.flags)&VAR_TYPE_MASK)==0 &&
			((lhs.flags&VAR_TYPE_MASK)!=0 || lhs.length==rhs.length && lhs.eid==rhs.eid && (lhs.length==0 || lhs.refV.id==rhs.refV.id));
		break;
	case VT_RANGE: return lhs.range[0]==rhs.range[0] && lhs.range[1]==rhs.range[1];
	}
	return true;
}

size_t AfyKernel::serSize(const PID& id)
{
	uint32_t pg=uint32_t(id.pid>>16); uint16_t idx=uint16_t(id.pid),st=uint16_t(id.pid>>48);
	return afy_len32(pg)+afy_len16(idx)+afy_len16(st)+afy_len32(id.ident);
}

size_t AfyKernel::serSize(const Value& v,bool full)
{
	size_t l=1; uint32_t i; uint64_t u64; const Value *pv;
	switch (v.type) {
	default: l=0; break;
	case VT_ANY: if (!full) l++; break;
	case VT_STREAM: u64=v.stream.is->length(); l+=1+afy_len64(u64)+(size_t)u64; break;
	case VT_STRING: case VT_URL: l+=afy_len32(v.length+1)+v.length+1; break;
	case VT_BSTR: l+=afy_len32(v.length)+v.length; break;
	case VT_INT: i=afy_enc32zz(v.i); l+=afy_len32(i); break;
	case VT_UINT: case VT_URIID: case VT_IDENTITY: l+=afy_len32(v.ui); break;
	case VT_INT64: case VT_INTERVAL: u64=afy_enc64zz(v.i64); l+=afy_len64(u64); break;
	case VT_UINT64: case VT_DATETIME: l+=afy_len64(v.ui64); break;
	case VT_FLOAT: l=2+sizeof(float); break;
	case VT_DOUBLE: l=2+sizeof(double); break;
	case VT_BOOL: case VT_CURRENT: l=2; break;
	case VT_REF: l+=serSize(v.pin->getPID()); break;
	case VT_REFID: l+=serSize(v.id); break;
	case VT_REFPROP: l+=serSize(v.ref.pin->getPID())+afy_len32(v.ref.pid); break;
	case VT_REFIDPROP: l+=serSize(v.refId->id)+afy_len32(v.refId->pid); break;
	case VT_REFELT: l+=serSize(v.ref.pin->getPID())+afy_len32(v.ref.pid)+afy_len32(v.ref.eid); break;
	case VT_REFIDELT: l+=serSize(v.refId->id)+afy_len32(v.refId->pid)+afy_len32(v.refId->eid); break;
	case VT_RANGE: l+=serSize(v.range[0])+serSize(v.range[1]); break;
	case VT_VARREF:
		l=7; if (v.length!=0) {l+=afy_len32(v.refV.id); if (v.eid!=STORE_COLLECTION_ID) l+=afy_len32(v.eid);}
		break;
	case VT_ARRAY:
		l+=afy_len32(v.length);
		for (i=0; i<v.length; i++) l+=afy_len32(v.varray[i].eid)+serSize(v.varray[i]);
		break;
	case VT_COLLECTION:
		l=v.nav->count(); l=1+afy_len32(l);
		for (pv=v.nav->navigate(GO_FIRST); pv!=NULL; pv=v.nav->navigate(GO_NEXT)) l+=afy_len32(pv->eid)+serSize(*pv);
		break;
	case VT_STRUCT:
		l+=afy_len32(v.length);
		for (i=0; i<v.length; i++) l+=afy_len32(v.varray[i].property)+serSize(v.varray[i]);
		break;
	case VT_EXPR: l+=((Expr*)v.expr)->serSize(); break;
	case VT_STMT: l=((Stmt*)v.stmt)->serSize(); l+=1+afy_len32(l); break;

	case VT_EXPRTREE:
	case VT_RESERVED1:
	case VT_RESERVED2:
		return 0;		// niy
	}
	if (full) {i=afy_enc32zz(v.eid); l+=2+afy_len32(i)+afy_len32(v.property);}
	return l;
}

byte *AfyKernel::serialize(const PID& id,byte *buf)
{
	uint32_t pg=uint32_t(id.pid>>16); uint16_t idx=uint16_t(id.pid),st=uint16_t(id.pid>>48);
	afy_enc32(buf,pg); afy_enc16(buf,idx); afy_enc16(buf,st); afy_enc32(buf,id.ident); return buf;
}

byte *AfyKernel::serialize(const Value& v,byte *buf,bool full)
{
	*buf++=v.type;
	unsigned i; uint32_t l; const Value *pv; uint64_t u64;
	switch (v.type) {
	default:
	case VT_ANY: if (!full) *buf++=v.meta; break;
	case VT_STREAM:
		*buf++=v.stream.is->dataType(); u64=v.stream.is->length();
		afy_enc64(buf,u64); buf+=v.stream.is->read(buf,(size_t)u64);
		break;
	case VT_STRING: case VT_URL: case VT_BSTR:
		l=v.type==VT_BSTR?v.length:v.length+1; afy_enc32(buf,l);
		if (v.bstr!=NULL) memcpy(buf,v.bstr,v.length); buf+=v.length;
		if (v.type!=VT_BSTR) *buf++='\0';
		break;
	case VT_INT: l=afy_enc32zz(v.i); afy_enc32(buf,l); break;
	case VT_UINT: case VT_URIID: case VT_IDENTITY: afy_enc32(buf,v.ui); break;
	case VT_INT64: case VT_INTERVAL: u64=afy_enc64zz(v.i64); afy_enc64(buf,u64); break;
	case VT_UINT64: case VT_DATETIME: afy_enc64(buf,v.ui64); break;
	case VT_FLOAT: *buf++=byte(v.qval.units); memcpy(buf,&v.f,sizeof(float)); buf+=sizeof(float); break;
	case VT_DOUBLE: *buf++=byte(v.qval.units); memcpy(buf,&v.d,sizeof(double)); buf+=sizeof(double); break;
	case VT_BOOL: *buf++=v.b; break;
	case VT_RANGE: buf=serialize(v.range[1],serialize(v.range[0],buf)); break;
	case VT_CURRENT: *buf++=byte(v.i); break;
	case VT_REF: buf[-1]=VT_REFID; buf=serialize(v.pin->getPID(),buf); break;
	case VT_REFPROP: buf[-1]=VT_REFIDPROP; buf=serialize(v.ref.pin->getPID(),buf); afy_enc32(buf,v.ref.pid); break;
	case VT_REFELT: buf[-1]=VT_REFIDELT; buf=serialize(v.ref.pin->getPID(),buf); afy_enc32(buf,v.ref.pid); afy_enc32(buf,v.ref.eid); break;
	case VT_REFID: buf=serialize(v.id,buf); break;
	case VT_REFIDPROP: buf=serialize(v.refId->id,buf); afy_enc32(buf,v.refId->pid); break;
	case VT_REFIDELT:  buf=serialize(v.refId->id,buf); afy_enc32(buf,v.refId->pid); afy_enc32(buf,v.refId->eid); break;
	case VT_VARREF:
		*buf++=v.refV.refN; *buf++=v.length|(v.eid!=STORE_COLLECTION_ID?0x80:0); 
		*buf++=byte(v.refV.type>>8); *buf++=byte(v.refV.type);
		*buf++=byte(v.refV.flags>>8); *buf++=byte(v.refV.flags);
		if (v.length!=0) {afy_enc32(buf,v.refV.id); if (v.eid!=STORE_COLLECTION_ID) afy_enc32(buf,v.eid);}
		break;
	case VT_ARRAY:
		afy_enc32(buf,v.length);
		for (i=0; i<v.length; i++) {afy_enc32(buf,v.varray[i].eid); buf=serialize(v.varray[i],buf);}
		break;
	case VT_COLLECTION:
		l=v.nav->count(); afy_enc32(buf,l);
		for (pv=v.nav->navigate(GO_FIRST); pv!=NULL; pv=v.nav->navigate(GO_NEXT)) {afy_enc32(buf,pv->eid); buf=serialize(*pv,buf);}
		break;
	case VT_STRUCT:
		afy_enc32(buf,v.length);
		for (i=0; i<v.length; i++) {afy_enc32(buf,v.varray[i].property); buf=serialize(v.varray[i],buf);}
		break;
	case VT_EXPR:
		if (v.expr!=NULL) buf=((Expr*)v.expr)->serialize(buf);
		break;
	case VT_STMT:
		if (v.stmt!=NULL) {l=(uint32_t)((Stmt*)v.stmt)->serSize(); afy_enc32(buf,l); buf=((Stmt*)v.stmt)->serialize(buf);}
		break;
	case VT_EXPRTREE:
	case VT_RESERVED1:
	case VT_RESERVED2:
		//???
		break;		// niy
	}
	if (full) {buf[0]=v.op; buf[1]=v.meta; buf+=2; l=afy_enc32zz(v.eid); afy_enc32(buf,l); afy_enc32(buf,v.property);}
	return buf;
}

RC AfyKernel::deserialize(PID& id,const byte *&buf,const byte *const ebuf)
{
	uint32_t u32; uint16_t u16;
	CHECK_dec32(buf,u32,ebuf); CHECK_dec16(buf,u16,ebuf); id.pid=uint64_t(u32)<<16|u16;
	CHECK_dec16(buf,u16,ebuf); id.setStoreID(u16); CHECK_dec32(buf,id.ident,ebuf); return RC_OK;
}

RC AfyKernel::deserialize(Value& val,const byte *&buf,const byte *const ebuf,MemAlloc *ma,bool fInPlace,bool full)
{
	if (buf==ebuf) return RC_CORRUPTED; assert(ma!=NULL);
	uint32_t l,i; uint64_t u64; RefVID *rv; Expr *exp; Stmt *qry; RC rc;
	val.type=(ValueType)*buf++; val.flags=NO_HEAP; val.meta=0; val.op=OP_SET;
	val.property=STORE_INVALID_PROPID; val.eid=STORE_COLLECTION_ID;
	switch (val.type) {
	default: return RC_CORRUPTED;
	case VT_ANY: if (!full) {if (buf>=ebuf) return RC_CORRUPTED; val.meta=*buf++;} break;
	case VT_STREAM:
		if (buf+1>ebuf) return RC_CORRUPTED;
		val.type=*buf++; CHECK_dec64(buf,u64,ebuf);
		if (fInPlace) val.bstr=buf;
		else if ((val.bstr=(byte*)ma->malloc((size_t)u64+1))==NULL) return RC_NORESOURCES;
		else {memcpy((byte*)val.bstr,buf,(size_t)u64); val.flags=ma->getAType(); if (val.type!=VT_BSTR) ((byte*)val.bstr)[(size_t)u64]=0;}
		val.length=(uint32_t)u64; buf+=(size_t)u64; break;
	case VT_STRING: case VT_URL: case VT_BSTR:
		CHECK_dec32(buf,l,ebuf); if (buf+l>ebuf) return RC_CORRUPTED;
		if (fInPlace) val.bstr=buf;
		else if ((val.bstr=(byte*)ma->malloc(l))==NULL) return RC_NORESOURCES;
		else {memcpy((byte*)val.bstr,buf,l); val.flags=ma->getAType();}
		val.length=val.type==VT_BSTR?l:l-1; buf+=l; break;
	case VT_INT:
		CHECK_dec32(buf,i,ebuf); val.i=afy_dec32zz(i); val.length=sizeof(int32_t); break;
	case VT_UINT: case VT_URIID: case VT_IDENTITY:
		CHECK_dec32(buf,val.ui,ebuf); val.length=sizeof(uint32_t); break;
	case VT_INT64: case VT_INTERVAL: 
		CHECK_dec64(buf,u64,ebuf); val.i64=afy_dec64zz(u64); val.length=sizeof(int64_t); break;
	case VT_UINT64: case VT_DATETIME:
		CHECK_dec64(buf,val.ui64,ebuf); val.length=sizeof(uint64_t); break;
	case VT_FLOAT:
		if (buf+sizeof(float)+1>ebuf) return RC_CORRUPTED;
		val.qval.units=*buf++; memcpy(&val.f,buf,sizeof(float));
		buf+=val.length=sizeof(float); break;
	case VT_DOUBLE: 
		if (buf+sizeof(double)+1>ebuf) return RC_CORRUPTED; 
		val.qval.units=*buf++; memcpy(&val.d,buf,sizeof(double));
		buf+=val.length=sizeof(double); break;
	case VT_BOOL:
		if (buf>=ebuf) return RC_CORRUPTED; val.b=*buf++!=0; val.length=1; break;
	case VT_REFID:
		if ((rc=deserialize(val.id,buf,ebuf))!=RC_OK) return rc;
		val.length=sizeof(PID); break;
	case VT_REFIDPROP: case VT_REFIDELT:
		rv=(RefVID*)ma->malloc(sizeof(RefVID)); if (rv==NULL) return RC_NORESOURCES;
		val.flags=ma->getAType(); val.refId=rv;
		if ((rc=deserialize(rv->id,buf,ebuf))!=RC_OK) return rc;
		CHECK_dec32(buf,rv->pid,ebuf);
		if (val.type==VT_REFIDELT) CHECK_dec32(buf,rv->eid,ebuf);
		val.length=1; break;
	case VT_VARREF:
		if (ebuf-buf<6) return RC_CORRUPTED;
		val.refV.refN=buf[0]; val.length=buf[1]; val.refV.type=buf[2]<<8|buf[3]; val.refV.flags=buf[4]<<8|buf[5]; buf+=6;
		if (val.length!=0) {
			bool fE=(val.length&0x80)!=0; if ((val.length&=0x7F)!=1) return RC_CORRUPTED;
			CHECK_dec32(buf,val.refV.id,ebuf); if (fE) {CHECK_dec32(buf,val.eid,ebuf);}
		}
		break;
	case VT_CURRENT:
		if (buf>=ebuf) return RC_CORRUPTED; val.i=*buf++; val.length=1; break;
	case VT_RANGE:
		if ((val.range=(Value*)ma->malloc(sizeof(Value)*2))==NULL) return RC_NORESOURCES;
		val.length=2; val.flags=ma->getAType();
		if ((rc=deserialize(*(Value*)&val.range[0],buf,ebuf,ma,fInPlace))!=RC_OK ||
			(rc=deserialize(*(Value*)&val.range[1],buf,ebuf,ma,fInPlace))!=RC_OK) return rc;
		break;
	case VT_ARRAY:
		CHECK_dec32(buf,val.length,ebuf); if (val.length==0) return RC_CORRUPTED;
		if ((val.varray=(Value*)ma->malloc(sizeof(Value)*val.length))==NULL) return RC_NORESOURCES;
		else for (i=0; i<val.length; i++) {
			uint32_t eid; CHECK_dec32(buf,eid,ebuf);
			if ((rc=deserialize(*(Value*)&val.varray[i],buf,ebuf,ma,fInPlace))!=RC_OK) return rc;
			((Value*)&val.varray[i])->eid=eid;
		}
		val.flags=ma->getAType(); break;
	case VT_STRUCT:
		CHECK_dec32(buf,val.length,ebuf); if (val.length==0) return RC_CORRUPTED;
		if ((val.varray=(Value*)ma->malloc(sizeof(Value)*val.length))==NULL) return RC_NORESOURCES;
		else for (i=0; i<val.length; i++) {
			uint32_t propID; CHECK_dec32(buf,propID,ebuf);
			if ((rc=deserialize(*(Value*)&val.varray[i],buf,ebuf,ma,fInPlace))!=RC_OK) return rc;
			((Value*)&val.varray[i])->property=propID;
		}
		val.flags=ma->getAType(); break;
	case VT_EXPR:
		if ((rc=Expr::deserialize(exp,buf,ebuf,ma))!=RC_OK) return rc;
		val.expr=exp; val.flags=ma->getAType(); val.length=1; break;
	case VT_STMT:
		CHECK_dec32(buf,val.length,ebuf);
		if ((rc=Stmt::deserialize(qry,buf,buf+val.length,ma))!=RC_OK) return rc;
		val.stmt=qry; val.flags=ma->getAType(); break;
	case VT_RESERVED1:
	case VT_RESERVED2:
		return RC_CORRUPTED;	// niy
	}
	if (full) {
		if (buf+4>ebuf) return RC_CORRUPTED; val.op=buf[0]; val.meta=buf[1]; buf+=2;
		CHECK_dec32(buf,i,ebuf); val.eid=afy_dec32zz(i); CHECK_dec32(buf,val.property,ebuf);
	}
	return RC_OK;
}

RC AfyKernel::streamToValue(IStream *stream,Value& val,MemAlloc *ma)
{
	try {
		val.setError();
		if (ma==NULL && (ma=Session::getSession())==NULL && (ma=StoreCtx::get())==NULL) return RC_NOSESSION;
		val.type=stream->dataType(); val.flags=ma->getAType();
		byte buf[256],*p; size_t l=stream->read(buf,sizeof(buf)),xl=1024,extra=val.type==VT_BSTR?0:1; RC rc;
		if ((p=(byte*)ma->malloc(l>=sizeof(buf)?xl:l+extra))==NULL) return RC_NORESOURCES;
		memcpy(p,buf,l);
		if (l>=sizeof(buf)) {
			while ((l+=stream->read(p+l,xl-l))>=xl) if ((p=(byte*)ma->realloc(p,xl+=xl/2))==NULL) return RC_NORESOURCES;
			if (l+extra!=xl && (p=(byte*)ma->realloc(p,l+extra))==NULL) return RC_NORESOURCES;
		}
		switch (val.type) {
		case VT_STRING: p[l]=0;
		default: val.length=(uint32_t)l; val.bstr=p; break;
		case VT_EXPR:
			if (l<sizeof(ExprHdr)) {ma->free(p); return RC_CORRUPTED;}
			{ExprHdr ehdr(0,0,0,0,0,0); Expr *exp; memcpy(&ehdr,p,sizeof(ExprHdr)); const byte *pp=p;
			rc=Expr::deserialize(exp,pp,pp+ehdr.lExpr,ma); ma->free(p); if (rc!=RC_OK) return rc;
			val.set(exp); break;}
		case VT_STMT:
			{const byte *pp=p; Stmt *stmt;
			rc=Stmt::deserialize(stmt,pp,pp+l,ma); ma->free(p); if (rc!=RC_OK) return rc;
			val.set(stmt); break;}
		}
		return RC_OK;
	} catch (RC rc) {return rc;} catch (...) {return RC_INVPARAM;}
}

int AfyKernel::cmpNoConv(const Value& arg,const Value& arg2,ulong u)
{
	ulong len; int c; Value v1,v2; assert(arg.type==arg2.type);
	switch (arg.type) {
	default: return -3;
	case VT_INT: return cmp3(arg.i,arg2.i);
	case VT_UINT: return cmp3(arg.ui,arg2.ui);
	case VT_INTERVAL:
	case VT_INT64: return cmp3(arg.i64,arg2.i64);
	case VT_DATETIME:
	case VT_UINT64: return cmp3(arg.ui64,arg2.ui64);
	case VT_FLOAT:
		if (arg.qval.units==arg2.qval.units) return cmp3(arg.f,arg2.f);
		v1.qval.d=arg.f; v1.qval.units=arg.qval.units; v2.qval.d=arg2.f; v2.qval.units=arg2.qval.units; 
		return !compatible(v1.qval,v2.qval)?-3:cmp3(v1.qval.d,v2.qval.d);
	case VT_DOUBLE:
		if (arg.qval.units==arg2.qval.units) return cmp3(arg.d,arg2.d);
		v1.qval=arg.qval; v2.qval=arg2.qval; return !compatible(v1.qval,v2.qval)?-3:cmp3(v1.qval.d,v2.qval.d);
	case VT_BOOL: return arg.b==arg2.b?0:(u&CND_SORT)!=0?arg.b<arg2.b?-1:1:(u&CND_NE)!=0?-1:-2;
	case VT_URIID: return arg.uid==arg2.uid?0:(u&CND_SORT)!=0?arg.uid<arg2.uid?-1:1:(u&CND_NE)!=0?-1:-2;
	case VT_IDENTITY: return arg.iid==arg2.iid?0:(u&CND_SORT)!=0?arg.iid<arg2.iid?-1:1:(u&CND_NE)!=0?-1:-2;
	case VT_STRING:
		if (testStrNum(arg.str,arg.length,v1) && testStrNum(arg2.str,arg2.length,v2)) return cmp(v1,v2,u);
	case VT_URL: case VT_BSTR:
		if (arg.str==NULL||arg.length==0) return arg2.str==NULL||arg2.length==0?0:-1;
		if (arg2.str==NULL||arg2.length==0) return 1;
		len=arg.length<=arg2.length?arg.length:arg2.length;
		c=sign(arg.type==VT_BSTR||(u&(CND_EQ|CND_NE))!=0&&(u&CND_NCASE)==0?memcmp(arg.bstr,arg2.bstr,len):
					(u&CND_NCASE)!=0?strncasecmp(arg.str,arg2.str,len):strncmp(arg.str,arg2.str,len));
		return c!=0?c:cmp3(arg.length,arg2.length);
	case VT_REF: return (u&CND_SORT)!=0?cmpPIDs(arg.pin->getPID(),arg2.pin->getPID()):arg.pin->getPID()==arg2.pin->getPID()?0:(u&CND_NE)!=0?-1:-2;
	case VT_REFPROP: return arg.ref.pin->getPID()==arg2.ref.pin->getPID()&&arg.ref.pid==arg.ref.pid?0:(u&CND_NE)!=0?-1:-2;								// CND_SORT
	case VT_REFELT: return arg.ref.pin->getPID()==arg2.ref.pin->getPID()&&arg.ref.pid==arg.ref.pid&&arg.ref.eid==arg2.ref.eid?0:(u&CND_NE)!=0?-1:-2;	// CND_SORT
	case VT_REFID: return (u&CND_SORT)!=0?cmpPIDs(arg.id,arg2.id):arg.id==arg2.id?0:(u&CND_NE)!=0?-1:-2;
	case VT_REFIDPROP: return arg.refId->id==arg2.refId->id&&arg.refId->pid==arg2.refId->pid?0:(u&CND_NE)!=0?-1:-2;										// CND_SORT
	case VT_REFIDELT:return arg.refId->id==arg2.refId->id&&arg.refId->pid==arg2.refId->pid&&arg.refId->eid==arg2.refId->eid?0:(u&CND_NE)!=0?-1:-2;		// CND_SORT
	}
}

int AfyKernel::cmpConv(const Value& arg,const Value& arg2,ulong u)
{
	Value val1,val2; const Value *pv1=&arg,*pv2=&arg2; assert(arg.type!=arg2.type);
	if ((isNumeric((ValueType)arg.type) || arg.type==VT_STRING && (pv1=&val1,testStrNum(arg.str,arg.length,val1))) && (isNumeric((ValueType)arg2.type) || arg2.type==VT_STRING && (pv2=&val2,testStrNum(arg2.str,arg2.length,val2)))) {
		const bool fRev=pv1->type>pv2->type; if (fRev) {const Value *pv=pv1; pv1=pv2; pv2=pv;}
		if (pv1->type!=pv2->type) {
			if (pv2->type>VT_DOUBLE) return cmp3(pv1->type,pv2->type);
			if (pv2->type>=VT_FLOAT) {
				if (pv2->type==VT_FLOAT) {val2.type=VT_DOUBLE; val2.d=pv2->f; val2.qval.units=pv2->qval.units; pv2=&val2;}
				if (pv1->type==VT_FLOAT) val1.d=pv1->f,val1.qval.units=pv1->qval.units;
				else {if (pv1->type==VT_INT) val1.d=pv1->i; else if (pv1->type==VT_UINT) val1.d=pv1->ui; else if (pv1->type==VT_INT64) val1.d=(double)pv1->i64; else val1.d=(double)pv1->ui64; val1.qval.units=Un_NDIM;} 
			} else if (pv2->type==VT_INT64) {
				if (pv1->type==VT_INT) val1.i64=pv1->i; else if (pv2->i64>=0) val1.i64=pv1->ui; else return fRev?-1:1;
			} else if (pv1->type==VT_INT64) {
				if (pv1->i64<0) return fRev?1:-1; val1.ui64=pv1->i64;
			} else if (pv1->type==VT_INT) {
				if (pv1->i<0) return fRev?1:-1; if (pv2->type==VT_UINT) val1.ui=pv1->i; else val1.ui64=pv1->i;
			} else {
				assert(pv1->type==VT_UINT && pv2->type==VT_UINT64); val1.ui64=pv1->ui;
			}
			val1.type=pv2->type; pv1=&val1;
		}
		int cmp=cmpNoConv(*pv1,*pv2,u); return fRev?-cmp:cmp;
	}
	if (isString((ValueType)arg.type) && isString((ValueType)arg2.type)) {
		int c=sign(memcmp(arg.bstr,arg2.bstr,min(arg.length,arg2.length))); return c==0?cmp3(arg.length,arg2.length):c;
	} else {
		//???
	}
	return cmp3(arg.type,arg2.type);
}

RC AfyKernel::convV(const Value& src,Value& dst,ushort type,MemAlloc *ma,unsigned mode)
{
	int l; char buf[256],*p; Value w; RC rc; TIMESTAMP ts; int64_t itv; URI *uri; Identity *ident; IdentityID iid; uint32_t ui;
	for (const Value *ps=&src;;) {if (ps->type==type) {
noconv:
		if (ps!=&dst) {if ((rc=copyV(*ps,dst,ma))!=RC_OK) {dst.setError(src.property); return rc;}}
	} else if (type>=VT_ALL) {
		if (((type&0xFF)!=VT_DOUBLE && (type&0xFF)!=VT_FLOAT || (type>>8)>=Un_ALL)) return RC_INVPARAM;
		if (src.type==VT_DOUBLE || src.type==VT_FLOAT) {dst=src; if (dst.type==VT_FLOAT) {dst.d=dst.f; dst.type=VT_DOUBLE;}}
		else if ((rc=convV(src,dst,VT_DOUBLE,ma,mode))!=RC_OK) return rc;
		if ((rc=convUnits(dst.qval,(Units)(type>>8)))!=RC_OK) return rc;
		if ((type&0xFF)==VT_FLOAT) {dst.f=(float)dst.d; dst.type=VT_FLOAT;}
		return RC_OK;
	} else if ((mode&CV_NODEREF)==0 && isRef((ValueType)ps->type) && type!=VT_URL && !isRef((ValueType)type)) {
		if ((rc=derefValue(*ps,dst,Session::getSession()))!=RC_OK) return rc;
		ps=&dst; continue;
	} else {
		if (ps!=&dst) {dst.eid=ps->eid; dst.flags=NO_HEAP;}
		if ((mode&CV_NOTRUNC)!=0 && isInteger((ValueType)type)) switch (src.type) {
		case VT_FLOAT: if (::floor(src.f)!=src.f) return RC_TYPE; break;
		case VT_DOUBLE: if (::floor(src.d)!=src.d) return RC_TYPE; break;
		}
		switch (type) {
		default: return RC_TYPE;
		case VT_STRING:
			switch (ps->type) {
			case VT_URL: goto noconv;
			case VT_INT:
				if ((l=sprintf(buf,"%d",ps->i))<0) return RC_INTERNAL;
				if ((p=(char*)ma->malloc(l+1))==NULL) return RC_NORESOURCES;
				memcpy(p,buf,l+1); dst.set(p,(unsigned long)l); dst.flags=ma->getAType(); break;
			case VT_UINT:
				ui=ps->ui;
			ui_to_str:
				if ((l=sprintf(buf,"%u",ui))<0) return RC_INTERNAL;
				if ((p=(char*)ma->malloc(l+1))==NULL) return RC_NORESOURCES;
				memcpy(p,buf,l+1); dst.set(p,(unsigned long)l); dst.flags=ma->getAType(); break;
			case VT_INT64:
				if ((l=sprintf(buf,_LD_FM,ps->i64))<0) return RC_INTERNAL;
				if ((p=(char*)ma->malloc(l+1))==NULL) return RC_NORESOURCES;
				memcpy(p,buf,l+1); dst.set(p,(unsigned long)l); dst.flags=ma->getAType(); break;
			case VT_UINT64:
				if ((l=sprintf(buf,_LU_FM,ps->ui64))<0) return RC_INTERNAL;
				if ((p=(char*)ma->malloc(l+1))==NULL) return RC_NORESOURCES;
				memcpy(p,buf,l+1); dst.set(p,(unsigned long)l); dst.flags=ma->getAType(); break;
			case VT_FLOAT:
				if ((l=sprintf(buf,"%g",ps->f))<0) return RC_INTERNAL;
				if ((p=(char*)ma->malloc(l+1))==NULL) return RC_NORESOURCES;
				memcpy(p,buf,l+1); dst.set(p,(unsigned long)l); dst.flags=ma->getAType(); break;
			case VT_DOUBLE:
				if ((l=sprintf(buf,"%g",ps->d))<0) return RC_INTERNAL;
				if ((p=(char*)ma->malloc(l+1))==NULL) return RC_NORESOURCES;
				memcpy(p,buf,l+1); dst.set(p,(unsigned long)l); dst.flags=ma->getAType(); break;
			case VT_BOOL: 
				if (ps->b) dst.set("true",4); else dst.set("false",5); break;
			case VT_STREAM:
				if ((rc=streamToValue(ps->stream.is,w,ma))!=RC_OK) return rc;
				if (w.type==VT_BSTR) {freeV(w); return RC_TYPE;}
				if (ps==&dst) freeV(dst); dst=w;
				if (dst.type!=type) {ps=&dst; continue;}
				break;
			case VT_CURRENT:
				switch (ps->i) {
				default: return RC_CORRUPTED;
				case CVT_TIMESTAMP: getTimestamp(ts); goto dt_to_str;
				case CVT_USER: iid=Session::getSession()->getIdentity(); goto ident_to_str;
				case CVT_STORE: ui=StoreCtx::get()->storeID; goto ui_to_str;
				}
			case VT_DATETIME:
				ts=ps->ui64;
			dt_to_str:
				if ((rc=convDateTime(Session::getSession(),ts,buf,l))!=RC_OK) return rc;
				if ((p=(char*)ma->malloc(l+1))==NULL) return RC_NORESOURCES;
				memcpy(p,buf,l+1); dst.set(p,(unsigned long)l); dst.flags=ma->getAType(); break;
			case VT_INTERVAL:
				if ((rc=convInterval(ps->i64,buf,l))!=RC_OK) return rc;
				if ((p=(char*)ma->malloc(l+1))==NULL) return RC_NORESOURCES;
				memcpy(p,buf,l+1); dst.set(p,(unsigned long)l); dst.flags=ma->getAType(); break;
				break;
			case VT_URIID:
				if (ps->uid==STORE_INVALID_PROPID) dst.set("",0);
				else if ((uri=(URI*)StoreCtx::get()->uriMgr->ObjMgr::find(ps->uid))==NULL) return RC_NOTFOUND;
				else {dst.set(strdup(uri->getName(),ma)); dst.flags=ma->getAType(); uri->release();}
				break;
			case VT_IDENTITY:
				iid=ps->iid;
			ident_to_str:
				if (iid==STORE_INVALID_IDENTITY) dst.set("",0);
				else if ((ident=(Identity*)StoreCtx::get()->identMgr->ObjMgr::find(iid))==NULL) return RC_NOTFOUND;
				else {dst.set(strdup(ident->getName(),ma)); dst.flags=ma->getAType(); ident->release();}
				break;
			case VT_STMT:
				if ((p=ps->stmt->toString())==NULL) return RC_NORESOURCES;
				if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				dst.set(p); dst.flags=SES_HEAP; break;
			case VT_EXPR:
				if ((p=ps->expr->toString())==NULL) return RC_NORESOURCES;
				if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				dst.set(p); dst.flags=SES_HEAP; break;
			default:
				{SOutCtx so(Session::getSession()); if ((rc=so.renderValue(src))!=RC_OK) return rc;
				if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				size_t l; char *p=(char*)so.result(l); dst.set(p,(uint32_t)l); dst.flags=SES_HEAP;}
				break;
			}
			break;
		case VT_URL:
			switch (ps->type) {
			default: return RC_TYPE;
			case VT_STRING: 
				// check format
				break;
			case VT_REF:
			case VT_REFID:
			case VT_REFPROP:
			case VT_REFELT:
			case VT_REFIDPROP:
			case VT_REFIDELT:
				// ???
				return RC_INTERNAL;
			}
			break;
		case VT_BSTR:
			switch (ps->type) {
			default: return RC_TYPE;
			case VT_STRING: case VT_URL: case VT_BSTR: goto noconv;
			case VT_INT: case VT_UINT: case VT_INT64: case VT_UINT64: case VT_FLOAT: case VT_DOUBLE:
			case VT_BOOL: case VT_DATETIME: case VT_INTERVAL:
				if ((p=(char*)ma->malloc(ps->length))==NULL) return RC_NORESOURCES;
				memcpy(p,&ps->i,ps->length); dst.set((unsigned char*)p,ps->length); dst.flags=ma->getAType(); break;
			case VT_STREAM:
				if ((rc=streamToValue(ps->stream.is,w,ma))!=RC_OK) return rc;
				if (ps==&dst) freeV(dst); dst=w; break;
			case VT_CURRENT:		//????
				if ((p=(char*)ma->malloc(sizeof(TIMESTAMP)))==NULL) return RC_NORESOURCES;
				getTimestamp(*(TIMESTAMP*)p); dst.set((unsigned char*)p,sizeof(TIMESTAMP)); dst.flags=ma->getAType(); break;
			case VT_RESERVED1:
			case VT_RESERVED2:
				return RC_INTERNAL;
			case VT_STRUCT:
				//???
				return RC_INTERNAL;
			}
			break;
		case VT_INT:
			switch (ps->type) {
			default: return RC_TYPE;
			case VT_UINT: goto noconv;
			case VT_STRING: 
				if ((rc=strToNum(ps->str,ps->length,w))!=RC_OK) return rc;
				if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				if (w.type!=VT_INT&&w.type!=VT_UINT) {ps=&w; continue;}
				dst=w; break;
			case VT_INT64: dst.i=int32_t(ps->i64); dst.length=sizeof(int32_t); break;
			case VT_UINT64: dst.i=int32_t(ps->ui64); dst.length=sizeof(int32_t); break;
			case VT_FLOAT: dst.i=int32_t(ps->f); dst.length=sizeof(int32_t); break;
			case VT_DOUBLE: dst.i=int32_t(ps->d); dst.length=sizeof(int32_t); break;
			}
			break;
		case VT_UINT:
			switch (ps->type) {
			default: return RC_TYPE;
			case VT_INT: goto noconv;
			case VT_STRING:
				if ((rc=strToNum(ps->str,ps->length,w))!=RC_OK) return rc;
				if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				if (w.type!=VT_INT&&w.type!=VT_UINT) {ps=&w; continue;}
				dst=w; break;
			case VT_INT64: dst.ui=uint32_t(ps->i64); dst.length=sizeof(uint32_t); break;
			case VT_UINT64: dst.ui=uint32_t(ps->ui64); dst.length=sizeof(uint32_t); break;
			case VT_FLOAT: dst.ui=uint32_t(ps->f); dst.length=sizeof(uint32_t); break;
			case VT_DOUBLE: dst.ui=uint32_t(ps->d); dst.length=sizeof(uint32_t); break;
			}
			break;
		case VT_INT64:
			switch (ps->type) {
			default: return RC_TYPE;
			case VT_UINT64: goto noconv;
			case VT_STRING:
				if ((rc=strToNum(ps->str,ps->length,w))!=RC_OK) return rc;
				if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				if (w.type!=VT_INT64&&w.type!=VT_UINT64) {ps=&w; continue;}
				dst=w; break;
			case VT_INT: dst.i64=int64_t(ps->i); dst.length=sizeof(int64_t); break;
			case VT_UINT: dst.i64=int64_t(ps->ui); dst.length=sizeof(int64_t); break;
			case VT_FLOAT: dst.i64=int64_t(ps->f); dst.length=sizeof(int64_t); break;
			case VT_DOUBLE: dst.i64=int64_t(ps->d); dst.length=sizeof(int64_t); break;
			}
			break;
		case VT_UINT64:
			switch (ps->type) {
			default: return RC_TYPE;
			case VT_INT64: goto noconv;
			case VT_STRING:
				if ((rc=strToNum(ps->str,ps->length,w))!=RC_OK) return rc;
				if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				if (w.type!=VT_INT64&&w.type!=VT_UINT64) {ps=&w; continue;}
				dst=w; break;
			case VT_INT: dst.ui64=uint64_t(ps->i); dst.length=sizeof(uint64_t); break;
			case VT_UINT: dst.ui64=uint64_t(ps->ui); dst.length=sizeof(uint64_t); break;
			case VT_FLOAT: dst.ui64=uint64_t(ps->f); dst.length=sizeof(uint64_t); break;
			case VT_DOUBLE: dst.ui64=uint64_t(ps->d); dst.length=sizeof(uint64_t); break;
			}
			break;
		case VT_FLOAT:
			dst.qval.units=Un_NDIM;
			switch (ps->type) {
			default: return RC_TYPE;
			case VT_STRING:
				if ((rc=strToNum(ps->str,ps->length,w))!=RC_OK) return rc;
				if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				if (w.type!=VT_FLOAT) {ps=&w; continue;}
				dst=w; break;
			case VT_INT: dst.f=float(ps->i); dst.length=sizeof(float); break;
			case VT_UINT: dst.f=float(ps->ui); dst.length=sizeof(float); break;
			case VT_INT64: dst.f=float(ps->i64); dst.length=sizeof(float); break;
			case VT_UINT64: dst.f=float(ps->ui64); dst.length=sizeof(float); break;
			case VT_DOUBLE: dst.f=float(ps->d); dst.qval.units=ps->qval.units; dst.length=sizeof(float); break;
			}
			break;
		case VT_DOUBLE:
			dst.qval.units=Un_NDIM;
			switch (ps->type) {
			default: return RC_TYPE;
			case VT_STRING:
				if ((rc=strToNum(ps->str,ps->length,w))!=RC_OK) return rc;
				if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				if (w.type!=VT_DOUBLE) {ps=&w; continue;}
				dst=w; break;
			case VT_INT: dst.d=double(ps->i); dst.length=sizeof(double); break;
			case VT_UINT: dst.d=double(ps->ui); dst.length=sizeof(double); break;
			case VT_INT64: dst.d=double(ps->i64); dst.length=sizeof(double); break;
			case VT_UINT64: dst.d=double(ps->ui64); dst.length=sizeof(double); break;
			case VT_FLOAT: dst.d=double(ps->f); dst.qval.units=ps->qval.units; dst.length=sizeof(double); break;
			}
			break;
		case VT_REF:
			switch (ps->type) {
			default: return RC_TYPE;
			case VT_REF: goto noconv;
			//case VT_URL:
				// ...
			//	if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				// ...
			//	break;
			//case VT_REFID:
				// ???
			//	break;
			//case VT_STRUCT:
			}
			break;
		case VT_REFID:
			switch (ps->type) {
			default: return RC_TYPE;
			case VT_REFID: goto noconv;
			//case VT_URL:
				// ...
			//	if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				// ...
			//	break;
			case VT_REF:
				w.id=ps->pin->getPID(); if (&dst==ps) freeV(dst);
				dst.id=w.id; dst.flags=NO_HEAP; break;
			//case VT_STRUCT:
			}
			break;
		case VT_BOOL:
			if (ps->type!=VT_STRING) return RC_TYPE;
			if (ps->length==4 && cmpncase(ps->str,"TRUE",4)==0) w.b=true;
			else if (ps->length==5 && cmpncase(ps->str,"FALSE",5)==0) w.b=false;
			else return RC_TYPE;
			if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
			dst.b=w.b; dst.length=1; dst.flags=NO_HEAP; break;
			break;
		case VT_DATETIME:
			switch (ps->type) {
			default: return RC_TYPE;
			case VT_UINT64: goto noconv;
			case VT_STRING:
				if ((rc=strToTimestamp(ps->str,ps->length,ts))!=RC_OK) return rc;
				if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				dst.ui64=ts; break;
			case VT_CURRENT:
				getTimestamp(ts); dst.ui64=ts; break;
			}
			break;
		case VT_INTERVAL:
			switch (ps->type) {
			default: return RC_TYPE;
			case VT_INT64: goto noconv;
			case VT_STRING:
				if ((rc=strToInterval(ps->str,ps->length,itv))!=RC_OK) return rc;
				if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				dst.i64=itv; break;
			}
			break;
		case VT_URIID:
			if (ps->type!=VT_STRING && ps->type!=VT_URL) return RC_TYPE;
			uri=StoreCtx::get()->uriMgr->insert(ps->str);
			if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
			dst.setURIID(uri!=NULL?uri->getID():STORE_INVALID_PROPID);
			if (uri!=NULL) uri->release(); break;
		case VT_IDENTITY:
			if (ps->type!=VT_STRING) return RC_TYPE;
			ident=(Identity*)StoreCtx::get()->identMgr->find(ps->str);
			if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
			dst.setIdentity(ident!=NULL?ident->getID():STORE_INVALID_IDENTITY);
			if (ident!=NULL) ident->release(); break;
		case VT_ARRAY:
			switch (ps->type) {
			default: return RC_TYPE;
			case VT_STRING:
				// ...
			//	if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				// ...
			//	break;
				return RC_TYPE;
			//case VT_COLLECTION:
				// ???
			//	break;
			}
			break;
		case VT_COLLECTION:
			switch (ps->type) {
			default: return RC_TYPE;
			case VT_STRING:
				// ...
			//	if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				// ...
			//	break;
				return RC_TYPE;
			//case VT_ARRAY:
				// ...
			//	break;
			}
			break;
		case VT_STRUCT:
			switch (ps->type) {
			default: return RC_TYPE;
			case VT_STRING:
				// ...
			//	if (&dst==ps && ps->flags!=NO_HEAP) freeV(dst);
				// ...
			//	break;
				return RC_TYPE;
			}
			break;
		case VT_STMT:
			if (ps->type==VT_STRING) {
				Session *ses=Session::getSession(); if (ses==NULL) return RC_NOSESSION; RC rc=RC_OK;
				SInCtx in(ses,ps->str,ps->length,NULL,0,(ses->getItf()&ITF_SPARQL)!=0?SQ_SPARQL:SQ_PATHSQL);
				try {Stmt *st=in.parseStmt(); if (&src==&dst) freeV(dst); dst.set(st); dst.flags=SES_HEAP; return RC_OK;}
				catch (SynErr) {rc=RC_SYNTAX;} catch (RC rc2) {rc=rc2;}
				return rc;
			}
			return RC_TYPE;
		case VT_EXPR:
			if (ps->type==VT_STRING) {
				Session *ses=Session::getSession(); if (ses==NULL) return RC_NOSESSION;
				SInCtx in(ses,ps->str,ps->length,NULL,0); Expr *pe=NULL;
				try {
					ExprTree *et=in.parse(false); in.checkEnd(); rc=Expr::compile(et,pe,ses,false); et->destroy(); 
					if (rc!=RC_OK) return rc; if (&src==&dst) freeV(dst);
					dst.set(pe); dst.flags=SES_HEAP; return RC_OK;
				} catch (SynErr) {return RC_SYNTAX;} catch (RC rc) {return rc;}
			}
			return RC_TYPE;
		}
	}
	dst.type=(ValueType)type;
	return RC_OK;}
}

RC AfyKernel::derefValue(const Value &src,Value &dst,Session *ses)
{
	PIN *pin; const RefVID *refId; const Value *cv;
	RC rc=RC_OK; HEAP_TYPE save=(HEAP_TYPE)(src.flags&HEAP_TYPE_MASK);
	switch (src.type) {
	default: return RC_TYPE;
	case VT_REF:
		pin=(PIN*)src.pin; 
		if ((rc=pin->getPINValue(dst,ses))==RC_OK && &src==&dst && save!=NO_HEAP) pin->destroy();
		break;
	case VT_REFID:
		rc=ses!=NULL?ses->getStore()->queryMgr->getPINValue(src.id,dst,0,ses):RC_NOSESSION; break;
	case VT_REFPROP:
	case VT_REFELT:
		pin=(PIN*)src.ref.pin;
		rc=ses!=NULL?ses->getStore()->queryMgr->loadValue(ses,pin->getPID(),src.ref.pid,src.type==VT_REFELT?src.ref.eid:STORE_COLLECTION_ID,dst,0):RC_NOSESSION;
		if (rc==RC_OK && &src==&dst && save!=NO_HEAP) pin->destroy();
		break;
	case VT_REFIDPROP:
	case VT_REFIDELT:
		refId=src.refId;
		rc=ses!=NULL?ses->getStore()->queryMgr->loadValue(ses,refId->id,refId->pid,src.type==VT_REFIDELT?refId->eid:STORE_COLLECTION_ID,dst,0):RC_NOSESSION;
		if (rc==RC_OK && &src==&dst && save!=NO_HEAP) if (save==SES_HEAP) ses->free((void*)refId); else free((void*)refId,save);
		break;
	case VT_STRUCT:
		rc=(cv=BIN<Value,PropertyID,ValCmp,uint32_t>::find(PROP_SPEC_REFID,src.varray,src.length))==NULL?RC_TYPE:
								ses!=NULL?ses->getStore()->queryMgr->getPINValue(cv->id,dst,0,ses):RC_NOSESSION;
		break;
	}
	return rc;
}

RC AfyKernel::convURL(const Value& src,Value& dst,HEAP_TYPE alloc)
{
	switch (src.type) {
	default: return RC_TYPE;
	case VT_URL:
		//...
		break;
	case VT_REF:
		//...
		break;
	case VT_REFID:
		//...
		break;
	case VT_REFPROP:
		// ...
		break;
	case VT_REFELT:
		// ...
		break;
	case VT_REFIDPROP:
		// ...
		break;
	case VT_REFIDELT:
		// ...
		break;
	}
	return RC_OK;
}

void AfyKernel::freeV(Value *v,ulong nv,MemAlloc *ma)
{
	for (ulong i=0; i<nv; i++) freeV(v[i]);
	ma->free(v);
}

void AfyKernel::freeV0(Value& v)
{
	try {
		HEAP_TYPE allc=(HEAP_TYPE)(v.flags&HEAP_TYPE_MASK); assert(allc!=NO_HEAP);
		switch (v.type) {
		default: break;
		case VT_STRING: case VT_BSTR: case VT_URL:
			if (v.length!=0 || (void*)v.str!=(void*)&v.length) free((char*)v.str,allc); break;
		case VT_REFIDPROP: case VT_REFIDELT: free((RefVID*)v.refId,allc); break;
		case VT_ARRAY: case VT_STRUCT:
			if (v.varray!=NULL) {
				for (ulong i=0; i<v.length; i++) freeV(const_cast<Value&>(v.varray[i]));
				free(const_cast<Value*>(v.varray),allc);
			}
			break;
		case VT_RANGE:
			if (v.range!=NULL) {
				assert(v.length==2);
				freeV(v.range[0]); freeV(v.range[1]); free(v.range,allc);
			}
			break;
		case VT_EXPRTREE: delete (ExprTree*)v.exprt; break;
		case VT_STREAM: if (v.stream.is!=NULL) v.stream.is->destroy(); break;
		case VT_STMT: if (v.stmt!=NULL) v.stmt->destroy(); break;
		case VT_COLLECTION: if (v.nav!=NULL) v.nav->destroy(); break;
		case VT_EXPR: free(v.expr,allc); break;
		}
	}  catch (RC) {} catch (...) {report(MSG_ERROR,"Exception in freeV(...)\n");}
}
