/*
 * Copyright 2012 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Clemens Zeidler, haiku@clemens-zeidler.de
 */


#include "fd_entry_mapping.h"

#include <new>

#include <util/AutoLock.h>
#include <util/OpenHashTable.h>

#include "vfs.h"	// for dump_fd_entry_ref
#include "Vnode.h"


static rw_lock sFdEntryMapLock = RW_LOCK_INITIALIZER("fd_entry_map_lock");


void
read_lock_fd_entry()
{
	rw_lock_read_lock(&sFdEntryMapLock);
}


void
read_unlock_fd_entry()
{
	rw_lock_read_unlock(&sFdEntryMapLock);
}


struct vnode_hash_key {
	dev_t	device;
	ino_t	vnode;
};


struct PathHashDefinition {
	typedef vnode_hash_key		KeyType;
	typedef	entry_node_link		ValueType;

	size_t HashKey(const vnode_hash_key& key) const
	{
		return Hash(key.device, key.vnode);
	}

	size_t Hash(entry_node_link* value) const
	{
		vnode* node = value->node;
		return Hash(node->device, node->id);
	}

	bool Compare(const vnode_hash_key& key, entry_node_link* value) const
	{
		vnode* node = value->node;
		if (node->device == key.device && node->id == key.vnode)
			return true;
		return false;
	}

	entry_node_link*& GetLink(entry_node_link* value) const
	{
		return value->next;
	}

	inline size_t Hash(dev_t device, ino_t node) const
	{
		return (uint32)(node >> 32) + ((uint32)node ^ (uint32)device);
	}
};


typedef BOpenHashTable<PathHashDefinition> EntryNodeLinkTable;


static EntryNodeLinkTable sEntryNodeLinkTable;


#if 0
void
dump_fd_entry_ref(fd_entry_ref* entry)
{
	if (entry->parent == NULL) {
		dprintf("entry (no parent): ref_count %i, dir %i, name %s\n",
			(int)entry->ref_count, (int)entry->directory, entry->filename);
	}

	char path[B_PATH_NAME_LENGTH];
	vfs_entry_ref_to_path(entry->parent->node->device, entry->directory,
		entry->filename, path, B_PATH_NAME_LENGTH);
	dprintf("entry: ref_count %i, dir %i, name %s, path %s\n",
		(int)entry->ref_count, (int)entry->directory, entry->filename, path);
}


void
dump_entry_node_links()
{
	EntryNodeLinkTable::Iterator it(&sEntryNodeLinkTable);
	dprintf("entry_node_link hash table:\n");
	while (entry_node_link* link = it.Next()) {
		dprintf("vnode: device %i, node %i\n", (int)link->node->device,
			(int)link->node->id);
		FDEntryRefList::Iterator pathIt(&link->entries);
		for (fd_entry_ref* entry = pathIt.Next(); entry != NULL;
			entry = pathIt.Next()) {
			dprintf("\t");
			dump_fd_entry_ref(entry);
		}
	}
}
#endif


fd_entry_ref::fd_entry_ref()
	:
	parent(NULL),
	ref_count(0),
	filename(NULL)
{
}


fd_entry_ref::~fd_entry_ref()
{
	free(filename);
}


void
fd_entry_ref::SetTo(ino_t dir, const char* name)
{
	directory = dir;
	free(filename);
	filename = strdup(name);
}


inline static void
add_reference(fd_entry_ref* ref)
{
	ref->ref_count++;
}


inline static void
remove_reference(fd_entry_ref* ref)
{
	ref->ref_count--;
	if (ref->ref_count > 0)
		return;

	entry_node_link* parent = ref->parent;

	if (parent == NULL) {
		// not correctly initialized entry
		delete ref;
		return;
	}

	parent->entries.Remove(ref);
	delete ref;

	if (parent->entries.IsEmpty()) {
		sEntryNodeLinkTable.Remove(parent);
		delete parent;
	}
}


static entry_node_link*
lookup_entry_node_link(vnode* node)
{
	vnode_hash_key key;
	key.device = node->device;
	key.vnode = node->id;
	return sEntryNodeLinkTable.Lookup(key);
}


entry_node_link*
lookup_entry_node_link(dev_t device, ino_t node)
{
	vnode_hash_key key;
	key.device = device;
	key.vnode = node;
	return sEntryNodeLinkTable.Lookup(key);
}


inline fd_entry_ref*
find_entry_ref(FDEntryRefList* list, ino_t directory, const char* name)
{
	if (name == NULL)
		return NULL;

	FDEntryRefList::Iterator it(list);
	while (fd_entry_ref* entry = it.Next()) {
		if (entry->directory == directory
			&& strncmp(entry->filename, name, B_FILE_NAME_LENGTH) == 0)
			return entry;
	}
	return NULL;
}


static status_t
insert_entry_to_table(entry_node_link* link, vnode* node, fd_entry_ref* ref)
{
	if (link == NULL) {
		link = new(std::nothrow) entry_node_link;
		if (link == NULL)
			return B_NO_MEMORY;
		link->node = node;

		status_t status = sEntryNodeLinkTable.Insert(link);
		if (status != B_OK) {
			delete link;
			return status;
		}
	}

	ref->parent = link;
	link->entries.Add(ref);
	return B_OK;
}


status_t
insert_fd_entry(vnode* node, int fd, bool kernel, ino_t directory,
	const char* name)
{
	if (S_ISDIR(node->Type()))
		return B_OK;

	file_descriptor* descriptor = get_fd(get_current_io_context(kernel), fd);
	if (descriptor == NULL)
		return B_ERROR;

	WriteLocker locker(sFdEntryMapLock);

	entry_node_link* link = lookup_entry_node_link(node);
	if (link != NULL) {
		fd_entry_ref* entry = find_entry_ref(&link->entries, directory, name);
		if (entry != NULL) {
			add_reference(entry);
			descriptor->entry_ref = entry;
			put_fd(descriptor);
			return B_OK;
		}
	}

	// insert new entry
	fd_entry_ref* entryRef = new(std::nothrow) fd_entry_ref;
	if (entryRef == NULL) {
		put_fd(descriptor);
		return B_NO_MEMORY;
	}
	add_reference(entryRef);

	entryRef->SetTo(directory, name);

	status_t status = insert_entry_to_table(link, descriptor->u.vnode,
		entryRef);
	if (status != B_OK) {
		remove_reference(entryRef);
		put_fd(descriptor);
		return status;
	}

	descriptor->entry_ref = entryRef;

	put_fd(descriptor);
	return B_OK;
}


status_t
remove_fd_entry(file_descriptor* descriptor)
{
	if (S_ISDIR(descriptor->u.vnode->Type()))
		return B_OK;

	WriteLocker locker(sFdEntryMapLock);

	if (descriptor->entry_ref != NULL) {
		remove_reference(descriptor->entry_ref);
		descriptor->entry_ref = NULL;
	}

	return B_OK;
}


status_t
move_fd_entry(dev_t device, ino_t node, ino_t fromDirectory,
	const char* fromName, ino_t newDirectory, const char* newName)
{
	WriteLocker locker(sFdEntryMapLock);

	entry_node_link* link = lookup_entry_node_link(device, node);
	if (link == NULL)
		return B_ERROR;

	fd_entry_ref* entry = find_entry_ref(&link->entries, fromDirectory,
		fromName);
	if (entry != NULL)
		entry->SetTo(newDirectory, newName);

	return B_OK;
}
