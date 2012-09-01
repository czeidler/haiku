/*****************************************************************************
**
** geom2d.h: an include file containing class definitions and inline 
** functions related to 2D geometrical primitives: vectors/points and lines.
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

#ifndef GEOM2D_H
#define GEOM2D_H

#include <math.h>
#include <stdlib.h>
#include <iostream>
#include "common.h"


#define EPS 1e-6
#define X 0
#define Y 1
#define Z 2

typedef Lreal Real;

class Vector2d {
	Real x, y;
public:
	Vector2d()                          { x = 0; y = 0; }
	Vector2d(Real a, Real b)            { x = a; y = b; }
	Vector2d(const Vector2d &v)         { *this = v; }
	Real& operator[](int i)             { return ((Real *)this)[i]; }
	const Real& operator[](int i) const { return ((Real *)this)[i]; }
	Real norm() const;
	Real normalize();
	Boolean operator==(const Vector2d&) const;
	Vector2d operator+(const Vector2d&) const;
	Vector2d operator-(const Vector2d&) const;
	Real     operator|(const Vector2d&) const;
	friend Vector2d operator*(Real, const Vector2d&);
	friend Vector2d operator/(const Vector2d&, Real);
	friend Real dot(const Vector2d&, const Vector2d&);
	friend std::istream& operator>>(std::istream&, Vector2d&);
	friend std::ostream& operator<<(std::ostream&, const Vector2d&);
};

typedef Vector2d Point2d;

class Line {
public:
	Line()	{}
	Line(const Point2d&, const Point2d&);
	void set(const Point2d&, const Point2d&);
	Real eval(const Point2d&) const;
	int classify(const Point2d&) const;
	Point2d intersect(const Point2d&, const Point2d&) const;
private:
	Real a, b, c;
};

// Vector2d:

inline Real Vector2d::norm() const
{
	return sqrt(x * x + y * y);
}

inline Real Vector2d::normalize()
{
	Real len = norm();

	if (len == 0.0)
		std::cerr << "Vector2d::normalize: Division by 0\n";
	else {
		x /= len;
		y /= len;
	}
	return len;
}

inline Vector2d Vector2d::operator+(const Vector2d& v) const
{
	return Vector2d(x + v.x, y + v.y);
}

inline Vector2d Vector2d::operator-(const Vector2d& v) const
{
	return Vector2d(x - v.x, y - v.y);
}

inline Boolean Vector2d::operator==(const Vector2d& v) const
{
	return (*this - v).norm() <= EPS;
}

inline Real Vector2d::operator|(const Vector2d& v) const
// dot product (cannot overload the . operator)
{
	return x * v.x + y * v.y;
}

inline Vector2d operator*(Real c, const Vector2d& v)
{
	return Vector2d(c * v.x, c * v.y);
}

inline Vector2d operator/(const Vector2d& v, Real c)
{
	return Vector2d(v.x / c, v.y / c);
}

inline Real dot(const Vector2d& u, const Vector2d& v)
// another dot product
{
        return u.x * v.x + u.y * v.y;
}

inline std::ostream& operator<<(std::ostream& os, const Vector2d& v)
{
	os << '(' << v.x << ", " << v.y << ')';
	return os;
}

inline std::istream& operator>>(std::istream& is, Vector2d& v)
{
	is >> v.x >> v.y;
	return is;
}

// Line:

inline Line::Line(const Point2d& p, const Point2d& q)
// Computes the normalized line equation through the points p and q.
{
	Vector2d t = q - p;
	Real len = t.norm();

	a =   t[Y] / len;
	b = - t[X] / len;
	// c = -(a*p[X] + b*p[Y]);

	// less efficient, but more robust -- seth.
	c = -0.5 * ((a*p[X] + b*p[Y]) + (a*q[X] + b*q[Y]));
}

inline void Line::set(const Point2d& p, const Point2d& q)
{
	*this = Line(p, q);
}

inline Real Line::eval(const Point2d& p) const
// Plugs point p into the line equation.
{
	return (a * p[X] + b* p[Y] + c);
}

inline Point2d Line::intersect(const Point2d& p1, const Point2d& p2) const
// Returns the intersection of the line with the segment (p1,p2)
{
        // assumes that segment (p1,p2) crosses the line
        Vector2d d = p2 - p1;
        Real t = - eval(p1) / (a*d[X] + b*d[Y]);
        return (p1 + t*d);
}

inline int Line::classify(const Point2d& p) const
// Returns -1, 0, or 1, if p is to the left of, on,
// or right of the line, respectively.
{
	Real d = eval(p);
	return (d < -EPS) ? -1 : (d > EPS ? 1 : 0);
}

inline Boolean operator==(const Point2d& point, const Line& line)
// Returns TRUE if point is on the line (actually, on the EPS-slab
// around the line).
{
	Real tmp = line.eval(point);
	return(ABS(tmp) <= EPS);
}

inline Boolean operator<(const Point2d& point, const Line& line)
// Returns TRUE if point is to the left of the line (left to
// the EPS-slab around the line).
{
	return (line.eval(point) < -EPS);
}

#endif
