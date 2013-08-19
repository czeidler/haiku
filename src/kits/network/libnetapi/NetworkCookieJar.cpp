/*
 * Copyright 2010-2013 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Christophe Huriaux, c.huriaux@gmail.com
 *		Hamish Morrison, hamishm53@gmail.com
 */


#include <Debug.h>
#include <HashMap.h>
#include <HashString.h>
#include <Message.h>
#include <NetworkCookieJar.h>

#include <new>

#include "NetworkCookieJarPrivate.h"


const char* kArchivedCookieMessageName = "be:cookie";


BNetworkCookieJar::BNetworkCookieJar()
	:
	fCookieHashMap(new PrivateHashMap())
{
}


BNetworkCookieJar::BNetworkCookieJar(const BNetworkCookieJar&)
	:
	fCookieHashMap(new PrivateHashMap())
{
	// TODO
}


BNetworkCookieJar::BNetworkCookieJar(const BNetworkCookieList& otherList)
	:
	fCookieHashMap(new PrivateHashMap())
{
	AddCookies(otherList);
}


BNetworkCookieJar::BNetworkCookieJar(BMessage* archive)
	:
	fCookieHashMap(new PrivateHashMap())
{
	BMessage extractedCookie;

	for (int32 i = 0; archive->FindMessage(kArchivedCookieMessageName, i,
			&extractedCookie) == B_OK; i++) {
		BNetworkCookie* heapCookie
			= new(std::nothrow) BNetworkCookie(&extractedCookie);

		if (heapCookie == NULL)
			break;

		if (!AddCookie(heapCookie)) {
			delete heapCookie;
			continue;
		}
	}
}


BNetworkCookieJar::~BNetworkCookieJar()
{
	BNetworkCookie* cookiePtr;

	for (Iterator it = GetIterator(); (cookiePtr = it.Next()) != NULL;)
		delete it.Remove();
}


// #pragma mark Add cookie to cookie jar


status_t
BNetworkCookieJar::AddCookie(const BNetworkCookie& cookie)
{
	BNetworkCookie* heapCookie = new(std::nothrow) BNetworkCookie(cookie);
	if (heapCookie == NULL)
		return B_NO_MEMORY;

	status_t result = AddCookie(heapCookie);
	if (result != B_OK) {
		delete heapCookie;
		return result;
	}

	return B_OK;
}


status_t
BNetworkCookieJar::AddCookie(BNetworkCookie* cookie)
{
	if (cookie == NULL)
		return B_BAD_VALUE;

	HashString key(cookie->Domain());

	BNetworkCookieList* list = fCookieHashMap->fHashMap.Get(key);
	if (list == NULL) {
		list = new(std::nothrow) BNetworkCookieList();
		if (list == NULL || fCookieHashMap->fHashMap.Put(key, list) != B_OK)
			return B_NO_MEMORY;
	}

	for (int32 i = 0; i < list->CountItems(); i++) {
		BNetworkCookie* c
			= reinterpret_cast<BNetworkCookie*>(list->ItemAt(i));

		if (c->Name() == cookie->Name() && c->Path() == cookie->Path()) {
			list->RemoveItem(i);
			break;
		}
	}

	if (cookie->ShouldDeleteNow())
		delete cookie;
	else
		list->AddItem(cookie);

	return B_OK;
}


status_t
BNetworkCookieJar::AddCookies(const BNetworkCookieList& cookies)
{
	for (int32 i = 0; i < cookies.CountItems(); i++) {
		BNetworkCookie* cookiePtr
			= reinterpret_cast<BNetworkCookie*>(cookies.ItemAt(i));

		// Using AddCookie by reference in order to avoid multiple
		// cookie jar share the same cookie pointers
		status_t result = AddCookie(*cookiePtr);
		if (result != B_OK)
			return result;
	}

	return B_OK;
}


// #pragma mark Purge useless cookies


uint32
BNetworkCookieJar::DeleteOutdatedCookies()
{
	int32 deleteCount = 0;
	BNetworkCookie* cookiePtr;

	for (Iterator it = GetIterator(); (cookiePtr = it.Next()) != NULL;) {
		if (cookiePtr->ShouldDeleteNow()) {
			delete it.Remove();
			deleteCount++;
		}
	}

	return deleteCount;
}


uint32
BNetworkCookieJar::PurgeForExit()
{
	int32 deleteCount = 0;
	BNetworkCookie* cookiePtr;

	for (Iterator it = GetIterator(); (cookiePtr = it.Next()) != NULL;) {
		if (cookiePtr->ShouldDeleteAtExit()) {
			delete it.Remove();
			deleteCount++;
		}
	}

	return deleteCount;
}


// #pragma mark BArchivable interface


status_t
BNetworkCookieJar::Archive(BMessage* into, bool deep) const
{
	status_t error = BArchivable::Archive(into, deep);

	if (error == B_OK) {
		BNetworkCookie* cookiePtr;

		for (Iterator it = GetIterator(); (cookiePtr = it.Next()) != NULL;) {
			BMessage subArchive;

			error = cookiePtr->Archive(&subArchive, deep);
			if (error != B_OK)
				return error;

			error = into->AddMessage(kArchivedCookieMessageName, &subArchive);
			if (error != B_OK)
				return error;
		}
	}

	return error;
}


BArchivable*
BNetworkCookieJar::Instantiate(BMessage* archive)
{
	if (archive->HasMessage(kArchivedCookieMessageName))
		return new(std::nothrow) BNetworkCookieJar(archive);

	return NULL;
}


// #pragma mark BFlattenable interface


bool
BNetworkCookieJar::IsFixedSize() const
{
	// Flattened size vary
	return false;
}


type_code
BNetworkCookieJar::TypeCode() const
{
	// TODO: Add a B_COOKIEJAR_TYPE
	return B_ANY_TYPE;
}


ssize_t
BNetworkCookieJar::FlattenedSize() const
{
	_DoFlatten();
	return fFlattened.Length() + 1;
}


status_t
BNetworkCookieJar::Flatten(void* buffer, ssize_t size) const
{
	if (FlattenedSize() > size)
		return B_ERROR;

	fFlattened.CopyInto(reinterpret_cast<char*>(buffer), 0,
		fFlattened.Length());
	reinterpret_cast<char*>(buffer)[fFlattened.Length()] = 0;

	return B_OK;
}


bool
BNetworkCookieJar::AllowsTypeCode(type_code) const
{
	// TODO
	return false;
}


status_t
BNetworkCookieJar::Unflatten(type_code, const void* buffer, ssize_t size)
{
	BString flattenedCookies;
	flattenedCookies.SetTo(reinterpret_cast<const char*>(buffer), size);

	while (flattenedCookies.Length() > 0) {
		BNetworkCookie tempCookie;
		BString tempCookieLine;

		int32 endOfLine = flattenedCookies.FindFirst('\n', 0);
		if (endOfLine == -1)
			tempCookieLine = flattenedCookies;
		else {
			flattenedCookies.MoveInto(tempCookieLine, 0, endOfLine);
			flattenedCookies.Remove(0, 1);
		}

		if (tempCookieLine.Length() != 0 && tempCookieLine[0] != '#') {
			for (int32 field = 0; field < 7; field++) {
				BString tempString;

				int32 endOfField = tempCookieLine.FindFirst('\t', 0);
				if (endOfField == -1)
					tempString = tempCookieLine;
				else {
					tempCookieLine.MoveInto(tempString, 0, endOfField);
					tempCookieLine.Remove(0, 1);
				}

				switch (field) {
					case 0:
						tempCookie.SetDomain(tempString);
						break;

					case 1:
						// TODO: Useless field ATM
						break;

					case 2:
						tempCookie.SetPath(tempString);
						break;

					case 3:
						tempCookie.SetSecure(tempString == "TRUE");
						break;

					case 4:
						tempCookie.SetExpirationDate(atoi(tempString));
						break;

					case 5:
						tempCookie.SetName(tempString);
						break;

					case 6:
						tempCookie.SetValue(tempString);
						break;
				} // switch
			} // for loop

			AddCookie(tempCookie);
		}
	}

	return B_OK;
}


// #pragma mark Iterators


BNetworkCookieJar::Iterator
BNetworkCookieJar::GetIterator() const
{
	return BNetworkCookieJar::Iterator(this);
}


BNetworkCookieJar::UrlIterator
BNetworkCookieJar::GetUrlIterator(const BUrl& url) const
{
	if (!url.HasPath()) {
		BUrl copy(url);
		copy.SetPath("/");
		return BNetworkCookieJar::UrlIterator(this, copy);
	}

	return BNetworkCookieJar::UrlIterator(this, url);
}


void
BNetworkCookieJar::_DoFlatten() const
{
	fFlattened.Truncate(0);

	BNetworkCookie* cookiePtr;
	for (Iterator it = GetIterator(); (cookiePtr = it.Next()) != NULL;) {
		fFlattened 	<< cookiePtr->Domain() << '\t' << "TRUE" << '\t'
			<< cookiePtr->Path() << '\t'
			<< (cookiePtr->Secure()?"TRUE":"FALSE") << '\t'
			<< (int32)cookiePtr->ExpirationDate() << '\t'
			<< cookiePtr->Name() << '\t' << cookiePtr->Value() << '\n';
	}
}


// #pragma mark Iterator


BNetworkCookieJar::Iterator::Iterator(const Iterator& other)
	:
	fCookieJar(other.fCookieJar),
	fIterator(other.fIterator),
	fLastList(other.fLastList),
	fList(other.fList),
	fElement(other.fElement),
	fLastElement(other.fLastElement),
	fIndex(other.fIndex)
{
}


BNetworkCookieJar::Iterator::Iterator(const BNetworkCookieJar* cookieJar)
	:
	fCookieJar(const_cast<BNetworkCookieJar*>(cookieJar)),
	fIterator(NULL),
	fLastList(NULL),
	fList(NULL),
	fElement(NULL),
	fLastElement(NULL),
	fIndex(0)
{
	fIterator = new(std::nothrow) PrivateIterator(
		fCookieJar->fCookieHashMap->fHashMap.GetIterator());

	// Locate first cookie
	_FindNext();
}


BNetworkCookieJar::Iterator::~Iterator()
{
	delete fIterator;
}


bool
BNetworkCookieJar::Iterator::HasNext() const
{
	return fElement;
}


BNetworkCookie*
BNetworkCookieJar::Iterator::Next()
{
	if (!fElement)
		return NULL;

	BNetworkCookie* result = fElement;
	_FindNext();
	return result;
}


BNetworkCookie*
BNetworkCookieJar::Iterator::NextDomain()
{
	if (!fElement)
		return NULL;

	BNetworkCookie* result = fElement;

	if (!fIterator->fCookieMapIterator.HasNext()) {
		fElement = NULL;
		return NULL;
	}

	fList = *fIterator->fCookieMapIterator.NextValue();
	fIndex = 0;
	fElement = reinterpret_cast<BNetworkCookie*>(fList->ItemAt(fIndex));

	return result;
}


BNetworkCookie*
BNetworkCookieJar::Iterator::Remove()
{
	if (!fLastElement)
		return NULL;

	BNetworkCookie* result = fLastElement;

	if (fIndex == 0) {
		if (fLastList->CountItems() == 1) {
			fIterator->fCookieMapIterator.Remove();
			delete fLastList;
		}
		else
			fLastList->RemoveItem(fLastList->CountItems() - 1);
	} else {
		fIndex--;
		fList->RemoveItem(fIndex);
	}

	fLastElement = NULL;
	return result;
}


BNetworkCookieJar::Iterator&
BNetworkCookieJar::Iterator::operator=(const BNetworkCookieJar::Iterator& other)
{
	fCookieJar = other.fCookieJar;
	fLastList = other.fLastList;
	fList = other.fList;
	fElement = other.fElement;
	fLastElement = other.fLastElement;
	fIndex = other.fIndex;

	fIterator = new(std::nothrow) PrivateIterator(*other.fIterator);
	if (fIterator == NULL) {
		// Make the iterator unusable.
		fElement = NULL;
		fLastElement = NULL;
	}
	return *this;
}


void
BNetworkCookieJar::Iterator::_FindNext()
{
	fLastElement = fElement;

	fIndex++;
	if (fList && fIndex < fList->CountItems()) {
		fElement = reinterpret_cast<BNetworkCookie*>(fList->ItemAt(fIndex));
		return;
	}

	if (!fIterator->fCookieMapIterator.HasNext()) {
		fElement = NULL;
		return;
	}

	fLastList = fList;
	fList = *(fIterator->fCookieMapIterator.NextValue());
	fIndex = 0;
	fElement = reinterpret_cast<BNetworkCookie*>(fList->ItemAt(fIndex));
}


// #pragma mark URL Iterator


BNetworkCookieJar::UrlIterator::UrlIterator(const UrlIterator& other)
{
	*this = other;
}


BNetworkCookieJar::UrlIterator::UrlIterator(const BNetworkCookieJar* cookieJar,
	const BUrl& url)
	:
	fCookieJar(const_cast<BNetworkCookieJar*>(cookieJar)),
	fIterator(NULL),
	fList(NULL),
	fLastList(NULL),
	fElement(NULL),
	fLastElement(NULL),
	fIndex(0),
	fLastIndex(0),
	fUrl(url)
{
	BString domain = url.Host();

	if (!domain.Length())
		return;

	fIterator = new(std::nothrow) PrivateIterator(
		fCookieJar->fCookieHashMap->fHashMap.GetIterator());

	if (fIterator != NULL) {
		// Prepending a dot since _FindNext is going to call _SupDomain()
		domain.Prepend(".");
		fIterator->fKey.SetTo(domain, domain.Length());
		_FindNext();
	}
}


BNetworkCookieJar::UrlIterator::~UrlIterator()
{
	delete fIterator;
}


bool
BNetworkCookieJar::UrlIterator::HasNext() const
{
	return fElement;
}


BNetworkCookie*
BNetworkCookieJar::UrlIterator::Next()
{
	if (!fElement)
		return NULL;

	BNetworkCookie* result = fElement;
	_FindNext();
	return result;
}


BNetworkCookie*
BNetworkCookieJar::UrlIterator::Remove()
{
	if (!fLastElement)
		return NULL;

	BNetworkCookie* result = fLastElement;

	fLastList->RemoveItem(fLastIndex);

	if (fLastList->CountItems() == 0) {
		HashString lastKey(fLastElement->Domain(),
			fLastElement->Domain().Length());

		delete fCookieJar->fCookieHashMap->fHashMap.Remove(lastKey);
	}

	fLastElement = NULL;
	return result;
}


BNetworkCookieJar::UrlIterator&
BNetworkCookieJar::UrlIterator::operator=(
	const BNetworkCookieJar::UrlIterator& other)
{
	fCookieJar = other.fCookieJar;
	fList = other.fList;
	fLastList = other.fLastList;
	fElement = other.fElement;
	fLastElement = other.fLastElement;
	fIndex = other.fIndex;
	fLastIndex = other.fLastIndex;

	fIterator = new(std::nothrow) PrivateIterator(*other.fIterator);
	if (fIterator == NULL) {
		// Make the iterator unusable.
		fElement = NULL;
		fLastElement = NULL;
	}

	return *this;
}


bool
BNetworkCookieJar::UrlIterator::_SuperDomain()
{
	const char* domain = fIterator->fKey.GetString();
	const char* nextDot = strchr(domain, '.');

	if (nextDot == NULL)
		return false;

	fIterator->fKey.SetTo(nextDot + 1);
	return true;
}


void
BNetworkCookieJar::UrlIterator::_FindNext()
{
	fLastIndex = fIndex;
	fLastElement = fElement;
	fLastList = fList;

	while (!_FindPath()) {
		if (!_SuperDomain()) {
			fElement = NULL;
			return;
		}

		_FindDomain();
	}
}


void
BNetworkCookieJar::UrlIterator::_FindDomain()
{
	fList = fCookieJar->fCookieHashMap->fHashMap.Get(fIterator->fKey);

	if (fList == NULL)
		fElement = NULL;

	fIndex = -1;
}


bool
BNetworkCookieJar::UrlIterator::_FindPath()
{
	fIndex++;
	while (fList && fIndex < fList->CountItems()) {
		fElement = reinterpret_cast<BNetworkCookie*>(fList->ItemAt(fIndex));

		if (fElement->IsValidForPath(fUrl.Path()))
			return true;

		fIndex++;
	}

	return false;
}
