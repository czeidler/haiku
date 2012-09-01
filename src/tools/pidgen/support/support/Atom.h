/*
 * Copyright (c) 2005 Palmsource, Inc.
 * 
 * This software is licensed as described in the file LICENSE, which
 * you should have received as part of this distribution. The terms
 * are also available at http://www.openbinder.org/license.html.
 * 
 * This software consists of voluntary contributions made by many
 * individuals. For the exact contribution history, see the revision
 * history and logs, available at http://www.openbinder.org
 */
#ifndef _ATOM_H
#define _ATOM_H

#include <Referenceable.h>


//typedef BReferenceable SAtom;
class SAtom : public BReferenceable {
public:
			void
			AcquireReference() const
			{
				BReferenceable* that = (BReferenceable*)this;
				that->AcquireReference();
			}


			bool
			ReleaseReference() const
			{
				BReferenceable* that = (BReferenceable*)this;
				return that->ReleaseReference();
			}

			//!	@deprecated Backwards compatibility.  Do not use.
	inline	void			Acquire(const void* id) const { return AcquireReference(); }
	inline	bool			AttemptRelease(const void* id) const { return ReleaseReference(); }
};


/**************************************************************************************/

//!	Smart-pointer template that maintains a strong reference on a reference counted class.
/*!	Can be used with SAtom, SLightAtom, SLimAtom, and SSharedBuffer.

	For the most part a sptr<T> looks and feels like a raw C++ pointer.  However,
	currently sptr<> does not have a boolean conversion operator, so to check
	whether a sptr is NULL you will need to write code like this:
@code
if (my_sptr == NULL) ...
@endcode
*/
template <class TYPE>
class sptr
{
public:
		//!	Initialize to NULL pointer.
		sptr();
		//!	Initialize from a raw pointer.
		sptr(TYPE* p);

		//!	Assignment from a raw pointer.
		sptr<TYPE>& operator =(TYPE* p);

		//!	Initialize from another sptr.
		sptr(const sptr<TYPE>& p);
		//!	Assignment from another sptr.
		sptr<TYPE>& operator =(const sptr<TYPE>& p);
		//!	Initialization from a strong pointer to another type of SAtom subclass (type conversion).
		template <class NEWTYPE> sptr(const sptr<NEWTYPE>& p);
		//!	Assignment from a strong pointer to another type of SAtom subclass (type conversion).
		template <class NEWTYPE> sptr<TYPE>& operator =(const sptr<NEWTYPE>& p);

		//!	Release strong reference on object.
		~sptr();

		//!	Dereference pointer.
		TYPE& operator *() const;
		//!	Member dereference.
		TYPE* operator ->() const;
		
		//!	Return the raw pointer of this object.
		/*!	Keeps the object and leaves its reference count as-is.  You normally
			don't need to use this, and instead can use the -> and * operators. */
		TYPE* ptr() const;
		//!	Clear (set to NULL) and return the raw pointer of this object.
		/*! You now own its strong reference and must manually call DecStrong(). */
		TYPE* detach();
		//!	Return true if ptr() is NULL.
		/*!	@note It is preferred to just compare against NULL, that is:
			@code
if (my_atom == NULL) ...
			@endcode */
		bool is_null() const;

		//!	Retrieve edit access to the object.
		/*!	Use this with SSharedBuffer objects to request edit access to the
			buffer.  If the buffer needs to be copied, the sptr<> will be updated
			to point to the new buffer.  Returns a raw pointer to the buffer that
			you can modify. */
		TYPE* edit();
		//!	Version of edit() that allows you to change the size of the shared buffer.
		TYPE* edit(size_t size);

		// Give comparison operators access to our pointer.
		#define COMPARE_FRIEND(op)								\
			bool operator op (const TYPE* p2) const;			\
			bool operator op (const sptr<TYPE>& p2) const;		\

			//bool operator op (const wptr<TYPE>& p2) const;		
		
		COMPARE_FRIEND(==)
		COMPARE_FRIEND(!=)
		COMPARE_FRIEND(<=)
		COMPARE_FRIEND(<)
		COMPARE_FRIEND(>)
		COMPARE_FRIEND(>=)
		
		#undef COMPARE_FRIEND

private:
		//friend class wptr<TYPE>;
		
		// Special constructor for more efficient wptr::promote().
		//sptr(SAtom::weak_atom_ptr* weak, bool);
		
		TYPE *m_ptr;
};


/*-------------------------------------------------------------*/
/*---- No user serviceable parts after this -------------------*/

/* ----------------- sptr Implementation ------------------*/

#define B_INC_STRONG(atom, who) atom->AcquireReference()
#define B_ATTEMPT_INC_STRONG(atom, who) atom->AcquireReference()
#define B_FORCE_INC_STRONG(atom, who) atom->AcquireReference()
#define B_DEC_STRONG(atom, who) atom->ReleaseReference()


template<class TYPE> inline
sptr<TYPE>::sptr()								{ m_ptr = NULL; }
template<class TYPE> inline
sptr<TYPE>::sptr(TYPE* p)						{ if ((m_ptr=p) != NULL) B_INC_STRONG(p, this); }

template<class TYPE> inline
sptr<TYPE>& sptr<TYPE>::operator =(TYPE* p)
{
	if (p) B_INC_STRONG(p, this);
	if (m_ptr) B_DEC_STRONG(m_ptr, this);
	m_ptr = p;
	return *this;
}

template<class TYPE> inline
sptr<TYPE>::sptr(const sptr<TYPE>& p)			{ if ((m_ptr=p.m_ptr) != NULL) B_INC_STRONG(m_ptr, this); }
template<class TYPE> inline
sptr<TYPE>& sptr<TYPE>::operator =(const sptr<TYPE>& p)
{
	if (p.ptr()) B_INC_STRONG(p, this);
	if (m_ptr) B_DEC_STRONG(m_ptr, this);
	m_ptr = p.ptr();
	return *this;
}
template<class TYPE> template<class NEWTYPE> inline
sptr<TYPE>::sptr(const sptr<NEWTYPE>& p)
{
	//TypeConversion<NEWTYPE, TYPE>::Assert();
	if ((m_ptr=p.ptr()) != NULL) B_INC_STRONG(m_ptr, this);
}
template<class TYPE> template<class NEWTYPE> inline
sptr<TYPE>& sptr<TYPE>::operator =(const sptr<NEWTYPE>& p)
{
	//TypeConversion<NEWTYPE, TYPE>::Assert();
	if (p.ptr()) B_INC_STRONG(p, this);
	if (m_ptr) B_DEC_STRONG(m_ptr, this);
	m_ptr = p.ptr();
	return *this;
}

template<class TYPE> inline
sptr<TYPE>::~sptr()								{ if (m_ptr) B_DEC_STRONG(m_ptr, this); }

/*template<class TYPE> inline
sptr<TYPE>::sptr(SAtom::weak_atom_ptr* p, bool)
{
 	m_ptr = (p && B_ATTEMPT_INC_STRONG(p->atom, this)) ? reinterpret_cast<TYPE*>(p->cookie) : NULL;
//	m_ptr = (p && B_ATTEMPT_INC_STRONG(p->atom, this)) ? dynamic_cast<TYPE*>(p->cookie) : NULL;
}*/

template<class TYPE> inline
TYPE & sptr<TYPE>::operator *() const			{ return *m_ptr; }
template<class TYPE> inline
TYPE * sptr<TYPE>::operator ->() const			{ return m_ptr; }
template<class TYPE> inline
TYPE * sptr<TYPE>::ptr() const					{ return m_ptr; }
template<class TYPE> inline
TYPE * sptr<TYPE>::detach()						{ TYPE* p = m_ptr; m_ptr = NULL; return p; }
template<class TYPE> inline
bool sptr<TYPE>::is_null() const				{ return m_ptr == NULL; }

template<class TYPE> inline
TYPE * sptr<TYPE>::edit()
{
	TYPE* p = m_ptr;
	if (p != NULL) {
		p = p->Edit();
		m_ptr = p;
	}
	return p;
}

template<class TYPE> inline
TYPE * sptr<TYPE>::edit(size_t size)
{
	TYPE* p = m_ptr;
	if (p != NULL) {
		p = p->Edit(size);
		m_ptr = p;
	}
	return p;
}


/**************************************************************************************/

// A zillion kinds of comparison operators.
#define COMPARE(op)																				\
template<class TYPE> inline																		\
bool sptr<TYPE>::operator op (const TYPE* p2) const												\
	{ return ptr() op p2; }																		\
template<class TYPE> inline																		\
bool sptr<TYPE>::operator op (const sptr<TYPE>& p2) const										\
	{ return ptr() op p2.ptr(); }																\

COMPARE(==)
COMPARE(!=)
COMPARE(<=)
COMPARE(<)
COMPARE(>)
COMPARE(>=)

#undef COMPARE


#endif //_ATOM_H