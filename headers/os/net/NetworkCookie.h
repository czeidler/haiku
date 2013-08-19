/*
 * Copyright 2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _B_NETWORK_COOKIE_H_
#define _B_NETWORK_COOKIE_H_


#include <Archivable.h>
#include <DateTime.h>
#include <Message.h>
#include <String.h>
#include <Url.h>


class BNetworkCookie : public BArchivable {
public:
								BNetworkCookie(const char* name,
									const char* value);
								BNetworkCookie(const BString& cookieString);
								BNetworkCookie(const BString& cookieString,
									const BUrl& url);
								BNetworkCookie(BMessage* archive);
								BNetworkCookie();
	virtual						~BNetworkCookie();

	// Parse a "SetCookie" string

			BNetworkCookie&		ParseCookieStringFromUrl(const BString& string,
									const BUrl& url);
			BNetworkCookie& 	ParseCookieString(const BString& cookieString);

	// Modify the cookie fields
			BNetworkCookie&		SetName(const BString& name);
			BNetworkCookie&		SetValue(const BString& value);
			BNetworkCookie&		SetDomain(const BString& domain);
			BNetworkCookie&		SetPath(const BString& path);
			BNetworkCookie&		SetMaxAge(int32 maxAge);
			BNetworkCookie&		SetExpirationDate(time_t expireDate);
			BNetworkCookie&		SetExpirationDate(BDateTime& expireDate);
			BNetworkCookie& 	SetSecure(bool secure);
			BNetworkCookie&		SetHttpOnly(bool httpOnly);

	// Access the cookie fields
			const BString&		Name() const;
			const BString&		Value() const;
			const BString&		Domain() const;
			const BString&		Path() const;
			time_t				ExpirationDate() const;
			const BString&		ExpirationString() const;
			bool				Secure() const;
			bool				HttpOnly() const;
			const BString&		RawCookie(bool full) const;

			bool				IsHostOnly() const;
			bool				IsSessionCookie() const;
			bool				IsValid() const;
			bool				IsValidForUrl(const BUrl& url) const;
			bool				IsValidForDomain(const BString& domain) const;
			bool				IsValidForPath(const BString& path) const;

	// Test if cookie fields are defined
			bool				HasName() const;
			bool				HasValue() const;
			bool				HasDomain() const;
			bool				HasPath() const;
			bool				HasExpirationDate() const;

	// Test if cookie could be deleted
			bool				ShouldDeleteAtExit() const;
			bool				ShouldDeleteNow() const;

	// BArchivable members
	virtual	status_t			Archive(BMessage* into,
									bool deep = true) const;
	static	BArchivable*		Instantiate(BMessage* archive);

	// Overloaded operators
			BNetworkCookie&		operator=(const char* string);
			bool				operator==(const BNetworkCookie& other);
			bool				operator!=(const BNetworkCookie& other);
private:
			void				_Reset();
			int32				_ExtractNameValuePair(const BString& string,
									BString& name, BString& value,
									int32 index);
			int32				_ExtractAttributeValuePair(
									const BString& string, BString& name,
									BString& value,	int32 index);
			BString				_DefaultPathForUrl(const BUrl& url);

private:
	mutable	BString				fRawCookie;
	mutable	bool				fRawCookieValid;
	mutable	BString				fRawFullCookie;
	mutable	bool				fRawFullCookieValid;
	mutable	BString				fExpirationString;
	mutable	bool				fExpirationStringValid;

			BString				fName;
			BString				fValue;
			BString				fDomain;
			BString				fPath;
			BDateTime			fExpiration;
			bool				fSecure;
			bool				fHttpOnly;

			bool				fHostOnly;
			bool				fSessionCookie;
};

#endif // _B_NETWORK_COOKIE_H_

