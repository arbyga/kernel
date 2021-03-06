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

/**
 * Classification control
 * Implementation of IndexNav interface (see affinity.h)
 */
#ifndef _CLASSIFIER_H_
#define _CLASSIFIER_H_

#include "pinref.h"
#include "idxtree.h"
#include "pinex.h"
#include "qmgr.h"
#include "session.h"
#include "startup.h"

using namespace AfyDB;

namespace AfyKernel
{
#define	SDEL_FLAG			0x80000000

#define	CLASS_BUF_LIMIT		0x10000000
#define	BATCHSIZE			1000


#define	PROP_SPEC_LAST		PropertyID(255)				// highest possible special propID
#define	CLASS_ACL			0x8000

#define	DEFAULT_CLASS_HASH_SIZE		0x100
#define	DEFAULT_CLASS_CACHE_SIZE	0x400

enum ClassIdxOp {CI_INSERT, CI_UPDATE, CI_DELETE, CI_SDELETE, CI_UDELETE, CI_PURGE, CI_INSERTD};

/**
 * class descriptor
 */
class Class
{
	friend	class		Classifier;
	friend	class		ClassIndex;
	friend	class		ClassDelTx;
	typedef	QElt<Class,ClassID,ClassID> QE;
	ClassID				cid;
	QE					*qe;
	class	Classifier&	mgr;
	class	Stmt		*query;
	class	ClassIndex	*index;
	PageID				cluster[2];
	PID					id;
	PageAddr			addr;
	ulong				flags;
	Session	*const		txs;
public:
	Class(ulong id,Classifier& cls,Session *s=NULL);
	~Class();
	class	Stmt		*getQuery() const {return query;}
	class	ClassIndex	*getIndex() const {return index;}
	ushort				getFlags() const {return (ushort)flags;}
	const	PageAddr&	getAddr() const {return addr;}
	RC					setAddr(const PageAddr& ad) {addr=ad; return update();}
	RC					update(bool fFirst=false);
	ClassID				getID() const {return cid;}
	const PID&			getPID() const {return id;}
	void				setKey(ClassID id,void*);
	QE					*getQE() const {return qe;}
	void				setQE(QE *q) {qe=q;}
	bool				isDirty() const {return false;}
	RW_LockType			lockType(RW_LockType lt) const {return lt;}
	bool				save() const {return true;}
	RC					load(int,ulong);
	void				release();
	void				destroy();
	void				drop();
	void				operator delete(void *p) {free(p,STORE_HEAP);}
	void				initNew() const {}
	static	Class		*createNew(ClassID id,void *mg);
	static	void		waitResource(void *mg) {threadYield();}
	static	void		signal(void *mg) {}
};

/**
 * index descriptor (associated with class family)
 */
class ClassIndex : public TreeStdRoot
{
	Class&			cls;
	IndexFormat		fmt;
	PageID			anchor;
	mutable RWLock	rwlock;
	mutable	RWLock	rootLock;
	ulong			state;
	const ulong		nSegs;
	IndexSeg		indexSegs[1];
public:
	ClassIndex(Class& cl,ulong nS,PageID rt,PageID anc,IndexFormat fm,uint32_t h,StoreCtx *ct)
				: TreeStdRoot(rt,ct),cls(cl),fmt(fm),anchor(anc),state(0),nSegs(nS) {height=h;}
	virtual			~ClassIndex();
	void			*operator new(size_t s,ulong nSegs,MemAlloc *ma) {return ma->malloc(s+int(nSegs-1)*sizeof(IndexSeg));}
	operator		Class&() const {return cls;}
	RC				drop() {PageID rt=root; root=INVALID_PAGEID; return rt!=INVALID_PAGEID?Tree::drop(rt,ctx):RC_OK;}

	ulong			getNSegs() const {return nSegs;}
	const IndexSeg *getIndexSegs() const {return indexSegs;}
	void			release() {cls.release();}

	TreeFactory		*getFactory() const;
	IndexFormat		indexFormat() const;
	PageID			startPage(const SearchKey*,int& level,bool,bool=false);
	PageID			prevStartPage(PageID pid);
	RC				addRootPage(const SearchKey& key,PageID& pageID,ulong level);
	RC				removeRootPage(PageID page,PageID leftmost,ulong level);
	bool			lock(RW_LockType,bool fTry=false) const;
	void			unlock() const;
	TreeConnect		*persist(uint32_t& hndl) const;
	void			destroy();

	static	IndexFormat	ifmt;
	friend	class	IndexScan;
	friend	class	Classifier;
	friend	class	Class;
};

class ClassPropIndex;
class Expr;

struct ClassRef
{
	const	ClassID		cid;
	ushort				nIndexProps;
	ushort				notifications;
	unsigned			flags;
	ClassRef(ClassID id,ushort np,ushort nots,unsigned flg) : cid(id),nIndexProps(np),notifications(nots),flags(flg) {}
};

/**
 * reference to a class
 * stored in memory tables, used in classification
 */
struct ClassRefT : public ClassRef
{
	const	ushort		nConds;
	ushort				refCnt;
	ClassPropIndex		*sub;
	union {
		Expr			*cond;
		Expr			**conds;
	};
	PropertyID			indexProps[1];
	ClassRefT(ClassID id,ushort np,ushort nc,ushort nots,unsigned flg) : ClassRef(id,np,nots,flg),nConds(nc),refCnt(0),sub(NULL) {cond=NULL;}
	void				*operator new(size_t s,ulong np,MemAlloc *ma) {return ma->malloc(s+int(np-1)*sizeof(PropertyID));}
	class ClassRefCmp	{public: __forceinline static int cmp(const ClassRef *cr,ClassID ci) {return cmp3(cr->cid,ci);}};
};

/**
 * reference to a class for class creation/destruction
 */
struct ClassDscr : public ClassRef
{
	ClassDscr		*next;
	const Stmt		*query;
	class IndexInit	*cidx;
	PID				id;
	PageAddr		addr;
	ClassDscr(ClassID cid,ushort np,unsigned flags) : ClassRef(cid,np,0,flags),next(NULL),query(NULL),cidx(NULL),id(PIN::defPID),addr(PageAddr::invAddr) {}
};

/**
 * an array of classes this PIN is a member of
 * @see Classifier::classify()
 */
struct ClassResult
{
	MemAlloc			*const ma;
	StoreCtx			*const ctx;
	const	ClassRef	**classes;
	ulong				nClasses;
	ulong				xClasses;
	ulong				nIndices;
	ulong				notif;
	ClassResult(MemAlloc *m,StoreCtx *ct) : ma(m),ctx(ct),classes(NULL),nClasses(0),xClasses(0),nIndices(0),notif(0) {}
	~ClassResult() {if (classes!=NULL) ma->free(classes);}
	RC					insert(const ClassRef *cr,const ClassRef **cins=NULL);
};

/**
 * node in a tree of classes used for classification
 */
struct PIdxNode {
	PropertyID			pid;
	PIdxNode			*up;
	PIdxNode			*next;
	PIdxNode			*down;
	const	ClassRefT	**classes;
	ulong				nClasses;
};

/**
 * in-memory tree of classes for classification
 */
class ClassPropIndex
{
	friend	class	Classifier;
	friend	class	ClassDelTx;
	PIdxNode		*root;
	const ClassRefT	**other;
	ulong			nOther;
	ulong			nClasses;
	MemAlloc		*const allc;
private:
	ClassPropIndex(MemAlloc *ma) : root(NULL),other(NULL),nOther(0),nClasses(0),allc(ma) {}
	~ClassPropIndex();
	void operator delete(void *p) {if (p!=NULL)((ClassPropIndex*)p)->allc->free(p);}
	bool			add(ClassID cid,const Stmt *qry,unsigned flags,const struct PropDNF *dnf,size_t ldnf,ulong notifications,StoreCtx *ctx);
	const ClassRefT	*find(ClassID cid,const PropertyID *pids,ulong npids) const;
	RC				remove(ClassID cid,const PropertyID *pids,ulong npids);
	RC				insert(ClassID cid,const Stmt *qry,unsigned flags,ClassRefT *&cr,ulong notifications,const ClassRefT **&pc,ulong& n);
	RC				classify(PINEx *pe,ClassResult& res);
	RC				classify(const ClassRefT *cp,PINEx *pin,ClassResult& res);
	template<typename T> class it {
		const	ClassPropIndex&	pidx;
		const	T				*pt;
		const	ulong			nProps;
		const	PIdxNode		*node;
		const	ClassRefT		**cp;
				ulong			nc;
				ulong			idx;
	public:
		it(const ClassPropIndex& pi,const T *p,ulong np) : pidx(pi),pt(p),nProps(np),node(pidx.root),cp(NULL),nc(0),idx(0) {}
		const ClassRefT **next() {
			if (cp==NULL) {
				if (pt==NULL || idx>=nProps || node==NULL) {nc=pidx.nOther; return cp=pidx.other;}
			} else if (nc>1) {--nc; return ++cp;}
			else if (node==NULL) return NULL;
			else {
			up:
				while (node->next==NULL || ++idx>=nProps) {
					if ((node=node->up)==NULL) {nc=pidx.nOther; return cp=pidx.other;}
					while (pt[--idx].getPropID()!=node->pid) assert(idx>0);
					if ((nc=node->nClasses)>0) return cp=node->classes;
				}
				node=node->next;
			}
			for (;;) {
				PropertyID pid = pt[idx].getPropID();
				do {
					for (; node->pid<pid; node=node->next) if (node->next==NULL) goto up;
					for (; node->pid>pid; pid=pt[++idx].getPropID()) if (idx+1>=nProps) goto up;
				} while (pid!=node->pid);
				if (node->down!=NULL && idx+1<nProps) {node=node->down; ++idx;} 
				else if ((nc=node->nClasses)==0) goto up; else return cp=node->classes;
			}
		}
	};
};

struct TxIndexElt
{
	uint32_t	id;
	SearchKey	key;
	byte		*ins;
	size_t		lIns;
	byte		*del;
	size_t		lDel;
	byte		*upd;
	size_t		lUpd;

	static	SListOp	compare(const TxIndexElt &e1,const TxIndexElt &e2) {
		int cmp=cmp3(e1.id,e2.id);
		if (cmp==0) {
			//...
		}
		return cmp<0?SLO_LT:SLO_GT;
	}
};

/**
 * transaction cache of index operations
 */
class TxIndex : public SList<TxIndexElt,TxIndexElt>
{
	Session	*const	ses;
public:
	TxIndex(Session *s) : SList<TxIndexElt,TxIndexElt>(*(MemAlloc*)s),ses(s) {}
	RC	flush();
};

/**
 * implementation of IndexNav interface
 */
class IndexNavImpl : public IndexNav, public IKeyCallback
{
	Session				*const	ses;
	unsigned			const	nVals;
	PropertyID			const	pid;
	ClassIndex					*cidx;
	TreeScan					*scan;
	bool						fFree;
	Value						v[1];
public:
	IndexNavImpl(Session *ses,ClassIndex *cidx,PropertyID pi=STORE_INVALID_PROPID);
	virtual	~IndexNavImpl();
	void	newKey();
	RC		next(PID& id,GO_DIR=GO_NEXT);
	RC		position(const Value *pv,unsigned nValues);
	RC		getValue(const Value *&pv,unsigned& nValues);
	const Value *next();
	unsigned nValues();
	void	destroy();
	void	*operator	new(size_t s,ulong nV,Session *ses) {return ses->malloc(s+int(nV-1)*sizeof(Value));}
	void	operator	delete(void *p) {if (p!=NULL) ((IndexNavImpl*)p)->ses->free(p);}
};

struct BuiltinURI
{
	size_t		lname;
	const char	*name;
	URIID		uid;
};

typedef QMgr<Class,ClassID,ClassID,int,STORE_HEAP> ClassHash;

/**
 * Classification manager
 */
class Classifier : public ClassHash, public TreeFactory, public TreeConnect
{
	friend	class		Class;
	friend	class		ClassIndex;
	friend	class		ClassPropIndex;
	friend	class		IndexInit;
	friend	class		ClassDelTx;
	StoreCtx			*const ctx;
	RWLock				rwlock;
	Mutex				lock;
	bool				fInit;
	ClassPropIndex		classIndex;
	TreeGlobalRoot		classMap;
	TreeGlobalRoot		classPINs;
	SharedCounter		nCached;
	int					xCached;
	volatile long		xPropID;
public:
	Classifier(StoreCtx *ct,ulong timeout,ulong hashSize=DEFAULT_CLASS_HASH_SIZE,ulong cacheSize=DEFAULT_CLASS_CACHE_SIZE);
	bool				isInit() const {return fInit;}
	RC					initStoreMaps(Session *ses);
	RC					restoreXPropID(Session *ses);
	RC					classify(PINEx *pin,ClassResult& res);
	RC					enable(Session *ses,class Class *cls,ulong notifications);
	void				disable(Session *ses,class Class *cls,ulong notifications);
	RWLock				*getLock() {return &rwlock;}
	void				setMaxPropID(PropertyID id);
	RC					getClassInfo(ClassID cid,Class *&cls,uint64_t& nPINs,uint64_t& nDeletedPINs);
	RC					index(Session *ses,PINEx *pin,const ClassResult& clr,ClassIdxOp op,const struct PropInfo **ppi=NULL,unsigned npi=0,const PageAddr *old=NULL);
	RC					initClasses(Session *ses);
	RC					rebuildAll(Session *ses);
	RC					classifyAll(PIN *const *pins,unsigned nPins,Session *ses,bool fDrop=false);
	RC					dropClass(Class *cls,Session *ses);
	void				findBase(class SimpleVar *qv);
	TreeGlobalRoot&		getClassMap() {return classMap;}
	TreeGlobalRoot&		getClassPINs() {return classPINs;}
	Class				*getClass(ClassID cid,RW_LockType lt=RW_S_LOCK);
	RC					setFlags(ClassID,ulong,ulong);
	RC					remove(ClassID,Session *ses);
	PropertyID			getXPropID() const {return (PropertyID)xPropID;}
	RC					classTx(Session *ses,ClassDscr*&,bool fCommit=true);
	static	void		merge(ClassDscr *from,ClassDscr *&to);

	byte				getID() const;
	byte				getParamLength() const;
	void				getParams(byte *buf,const Tree& tr) const;
	RC					createTree(const byte *params,byte lparams,Tree *&tree);
	static	const		BuiltinURI	builtinURIs[PROP_SPEC_MAX+1];
private:
	RC					add(Session *ses,ClassID cid,const Stmt *qry,unsigned flags,ulong notifications=0);
	ClassRefT			*findBaseRef(const Class *base,Session *ses);
	ClassPropIndex		*getClassPropIndex(const class SimpleVar *qv,Session *ses,bool fAdd=true);
	RC					indexFormat(ulong vt,IndexFormat& fmt) const;
	RC					insertRef(struct ClassCtx& cctx,ushort **ppb,size_t *ps,const byte *extb,ushort lext,struct IndexValue *iv=NULL);
	RC					freeSpace(ClassCtx& cctx,size_t l,unsigned skip=~0u);
	Tree				*connect(uint32_t handle);
};

};

#endif
