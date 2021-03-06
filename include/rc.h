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

#ifndef _RC_H_
#define _RC_H_

namespace AfyRC 
{

/**
 * message level
 */
enum MsgType
{
	MSG_CRIT, MSG_ERROR, MSG_WARNING, MSG_NOTICE, MSG_INFO, MSG_DEBUG 
};

/**
 * return codes
 */
enum RC
{
	RC_OK,					/**< succesful completion */
	RC_NOTFOUND,			/**< resources (pin,property,element,page,etc.) not found */
	RC_ALREADYEXISTS,		/**< resource (element,key,file,store etc.) already exists */
	RC_INTERNAL,			/**< internal error */
	RC_NOACCESS,			/**< access is denied */
	RC_NORESOURCES,			/**< not possible to allocate resource (too big pin or not enough memory */
	RC_FULL,				/**< disk is full */
	RC_DEVICEERR,			/**< i/o device error */
	RC_DATAERROR,			/**< data check (e.g. parity) error */
	RC_EOF,					/**< internal use only */
	RC_TIMEOUT,				/**< operation interrupted by timeout */
	RC_REPEAT,				/**< internal use only */
	RC_CORRUPTED,			/**< data element (e.g. pin,page,collection element,etc.) is corrupted */
	RC_CANCELED,			/**< i/o operation was cancelled */
	RC_VERSION,				/**< incompatible version */
	RC_TRUE,				/**< internal use only */
	RC_FALSE,				/**< internal use only */
	RC_TYPE,				/**< invalid type or type conversion is impossible */
	RC_DIV0,				/**< division by 0 */
	RC_INVPARAM,			/**< invalid parameter value */
	RC_READTX,				/**< attempt to modify data inside of a read-only transaction */
	RC_OTHER,				/**< unspecified error */
	RC_DEADLOCK,			/**< transaction interrupted due to a deadlock */
	RC_QUOTA,				/**< store allocation quota exceeded */
	RC_SHUTDOWN,			/**< the store is being shut - transaction aborted */
	RC_DELETED,				/**< pin was deleted */
	RC_CLOSED,				/**< result set closed after commit/rollback */
	RC_READONLY,			/**< store in read-only state (backup, logging error) */
	RC_NOSESSION,			/**< no session set for current thread */
	RC_INVOP,				/**< invalid operation for this object */
	RC_SYNTAX,				/**< syntactic error in query or expression */
	RC_TOOBIG,				/**< object (pin, property, collection) is too big */
	RC_PAGEFULL				/**< no space on page for the object (either pin or index entry) */
};

};

#endif
