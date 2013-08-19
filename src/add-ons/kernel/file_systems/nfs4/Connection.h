/*
 * Copyright 2012-2013 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Paweł Dziepak, pdziepak@quarnos.org
 */
#ifndef CONNECTION_H
#define CONNECTION_H


#include <netinet/in.h>

#include <lock.h>
#include <SupportDefs.h>


struct PeerAddress {
			sockaddr_storage	fAddress;
			int					fProtocol;

			bool				operator==(const PeerAddress& address);
			bool				operator<(const PeerAddress& address);

			PeerAddress&		operator=(const PeerAddress& address);

								PeerAddress();
								PeerAddress(int networkFamily);

	inline	int					Family() const;

			const char*			ProtocolString() const;
			void				SetProtocol(const char* protocol);

			char*				UniversalAddress() const;

			socklen_t			AddressSize() const;

			void				SetPort(uint16 port);
			uint16				Port() const;

			const void*			InAddr() const;
			size_t				InAddrSize() const;
};


inline int
PeerAddress::Family() const
{
	return fAddress.ss_family;
}


struct addrinfo;

class AddressResolver {
public:
								AddressResolver(const char* name);
								~AddressResolver();

			status_t			GetNextAddress(PeerAddress* address);

			void				ForceProtocol(const char* protocol);
			void				ForcePort(uint16 port);

protected:
			status_t			ResolveAddress(const char* name);

private:
			addrinfo*			fHead;
			addrinfo*			fCurrent;

			PeerAddress			fAddress;

			uint16				fForcedPort;
			int					fForcedProtocol;

			status_t			fStatus;
};

class ConnectionBase {
public:
								ConnectionBase(const PeerAddress& address);
	virtual						~ConnectionBase();

			status_t			GetLocalAddress(PeerAddress* address);

			void				Disconnect();

protected:
			sem_id				fWaitCancel;
			int					fSocket;
			mutex				fSocketLock;

			const PeerAddress	fPeerAddress;
};

class Connection : public ConnectionBase {
public:
	static	status_t			Connect(Connection **connection,
									const PeerAddress& address);
	static	status_t			SetTo(Connection **connection, int socket,
									const PeerAddress& address);

	virtual	status_t			Send(const void* buffer, uint32 size) = 0;
	virtual	status_t			Receive(void** buffer, uint32* size) = 0;

			status_t			Reconnect();

protected:
	static	Connection*			CreateObject(const PeerAddress& address);

								Connection(const PeerAddress& address);
			status_t			Connect();

};

class ConnectionStream : public Connection {
public:
								ConnectionStream(const PeerAddress& address);

	virtual	status_t			Send(const void* buffer, uint32 size);
	virtual	status_t			Receive(void** buffer, uint32* size);
};

class ConnectionPacket : public Connection {
public:
								ConnectionPacket(const PeerAddress& address);

	virtual	status_t			Send(const void* buffer, uint32 size);
	virtual	status_t			Receive(void** buffer, uint32* size);
};

class ConnectionListener : public ConnectionBase {
public:
	static	status_t	Listen(ConnectionListener** listener, int networkFamily,
							uint16 port = 0);

			status_t	AcceptConnection(Connection** connection);

protected:
						ConnectionListener(const PeerAddress& address);
};

#endif	// CONNECTION_H

