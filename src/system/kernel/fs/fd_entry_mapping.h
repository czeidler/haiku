/*
 * Copyright 2012 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Clemens Zeidler, haiku@clemens-zeidler.de
 */
#ifndef FD_ENTRY_MAPPING_H
#define FD_ENTRY_MAPPING_H


#include <OS.h>

#include <fd.h>
#include <khash.h>
#include <util/SinglyLinkedList.h>


struct entry_node_link;


struct fd_entry_ref : public SinglyLinkedListLinkImpl<fd_entry_ref> {
public:
								fd_entry_ref();
								~fd_entry_ref();
			void				SetTo(ino_t directory, const char* name);

			entry_node_link*	parent;
			int32				ref_count;
			ino_t				directory;
			char*				filename;
};


typedef SinglyLinkedList<fd_entry_ref>	FDEntryRefList;


struct entry_node_link {
	entry_node_link*	next;
	vnode*				node;
	FDEntryRefList		entries;
};


bool				init_fd_entry_hash_table();
void				dump_fd_entry_hash_table();

void				read_lock_fd_entry();
void				read_unlock_fd_entry();

//! lookup_entry_node_link needs to be locked
entry_node_link*	lookup_entry_node_link(dev_t device, ino_t node);

status_t			insert_fd_entry(vnode* node, int fd, bool kernel,
						ino_t directory, const char* filename);
status_t			remove_fd_entry(file_descriptor* descriptor);
status_t			move_fd_entry(dev_t device, ino_t node, ino_t fromDirectory,
						const char* fromName, ino_t newDirectory,
						const char* newName);

#endif // FD_ENTRY_MAPPING_H
