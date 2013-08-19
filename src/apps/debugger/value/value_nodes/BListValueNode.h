/*
 * Copyright 2012-2013, Rene Gollent, rene@gollent.com.
 * Distributed under the terms of the MIT License.
 */
#ifndef BLIST_VALUE_NODE_H
#define BLIST_VALUE_NODE_H


#include <List.h>
#include <Variant.h>

#include <ObjectList.h>

#include "ValueLocation.h"
#include "ValueNode.h"


class CompoundType;


class BListValueNode : public ValueNode {
public:
									BListValueNode(ValueNodeChild* nodeChild,
										Type* type);
	virtual							~BListValueNode();

	virtual	Type*					GetType() const;
	virtual	status_t				ResolvedLocationAndValue(
										ValueLoader* valueLoader,
										ValueLocation*& _location,
										Value*& _value);

	virtual bool					ChildCreationNeedsValue() const
										{ return true; }
	virtual status_t				CreateChildren();
	virtual int32					CountChildren() const;
	virtual ValueNodeChild*			ChildAt(int32 index) const;

	virtual	bool					IsRangedContainer() const;
	virtual	bool					IsContainerRangeFixed() const;
	virtual	void					ClearChildren();
	virtual	status_t				CreateChildrenInRange(int32 lowIndex,
										int32 highIndex);
	virtual	status_t				SupportedChildRange(int32& lowIndex,
										int32& highIndex) const;
private:
			class BListElementNodeChild;
			class BListItemCountNodeChild;

			// for GCC2
			friend class BListElementNodeChild;
			friend class BListItemCountNodeChild;

			typedef BObjectList<ValueNodeChild> ChildNodeList;

private:

			Type*					fType;
			ChildNodeList			fChildren;
			ValueLoader*			fLoader;
			BVariant				fDataLocation;
			BVariant				fItemCountLocation;
			Type*					fItemCountType;
			int32					fItemCount;
			bool					fCountChildCreated;
};


#endif	// BLIST_VALUE_NODE_H
