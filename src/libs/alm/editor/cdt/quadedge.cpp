/*****************************************************************************
**
** quadedge.C: routines for QuadEdge and Mesh classes and for contstruction
** of constrained Delaunay triangulations (CDTs). 
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

#include "quadedge.h"

using namespace std;

/*********************** Basic Topological Operators ************************/

Edge* Mesh::MakeEdge(Boolean constrained = FALSE)
{
	QuadEdge *qe = new QuadEdge(constrained);
	qe->p = edges.insert(edges.first(), qe);
	return qe->edges();
}

Edge* Mesh::MakeEdge(Point2d *a, Point2d *b, Boolean constrained = FALSE)
{
	Edge *e = MakeEdge(constrained);
	e->EndPoints(a, b);
	return e;
}

void Splice(Edge* a, Edge* b)
// This operator affects the two edge rings around the origins of a and b,
// and, independently, the two edge rings around the left faces of a and b.
// In each case, (i) if the two rings are distinct, Splice will combine
// them into one; (ii) if the two are the same ring, Splice will break it
// into two separate pieces. 
// Thus, Splice can be used both to attach the two edges together, and
// to break them apart. See Guibas and Stolfi (1985) p.96 for more details
// and illustrations.
{
	Edge* alpha = a->Onext()->Rot();
	Edge* beta  = b->Onext()->Rot();

	Edge* t1 = b->Onext();
	Edge* t2 = a->Onext();
	Edge* t3 = beta->Onext();
	Edge* t4 = alpha->Onext();

	a->next = t1;
	b->next = t2;
	alpha->next = t3;
	beta->next = t4;
}

void Mesh::DeleteEdge(Edge* e)
{
	// Make sure the starting edge does not get deleted:
	if (startingEdge->QEdge() == e->QEdge()) {
		Warning("Mesh::DeleteEdge: attempting to delete starting edge");
		// try to recover:
		startingEdge = (e != e->Onext()) ? e->Onext() : e->Dnext();
		Assert(startingEdge->QEdge() != e->QEdge());
	}

	// remove edge from the edge list:
	QuadEdge *qe = e->QEdge();
	edges.remove(edges.prev(qe->p));
	delete qe;
}

QuadEdge::~QuadEdge()
{
	// If there are no other edges from the origin or the destination
	// the corresponding data pointers should be deleted:
	if (e[0].data != NIL(Point2d) && e[0].next == e)
		delete e[0].data;
	if (e[2].data != NIL(Point2d) && e[2].next == (e+2))
		delete e[2].data;

	// Detach edge from the rest of the subdivision:
	Splice(e, e->Oprev());
	Splice(e->Sym(), e->Sym()->Oprev());
}

Mesh::~Mesh()
{
	QuadEdge *qp;
	for (LlistPos p = edges.first(); !edges.isEnd(p); p = edges.next(p)) {
		qp = (QuadEdge *)edges.retrieve(p);
		delete qp;
	}
}


/************* Topological Operations for Delaunay Diagrams *****************/

Mesh::Mesh(const Point2d& a, const Point2d& b, const Point2d& c)
// Initialize the mesh to the triangle defined by the points a, b, c.
{
	Point2d *da = new Point2d(a);
	Point2d *db = new Point2d(b);
	Point2d *dc = new Point2d(c);

	Edge* ea = MakeEdge(da, db, TRUE);
   	Edge* eb = MakeEdge(db, dc, TRUE);
	Edge* ec = MakeEdge(dc, da, TRUE);

	Splice(ea->Sym(), eb);
	Splice(eb->Sym(), ec);
	Splice(ec->Sym(), ea);

	startingEdge = ec;
}

Mesh::Mesh(const Point2d& a, const Point2d& b,
           const Point2d& c, const Point2d& d)
// Initialize the mesh to the Delaunay triangulation of
// the quadrilateral defined by the points a, b, c, d.
// NOTE: quadrilateral is assumed convex.
{
	Boolean InCircle(const Point2d&, const Point2d&,
	                 const Point2d&, const Point2d&);
	Point2d *da = new Point2d(a);
	Point2d *db = new Point2d(b);
	Point2d *dc = new Point2d(c);
	Point2d *dd = new Point2d(d);

	Edge* ea = MakeEdge(da, db, TRUE);
	Edge* eb = MakeEdge(db, dc, TRUE);
	Edge* ec = MakeEdge(dc, dd, TRUE);
	Edge* ed = MakeEdge(dd, da, TRUE);

	Splice(ea->Sym(), eb);
	Splice(eb->Sym(), ec);
	Splice(ec->Sym(), ed);
	Splice(ed->Sym(), ea);

	// Split into two triangles:
	if (InCircle(c, d, a, b)) {
		// Connect d to b:
		startingEdge = Connect(ec, eb);
	} else {
		// Connect c to a:
		startingEdge = Connect(eb, ea);
	}
}

// The following typedefs are necessary to avoid stupid warnings
// on some compilers:
typedef Point2d* Point2dPtr;
typedef Edge*    EdgePtr;

Mesh::Mesh( int numVertices, double *bdryVertices )
// Initialize the mesh to the Delaunay triangulation of the
// convex polygon defined by the coordinates in the bdryVertices
// array. The number of vertices (coordinate pairs) is specified
// via numVertices.
// NOTE: polygon is assumed convex.
{
	Point2d orig, dest;
	Edge *e0, *e1;
	register int i;

	Assert(numVertices >= 3);

	Point2d **verts = new Point2dPtr[numVertices + 1];
	Edge **edges = new EdgePtr[numVertices + 1];

	// Create all vertices:
	for (i = 0; i < numVertices; i++) {
		verts[i] = new Point2d(bdryVertices[2*i], bdryVertices[2*i+1]);
	}
	verts[numVertices] = verts[0];

	// Create all edges:
	for (i = 0; i < numVertices; i++) {
		edges[i] = MakeEdge(verts[i], verts[i+1], TRUE);
	}
	edges[numVertices] = edges[0];

	// Connect edges together:
	for (i = 0; i < numVertices; i++) {
		Splice(edges[i]->Sym(), edges[i+1]);
	}
	
	// Triangulate:
	Triangulate(edges[0]);

	// Initialize starting edge:
	startingEdge = edges[0];

	delete[] verts;
	delete[] edges;
}

Edge* Mesh::Connect(Edge* a, Edge* b)
// Add a new edge e connecting the destination of a to the
// origin of b, in such a way that all three have the same
// left face after the connection is complete.
// Additionally, the data pointers of the new edge are set.
{
	Edge* e = MakeEdge(a->Dest(), b->Org());
	Splice(e, a->Lnext());
	Splice(e->Sym(), b);
	return e;
}

void Swap(Edge* e)
// Essentially turns edge e counterclockwise inside its enclosing
// quadrilateral. The data pointers are modified accordingly.
{
	Edge* a = e->Oprev();
	Edge* b = e->Sym()->Oprev();
	Splice(e, a);
	Splice(e->Sym(), b);
	Splice(e, a->Lnext());
	Splice(e->Sym(), b->Lnext());
	e->EndPoints(a->Dest(), b->Dest());
}

Point2d snap(const Point2d& x, const Point2d& a, const Point2d& b)
{
	if (x == a)
	    return a;
	if (x == b)
	    return b;
	Real t1 = (x-a) | (b-a);
	Real t2 = (x-b) | (a-b);
	
	Real t = MAX(t1,t2) / (t1 + t2);
	
	return ((t1 > t2) ? ((1-t)*a + t*b) : ((1-t)*b + t*a));
}

void Mesh::SplitEdge(Edge *e, const Point2d& x)
// Shorten edge e s.t. its destination becomes x. Connect
// x to the previous destination of e by a new edge. If e
// is constrained, x is snapped onto the edge, and the new
// edge is also marked as constrained.
{
	Point2d *dt;

	if (e->isConstrained()) {
		// snap the point to the edge before splitting:
		dt = new Point2d(snap(x, e->Org2d(), e->Dest2d()));
	} else
	    dt = new Point2d(x);
	Edge *t  = e->Lnext();
	Splice(e->Sym(), t);
	e->EndPoints(e->Org(), dt);
	Edge *ne = Connect(e, t);
	if (e->isConstrained())
		ne->Constrain();
}


/*************** Geometric Predicates for Delaunay Diagrams *****************/

inline Real TriArea(const Point2d& a, const Point2d& b, const Point2d& c)
// Returns twice the area of the oriented triangle (a, b, c), i.e., the
// area is positive if the triangle is oriented counterclockwise.
{
	return (b[X] - a[X])*(c[Y] - a[Y]) - (b[Y] - a[Y])*(c[X] - a[X]);
}

Boolean InCircle(const Point2d& a, const Point2d& b,
                 const Point2d& c, const Point2d& d)
// Returns TRUE if the point d is inside the circle defined by the
// points a, b, c. See Guibas and Stolfi (1985) p.107.
{
	Real az = a | a;
	Real bz = b | b;
	Real cz = c | c;
	Real dz = d | d;

	Real det = (az * TriArea(b, c, d) - bz * TriArea(a, c, d) +
				cz * TriArea(a, b, d) - dz * TriArea(a, b, c));

	return (det > 0);
}

Boolean ccw(const Point2d& a, const Point2d& b, const Point2d& c)
// Returns TRUE if the points a, b, c are in a counterclockwise order
{
	Real det = TriArea(a, b, c);
	return (det > 0);
}

Boolean cw(const Point2d& a, const Point2d& b, const Point2d& c)
// Returns TRUE if the points a, b, c are in a clockwise order
{
	Real det = TriArea(a, b, c);
	return (det < 0);
}

Boolean RightOf(const Point2d& x, Edge* e)
// Returns TRUE if the point x is strictly to the right of e
{
	return ccw(x, *(e->Dest()), *(e->Org()));
}

Boolean LeftOf(const Point2d& x, Edge* e)
// Returns TRUE if the point x is strictly to the left of e
{
	return cw(x, *(e->Dest()), *(e->Org()));
}

Boolean OnEdge(const Point2d& x, Edge* e)
// A predicate that determines if the point x is on the edge e.
// The point is considered on if it is in the EPS-neighborhood
// of the edge.
{
	Point2d a = e->Org2d(), b = e->Dest2d();
	Real t1 = (x - a).norm(), t2 = (x - b).norm(), t3;
	if (t1 <= EPS || t2 <= EPS)
		return TRUE;
	t3 = (a - b).norm();
	if (t1 > t3 || t2 > t3)
	    return FALSE;
	Line line(a, b);
	return (x == line);
}

Point2d Intersect(Edge *e, const Line& l)
// Returns the intersection point between e and l (assumes it exists).
{
	return l.intersect(e->Org2d(), e->Dest2d());
}

Point2d CircumCenter(const Point2d& a, const Point2d& b, const Point2d& c)
// Returns the center of the circle through points a, b, c.
// From Graphics Gems I, p.22
{
	Real d1, d2, d3, c1, c2, c3;

	d1 = (b - a) | (c - a);
	d2 = (b - c) | (a - c);
	d3 = (a - b) | (c - b);
	c1 = d2 * d3;
	c2 = d3 * d1;
	c3 = d1 * d2;
	return ((c2 + c3)*a + (c3 + c1)*c + (c1 + c2)*b) / (2*(c1 + c2 + c3));
}


/************* An Incremental Algorithm for the Construction of *************/
/************************ Delaunay Diagrams *********************************/

Boolean hasLeftFace(Edge *e)
// Returns TRUE if there is a triangular face to the left of e.
{
	return (e->Lprev()->Org() == e->Lnext()->Dest() &&
	        LeftOf(e->Lprev()->Org2d(), e));
}
	
inline Boolean hasRightFace(Edge *e)
// Returns TRUE if there is a triangular face to the right of e.
{
	return hasLeftFace(e->Sym());
}
	
void FixEdge(Edge *e)
// A simple, but recursive way of doing things.
// Flips e, if necessary, in which case calls itself
// recursively on the two edges that were to the right
// of e before it was flipped, to propagate the change.
{
	if (e->isConstrained())
		return;
	Edge *f = e->Oprev();
	Edge *g = e->Dnext();
	if (InCircle(*(e->Dest()), *(e->Onext()->Dest()),
		         *(e->Org()), *(f->Dest()))) {
		Swap(e);
		FixEdge(f);
		FixEdge(g);
	}
}

void Mesh::Triangulate(Edge *first)
// Triangulates the left face of first, which is assumed to be closed.
// It is also assumed that all the vertices of that face lie to the
// left of last (the edge preceeding first).	
// This is NOT intended as a general simple polygon triangulation
// routine. It is called by InsertEdge in order to restore a
// triangulation after an edge has been inserted.
// The routine consists of two loops: the outer loop continues as
// long as the left face is not a triangle. The inner loop examines 
// successive triplets of vertices, creating a triangle whenever the
// triplet is counterclockwise.
{
	Edge *a, *b, *e, *t1, *t2, *last = first->Lprev();

	while (first->Lnext()->Lnext() != last) {
		e = first->Lnext();
		t1 = first;
		while (e != last) {
			t2 = e->Lnext();
			if (t2 == last && t1 == first)
			    break;
			if (LeftOf(e->Dest2d(), t1)) {
				if (t1 == first)
					t1 = first = Connect(e, t1)->Sym();
				else
					t1 = Connect(e, t1)->Sym();
				a = t1->Oprev(), b = t1->Dnext();
				FixEdge(a);
				FixEdge(b);
				e = t2;
			} else {
				t1 = e;
				e = t2;
			}
		}
	}
	a = last->Lnext(), b = last->Lprev();
	FixEdge(a);
	FixEdge(b);
	FixEdge(last);
}

static Boolean coincide(const Point2d& a, const Point2d& b, Real dist)
// Returns TRUE if the points a and b are closer than dist to each other.
// This is useful for creating nicer meshes, by preventing points from
// being too close to each other.
{
	Vector2d d = a - b;

	if (fabs(d[X]) > dist || fabs(d[Y]) > dist)
		return FALSE;

	return ((d|d) <= dist*dist);
}

Edge *Mesh::InsertSite(const Point2d& x, Real dist /* = EPS */)
// Inserts a new site into a CDT. This is basically the Guibas-Stolfi
// incremental site insertion algorithm, except that is does not flip
// constraining edges (in fact, it does not even test them.)
// Returns NIL(Edge) if insertion has failed; otherwise, returns an edge
// whose origin is the new site.
{
	Edge *e = Locate(x);
	if (e == NIL(Edge))
	    return e;

	if (coincide(x, e->Org2d(), dist))
		return e;
	if (coincide(x, e->Dest2d(), dist))
	    return e->Sym();

	Boolean hasLeft = hasLeftFace(e);
	Boolean hasRight = hasRightFace(e);

	if (!hasLeft && !hasRight) {
		Warning("InsertSite: edge does not have any faces");
		return NIL(Edge);
	}

	// x should be inside a face adjacent to e:
	Boolean onEdge = OnEdge(x, e);
	Boolean insideLeft  = hasLeft && (onEdge || LeftOf(x, e)) &&
	                      RightOf(x, e->Onext()) && RightOf(x, e->Dprev());
	Boolean insideRight = hasRight && (onEdge || RightOf(x, e)) &&
	                      LeftOf(x, e->Oprev()) && LeftOf(x, e->Dnext());
	if (!insideLeft && !insideRight) {
		Warning("InsertSite: point not in a face adjacent to edge");
		return NIL(Edge);
	}	                    

	if (insideLeft  && coincide(x, e->Onext()->Dest2d(), dist))
		return e->Lprev();
	if (insideRight && coincide(x, e->Oprev()->Dest2d(), dist))
		return e->Dnext();

	// Now we know, x lies within the quadrilateral whose diagonal is e
	// (or a triangle, if e only has one adjacent face). We also know x
	// is not one of e's endpoints.

	if (onEdge) {

		// snap x to e, and check for coincidence:
		Point2d xx = snap(x, e->Org2d(), e->Dest2d());
		if (coincide(xx, e->Org2d(), dist))
			return e;
		if (coincide(xx, e->Dest2d(), dist))
			return e->Sym();

		if (hasRight && hasLeft) {
			// has two faces
			Edge *a, *b, *c, *d;
			a = e->Oprev();
			b = e->Dnext();
			c = e->Lnext();
			d = e->Lprev();
			SplitEdge(e, x);
			Connect(e, e->Lprev());
			Connect(e->Oprev(), e->Sym());
			FixEdge(a);
			FixEdge(b);
			FixEdge(c);
			FixEdge(d);
		} else {
			// has only one face
			if (hasRight)
		        e = e->Sym();
			Edge *c = e->Lnext();
			Edge *d = e->Lprev();
			SplitEdge(e, x);
			Connect(e, e->Lprev());
			FixEdge(c);
			FixEdge(d);
		}
		startingEdge = e->Sym();
		return startingEdge;
	}

	// x is not on e, should be in face to the left of e:
	if (!insideLeft) {
		Warning("InsertSite: point is not to the left of edge");
		return NIL(Edge);
	}

	// x should be strictly inside the left face:
	if (OnEdge(x, e->Onext()) || OnEdge(x, e->Dprev())) {
		Warning("InsertSite: point is not strictly inside face");
		return NIL(Edge);
	}

	// Now, hopefully, x is strictly inside the left face of e,
	// so everything should proceed smoothly from this point on.

   	Point2d* first = e->Org();
	Edge* base = MakeEdge(e->Org(), new Point2d(x));
	Splice(base, e);
	// use this edge as the starting point next time:
	startingEdge = base->Sym();
	do {
		base = Connect(e, base->Sym());
		e = base->Oprev();
	} while (e->Dprev() != startingEdge);

	// Examine suspect edges to ensure that the Delaunay condition
	// is satisfied.
   	do {
		Edge* t = e->Oprev();
		if (!e->isConstrained() &&
			InCircle(e->Org2d(), t->Dest2d(), e->Dest2d(), x)) {
				Swap(e);
				e = e->Oprev();
			}
		else if (e->Lprev() == startingEdge)  // no more suspect edges
			return startingEdge;
		else  // pop a suspect edge
		    e = e->Onext()->Lprev();
	} while (TRUE);
}

void Mesh::InsertEdge(const Point2d& a, const Point2d& b)
// Inserts a constraining edge into a CDT.
{
	Edge *ea, *eb;
	Point2d aa, bb;
	if (ea = InsertSite(a))
	    aa = ea->Org2d();
	if (eb = InsertSite(b))
	    bb = eb->Org2d();
	if (ea == NIL(Edge) || eb == NIL(Edge)) {
		Warning("InsertEdge: could not insert endpoints of edge");
		return;
	}

	startingEdge = ea;
	if ((ea = Locate(aa)) == NIL(Edge)) {
		Warning("InsertEdge: could not locate an endpoint");
	    return;
	}
	if (!(aa == ea->Org2d()))
	    ea = ea->Sym();
	if (!(aa == ea->Org2d())) {
		Warning("InsertEdge: point a is not an endpoint of ea");
		return;
	}

	if (aa == bb) {
		Warning("InsertEdge: both ends map to same vertex");
		return;
	}

	// aa and bb are vertices in the mesh; our goal is to connect
	// them by a sequence of one or more edges.

	Line ab(aa, bb);
	Vector2d dd = bb - aa;
	Edge *t, *ne;
	Point2d *last = eb->Org(); 

	while (ea->Org() != last) {

		// Set ea to the first edge to the right of (or aligned with)
		// the segment (a,b), by moving ccw around the origin of ea:
		t = ea;
		do {
			if ((ea->Dest2d() == ab && ((ea->Dest2d() - aa)|dd) > 0))
				break;
			if (ea->Onext()->Dest2d() == ab &&
				((ea->Onext()->Dest2d() - aa)|dd) > 0) {
					ea = ea->Onext();
					break;
				}
			if (!cw(ea->Dest2d(), bb, aa) && cw(ea->Onext()->Dest2d(), bb, aa))
			    break;
			ea = ea->Onext();
			if (ea == t) {
				Warning("InsertEdge: infinite loop");
				return;
			}
		} while (TRUE);

		// check to see if an edge is already there:
		if (ea->Dest2d() == ab) {
			ea->Constrain();
			aa = ea->Dest2d();
			if (aa == bb)
				return;
			ab.set(aa, bb);
			dd = bb - aa;
			ea = ea->Sym()->Onext();
			continue;
		}

		t = ea;

		while (TRUE) {
			if (t->Lnext()->Dest2d() == ab) {
				if (t->Lnext()->Lnext()->Lnext() == ea) {
					// edge is already there
					t->Lnext()->Lnext()->Constrain();
					ea = t->Lnext()->Sym();
					break;
				} else {
					// need a new edge
					ne = Connect(t->Lnext(), ea); ne->Constrain();
					ea = t->Lnext()->Sym();
					Triangulate(ne->Lnext());
					Triangulate(ne->Oprev());
					break;
				}
			}
			if (t->Lnext()->Dest2d() < ab) {
				// edges cross
				if (!t->Lnext()->isConstrained()) { 
					DeleteEdge(t->Lnext());
				} else {
					// the crossing edge is also constrained
					// compute and insert the intersection
					Point2d x = Intersect(t->Lnext(), ab);
					// split t->Lnext() into two at x
					SplitEdge(t->Lnext(), x);
					// connect to the new vertex:
					ne = Connect(t->Lnext(), ea);
					ne->Constrain();
					ea = t->Lnext()->Sym();
					Triangulate(ne->Lnext());
					Triangulate(ne->Oprev());
					break;
				}
			} else {
				t = t->Lnext();
			}
		}
	}
}

Boolean BruteForceLocate = FALSE;

Edge* Mesh::Locate(const Point2d& x)
// The goals of this routine are as follows:
// - if x coincides with one or more vertices in the mesh, return an edge
//   whose origin is one such vertex;
// - otherwise, return the edge nearest to x oriented ccw around a face
//   containing x (up to numerical round-off); 
// In the event of failure, we pass the search to BruteForceLocate.
{
	if (::BruteForceLocate)
		return BruteForceLocate(x);

	Edge* e = startingEdge;
	int iterations = 0;

	while (TRUE) {
		iterations++;

		if (iterations > numEdges()) {
			Warning("Locate: infinite loop");
			return BruteForceLocate(x);
		}

		// first, examine the endpoints for coincidence:
		if (x == e->Org2d())
			return startingEdge = e;
		if (x == e->Dest2d())
			return startingEdge = e->Sym();
		
		// x isn't one of the endpoints.
		// orient e s.t. x is not to its right:
		if (RightOf(x, e))
			e = e->Sym();
		
		// x is not to the right of e.
		// now we need to test if x is in the left face of e:
		if (hasLeftFace(e)) {
			
			// does x coincide with the third vertex?
			if (x == e->Lprev()->Org2d())
				return startingEdge = e->Lprev();
			
			if (LeftOf(x, e->Onext())) {
				e = e->Onext();
				continue;
			}
			if (LeftOf(x, e->Dprev())) {
				e = e->Dprev();
				continue;
			}
			
			// x is inside the left face of e!
			// return the edge nearest to x:
			Line l1(e->Org2d(), e->Dest2d());
			Line l2(e->Dest2d(), e->Lprev()->Org2d());
			Line l3(e->Lprev()->Org2d(), e->Org2d());
			switch (imin(fabs(l1.eval(x)), fabs(l2.eval(x)), fabs(l3.eval(x)))) {
			  case 0:
				startingEdge = e; break;
			  case 1:
				startingEdge = e->Lnext(); break;
			  case 2:
				startingEdge = e->Lprev(); break;
			}
			return startingEdge;
		}
		
		// there is no left face!
		if (OnEdge(x, e))
			return startingEdge = e->Sym();

		// ok, let's try to get closer using the edges of the right face
		if (RightOf(x, e->Oprev())) {
			e = e->Oprev()->Sym();
			continue;
		}
		if (RightOf(x, e->Dnext())) {
			e = e->Dnext()->Sym();
			continue;
		}

		Warning("Locate: I'm stuck and I can't move on");
		return BruteForceLocate(x);
	}	
}

Edge* Mesh::BruteForceLocate(const Point2d& x)
// Same as Locate, but uses a brute force exhaustive search.
{
	Edge *e;

	for (LlistPos p = edges.first(); !edges.isEnd(p); p = edges.next(p)) {

		e = ((QuadEdge *)edges.retrieve(p))->edges();
		// first, examine the endpoints for coincidence:
		if (x == e->Org2d())
			return startingEdge = e;
		if (x == e->Dest2d())
			return startingEdge = e->Sym();

		// x isn't one of the endpoints.
		// orient e s.t. x is not to its right:
		if (RightOf(x, e))
			e = e->Sym();

		// x is not to the right of e.
		// now we need to test if x is in the left face of e:
		if (hasLeftFace(e)) {

			// does x coincide with the third vertex?
			if (x == e->Lprev()->Org2d())
				return startingEdge = e->Lprev();

			if (!RightOf(x, e->Lprev()) && !RightOf(x, e->Lnext())) {
				// x is in this face!
				// return the edge nearest to x:
				Line l1(e->Org2d(), e->Dest2d());
				Line l2(e->Dest2d(), e->Lprev()->Org2d());
				Line l3(e->Lprev()->Org2d(), e->Org2d());
				switch(imin(fabs(l1.eval(x)), fabs(l2.eval(x)), fabs(l3.eval(x)))) {
				  case 0:
					startingEdge = e; break;
				  case 1:
					startingEdge = e->Lnext(); break;
				  case 2:
					startingEdge = e->Lprev(); break;
				}
				return startingEdge;
			} else
				continue;
		} 

		// there is no left face!
		if (OnEdge(x, e))
			return startingEdge = e->Sym();
		else
			continue;

	}

	// if we're here, it must be because we failed :-{
	Warning("Locate: Could not locate using brute force");
	return NIL(Edge);
}


/************************* Mesh Traversal Routines **************************/

#define UNVISITED 0
#define VISITED   1

inline void Mesh::MarkEdges(Edge *e)
{
	register Edge *t = e;
	do {
		t->mark = VISITED;
		t = t->Onext();
	} while (t != e);
}

void Mesh::ApplyEdges( void (*f)( void *arg, void *edge, Boolean ), void *arg )
// Applies the function f to every edge in the edge-list. The function
// takes application dependent arguments pointed to by arg, a pointer
// to the edge, and a flag that specifies whether or not the edge is
// constrained. This last argument allows the application to handle
// constrained edges differently.
{
	register QuadEdge *qp;
	register LlistPos p;

	for (p = edges.first(); !edges.isEnd(p); p = edges.next(p)) {
	    qp = (QuadEdge *)edges.retrieve(p);
	    f(arg, qp->edges(), qp->isConstrained()); 
	} 	
}

void Mesh::ApplyVertices( void (*f)( void *arg, void *vertex ), void *arg )
// Applies the function f to every vertex in the mesh. This is done
// similarly to ApplyEdges, but care must be taken to apply only once
// to each vertex. Every time f is applied to a vertex, all edges
// originating at that vertex are marked as VISITED. When we traverse
// the edge-list we only apply f to the origins of UNVISITED edges.
// After the traversal is complete, we go over the edges once more to
// reset the marks.
{
	register Edge *e;
	register LlistPos p;

	for (p = edges.first(); !edges.isEnd(p); p = edges.next(p)) {
	    e = ((QuadEdge *)edges.retrieve(p))->edges();
		if (e->mark == UNVISITED) {
			f(arg, e->Org());
			MarkEdges(e);
		}
		if (e->Sym()->mark == UNVISITED) {
			f(arg, e->Sym()->Org());
			MarkEdges(e->Sym());
		}
	} 	

	for (p = edges.first(); !edges.isEnd(p); p = edges.next(p)) {
	    e = ((QuadEdge *)edges.retrieve(p))->edges();
		e->mark = e->Sym()->mark = UNVISITED;
	}
}

