/*
 * Copyright 2011, Clemens Zeidler
 * Distributed under the terms of the MIT License.
 */
#ifndef _BINARY_COMPATIBILITY_APP_H_
#define _BINARY_COMPATIBILITY_APP_H_


#include <binary_compatibility/Global.h>


struct perform_data_save_restore_state {
	BMessage*			state;
	BApplicationState*	storage;
	bool				return_value;
};

#endif /* _BINARY_COMPATIBILITY_APP_H_ */
