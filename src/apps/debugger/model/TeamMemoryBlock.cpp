/*
 * Copyright 2011, Rene Gollent, rene@gollent.com.
 * Distributed under the terms of the MIT License.
 */


#include "TeamMemoryBlock.h"


#include <AutoLocker.h>

#include "TeamMemoryBlockManager.h"


// #pragma mark - TeamMemoryBlock


TeamMemoryBlock::TeamMemoryBlock(target_addr_t baseAddress,
	TeamMemoryBlockOwner* owner)
	:
	fValid(false),
	fWritable(false),
	fBaseAddress(baseAddress),
	fBlockOwner(owner)
{
}


TeamMemoryBlock::~TeamMemoryBlock()
{
	delete fBlockOwner;
}


void
TeamMemoryBlock::AddListener(Listener* listener)
{
	AutoLocker<BLocker> lock(fLock);
	fListeners.Add(listener);
}


bool
TeamMemoryBlock::HasListener(Listener* listener)
{
	AutoLocker<BLocker> lock(fLock);
	ListenerList::Iterator iterator = fListeners.GetIterator();
	while (iterator.HasNext()) {
		if (iterator.Next() == listener)
			return true;
	}

	return false;
}


void
TeamMemoryBlock::RemoveListener(Listener* listener)
{
	AutoLocker<BLocker> lock(fLock);
	fListeners.Remove(listener);
}


void
TeamMemoryBlock::MarkValid()
{
	fValid = true;
	NotifyDataRetrieved();
}


void
TeamMemoryBlock::Invalidate()
{
	fValid = false;
}


bool
TeamMemoryBlock::Contains(target_addr_t address) const
{
	return fValid && address >= fBaseAddress
		&& address < (fBaseAddress + sizeof(fData));
}


void
TeamMemoryBlock::SetWritable(bool writable)
{
	fWritable = writable;
}


void
TeamMemoryBlock::NotifyDataRetrieved(status_t result)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		if (result == B_OK)
			listener->MemoryBlockRetrieved(this);
		else
			listener->MemoryBlockRetrievalFailed(this, result);
	}
}


void
TeamMemoryBlock::LastReferenceReleased()
{
	fBlockOwner->RemoveBlock(this);

	delete this;
}


// #pragma mark - TeamMemoryBlock


TeamMemoryBlock::Listener::~Listener()
{
}


void
TeamMemoryBlock::Listener::MemoryBlockRetrieved(TeamMemoryBlock* block)
{
}


void
TeamMemoryBlock::Listener::MemoryBlockRetrievalFailed(TeamMemoryBlock* block,
	status_t result)
{
}
