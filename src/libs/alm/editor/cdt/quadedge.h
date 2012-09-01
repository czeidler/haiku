/*****************************************************************************
**
** quadedge.h: an include file for the Edge, QuadEdge, and Mesh classes.
**
** Copyright (C) 1995 by Dani Lischinski 
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
******************************************************************************/

#ifndef QUADEDGE_H
#define QUADEDGE_H

#include "geom2d.h"
#include "dllist.h"

class QuadEdge;
class Mesh;

class Edge {
	friend class QuadEdge;
	friend class Mesh;
	friend void Splice(Edge*, Edge*);
  private:
	char num, mark;
	Edge *next;
	Point2d *data;
  public:
	Edge()	{ data = 0; }
	Edge* Rot();
	Edge* invRot();
	Edge* Sym();
	Edge* Onext();
	Edge* Oprev();
	Edge* Dnext();
	Edge* Dprev();
	Edge* Lnext();	
	Edge* Lprev();
	Edge* Rnext();	
	Edge* Rprev();
	Point2d* Org();
	Point2d* Dest();
	const Point2d& Org2d() const;
	const Point2d& Dest2d() const;
	void  EndPoints(Point2d*, Point2d*);
	void Constrain();
	Boolean isConstrained();
	QuadEdge* QEdge() const    { return (QuadEdge *)(this - num); }
};

class QuadEdge {
	friend class Mesh;
  private:
	Edge e[4];
	Boolean c;
	LlistPos p;
  public:
	QuadEdge(Boolean);
	Edge *edges()           { return e; }
	Boolean isConstrained() { return c; }
	void Constrain()        { c = TRUE; }
	~QuadEdge();
};

class Mesh {
  private:
	Edge *startingEdge;
	Llist edges;
	void DeleteEdge(Edge*);
	Edge *MakeEdge(Boolean);
	Edge *MakeEdge(Point2d*, Point2d*, Boolean);
	Edge *Connect(Edge*, Edge*);
	Edge *Locate(const Point2d&);
	Edge *BruteForceLocate(const Point2d&);
	void SplitEdge(Edge*, const Point2d&);
	void Triangulate(Edge*);
	void MarkEdges(Edge*);
  public:
	Mesh(const Point2d&, const Point2d&, const Point2d&);
	Mesh(const Point2d&, const Point2d&, const Point2d&, const Point2d&);
	Mesh(int numVertices, double *bdryVertices);
	Edge *InsertSite(const Point2d&, Real dist = EPS);
	void InsertEdge(const Point2d&, const Point2d&);
	int  numEdges() const { return edges.length(); }
	void ApplyVertices( void (*f)( void *, void * ), void * );
	void ApplyEdges( void (*f)( void *, void *, Boolean ), void * );
	~Mesh();
};

inline QuadEdge::QuadEdge(Boolean constrained = FALSE)
{
	e[0].num = 0, e[1].num = 1, e[2].num = 2, e[3].num = 3;
	e[0].mark = 0, e[1].mark = 0, e[2].mark = 0, e[3].mark = 0;
	e[0].next = &(e[0]); e[1].next = &(e[3]);
	e[2].next = &(e[2]); e[3].next = &(e[1]);
	c = constrained;
}

/************************* Edge Algebra *************************************/

inline Edge* Edge::Rot()
// Return the dual of the current edge, directed from its right to its left. 
{
	return (num < 3) ? this + 1 : this - 3;
}

inline Edge* Edge::invRot()
// Return the dual of the current edge, directed from its left to its right. 
{
	return (num > 0) ? this - 1 : this + 3;
}

inline Edge* Edge::Sym()
// Return the edge from the destination to the origin of the current edge.
{
	return (num < 2) ? this + 2 : this - 2;
}
	
inline Edge* Edge::Onext()
// Return the next ccw edge around (from) the origin of the current edge.
{
	return next;
}

inline Edge* Edge::Oprev()
// Return the next cw edge around (from) the origin of the current edge.
{
	return Rot()->Onext()->Rot();
}

inline Edge* Edge::Dnext()
// Return the next ccw edge around (into) the destination of the current edge.
{
	return Sym()->Onext()->Sym();
}

inline Edge* Edge::Dprev()
// Return the next cw edge around (into) the destination of the current edge.
{
	return invRot()->Onext()->invRot();
}

inline Edge* Edge::Lnext()
// Return the ccw edge around the left face following the current edge.
{
	return invRot()->Onext()->Rot();
}

inline Edge* Edge::Lprev()
// Return the ccw edge around the left face before the current edge.
{
	return Onext()->Sym();
}

inline Edge* Edge::Rnext()
// Return the edge around the right face ccw following the current edge.
{
	return Rot()->Onext()->invRot();
}

inline Edge* Edge::Rprev()
// Return the edge around the right face ccw before the current edge.
{
	return Sym()->Onext();
}

/************** Access to non-topological info ******************************/

inline Point2d* Edge::Org()
{
	return data;
}

inline Point2d* Edge::Dest()
{
	return Sym()->data;
}

inline const Point2d& Edge::Org2d() const
{
	return *data;
}

inline const Point2d& Edge::Dest2d() const
{
	return (num < 2) ? *((this + 2)->data) : *((this - 2)->data);
}

inline void Edge::EndPoints(Point2d* oR, Point2d* de)
{
	data = oR;
	Sym()->data = de;
}

inline Boolean Edge::isConstrained()
{
	return QEdge()->isConstrained();
}

inline void Edge::Constrain()
{
	QEdge()->Constrain();
}

#endif /* QUADEDGE_H */
