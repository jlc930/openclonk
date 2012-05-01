/*
 * OpenClonk, http://www.openclonk.org
 *
 * Copyright (c) 2012  Armin Burgmeier
 *
 * Portions might be copyrighted by other authors who have contributed
 * to OpenClonk.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * See isc_license.txt for full license and disclaimer.
 *
 * "Clonk" is a registered trademark of Matthes Bender.
 * See clonk_trademark_license.txt for full license.
 */

#include <C4Include.h>
#include <C4Landscape.h>
#include <C4Rope.h>

// TODO: For all square roots, we must avoid floating point by using an
// integer-based Sqrt algorithm
// TODO: Could also use an approximation which works without Sqrt, especially
// if this becomes a performance bottleneck. http://www.azillionmonkeys.com/qed/sqroot.html

namespace
{
	struct Vertex {
		Vertex() {}
		Vertex(float x, float y): x(x), y(y) {}

		float x;
		float y;
	};

	struct DrawVertex: Vertex {
		float u;
		float v;
	};

	// For use in initializer list
	C4Real ObjectDistance(C4Object* first, C4Object* second)
	{
		C4Real dx = second->fix_x - first->fix_x;
		C4Real dy = second->fix_y - first->fix_y;
		return ftofix(sqrt(fixtof(dx*dx + dy*dy))); // TODO: Replace by integer sqrt
	}

	// Helper function for Draw: determines vertex positions for one segment
	void VertexPos(Vertex& out1, Vertex& out2, Vertex& out3, Vertex& out4,
		             const Vertex& v1, const Vertex& v2, float w)
	{
		const float l = sqrt( (v1.x - v2.x)*(v1.x - v2.x) + (v1.y - v2.y)*(v1.y - v2.y));

		out1.x = v1.x + w/2.0f * (v1.y - v2.y) / l;
		out1.y = v1.y - w/2.0f * (v1.x - v2.x) / l;
		out2.x = v1.x - w/2.0f * (v1.y - v2.y) / l;
		out2.y = v1.y + w/2.0f * (v1.x - v2.x) / l;
		out3.x = v2.x + w/2.0f * (v1.y - v2.y) / l;
		out3.y = v2.y - w/2.0f * (v1.x - v2.x) / l;
		out4.x = v2.x - w/2.0f * (v1.y - v2.y) / l;
		out4.y = v2.y + w/2.0f * (v1.x - v2.x) / l;
	}

	// Copied from StdGL.cpp... actually the rendering code should be moved
	// there so we don't need to duplicate it here.
	bool ApplyZoomAndTransform(float ZoomX, float ZoomY, float Zoom, C4BltTransform* pTransform)
	{
		// Apply zoom
		glTranslatef(ZoomX, ZoomY, 0.0f);
		glScalef(Zoom, Zoom, 1.0f);
		glTranslatef(-ZoomX, -ZoomY, 0.0f);

		// Apply transformation
		if (pTransform)
		{
			const GLfloat transform[16] = { pTransform->mat[0], pTransform->mat[3], 0, pTransform->mat[6], pTransform->mat[1], pTransform->mat[4], 0, pTransform->mat[7], 0, 0, 1, 0, pTransform->mat[2], pTransform->mat[5], 0, pTransform->mat[8] };
			glMultMatrixf(transform);

			// Compute parity of the transformation matrix - if parity is swapped then
			// we need to cull front faces instead of back faces.
			const float det = transform[0]*transform[5]*transform[15]
			                  + transform[4]*transform[13]*transform[3]
			                  + transform[12]*transform[1]*transform[7]
			                  - transform[0]*transform[13]*transform[7]
			                  - transform[4]*transform[1]*transform[15]
			                  - transform[12]*transform[5]*transform[3];
			return det > 0;
		}

		return true;
	}
}

C4RopeElement::C4RopeElement(C4Object* obj, bool fixed):
	Fixed(fixed), fx(Fix0), fy(Fix0), rx(Fix0), ry(Fix0), rdt(Fix0),
	Next(NULL), Prev(NULL), Object(obj)
{
}

C4RopeElement::C4RopeElement(C4Real x, C4Real y, C4Real m, bool fixed):
	Fixed(fixed), x(x), y(y), vx(Fix0), vy(Fix0), m(m),
	fx(Fix0), fy(Fix0), rx(Fix0), ry(Fix0), rdt(Fix0),
	Next(NULL), Prev(NULL), Object(NULL)
{
}

void C4RopeElement::AddForce(C4Real x, C4Real y)
{
	fx += x;
	fy += y;
}

void C4RopeElement::Execute(const C4Rope* rope, C4Real dt)
{
	ResetForceRedirection(dt);

	// If attached object is contained, apply force to container
	C4Object* Target = Object;
	if(Target)
		while(Target->Contained)
			Target = Target->Contained;

	// Apply stricking friction
	if(!Target)
	{
		// Sticking friction: If a segment has contact with the landscape and it
		// is at rest then one needs to exceed a certain threshold force until it
		// starts moving.
		int ix = fixtoi(x);
		int iy = fixtoi(y);
		if(GBackSolid(ix+1, iy) || GBackSolid(ix-1, iy) || GBackSolid(ix, iy-1) || GBackSolid(ix, iy+1))
		{
			if(vx*vx + vy*vy < Fix1/4)
			{
				if(fx*fx + fy*fy < Fix1/4)
				{
					fx = fy = Fix0;
					vx = vy = Fix0;
					return;
				}
			}
		}
	}

	// Apply forces
	if(!Fixed)
	{
		if(!Target)
		{
			vx += dt * fx / m;
			vy += dt * fy / m;
		}
		else
		{
			Target->xdir += dt * fx / Target->Mass;
			Target->ydir += dt * fy / Target->Mass;
		}
	}
	fx = fy = Fix0;

	// Execute movement
	if(!Target)
	{
		int old_x = fixtoi(x);
		int old_y = fixtoi(y);
		int new_x = fixtoi(x + dt * vx);
		int new_y = fixtoi(y + dt * vy);
		int max_p = Max(abs(new_x - old_x), abs(new_y - old_y));

		int prev_x = old_x;
		int prev_y = old_y;
		bool hit = false;
		for(int i = 1; i <= max_p; ++i)
		{
			int inter_x = old_x + i * (new_x - old_x) / max_p;
			int inter_y = old_y + i * (new_y - old_y) / max_p;
			if(GBackSolid(inter_x, inter_y))
			{
				x = itofix(prev_x);
				y = itofix(prev_y);
				hit = true;

				// Apply friction force
				fx -= rope->GetOuterFriction() * vx; fy -= rope->GetOuterFriction() * vy; 

				// Force redirection so that not every single pixel on a
				// chunky landscape is an obstacle for the rope.
				SetForceRedirection(rope);
				break;
			}

			prev_x = inter_x;
			prev_y = inter_y;
		}

		if(!hit)
		{
			x += dt * vx;
			y += dt * vy;
		}
	}
	else
	{
		if(!Fixed)
		{
			// Object Force redirection
			if(Target->xdir*Target->xdir + Target->ydir*Target->ydir >= Fix1)
			{
				// Check if the object has contact to the landscape
				long iResult = 0;
				const DWORD dwCNATCheck = CNAT_Left | CNAT_Right | CNAT_Top | CNAT_Bottom;
				for (int i = 0; i < Target->Shape.VtxNum; ++i)
					iResult |= Target->Shape.GetVertexContact(i, dwCNATCheck, Target->GetX(), Target->GetY());

				// TODO: Check force redirection at the vertex which has contact
				if(iResult)
					SetForceRedirection(rope);
			}
		}
	}
}

void C4RopeElement::ResetForceRedirection(C4Real dt)
{
	// countdown+reset force redirection
	if(rdt != Fix0)
	{
		if(dt > rdt)
		{
			rx = ry = Fix0;
			rdt = Fix0;
		}
		else
		{
			rdt -= dt;
		}
	}
}

void C4RopeElement::SetForceRedirection(const C4Rope* rope)
{
	// The procedure is the following: we try manuevering around
	// the obstacle, either left or right.  Which way we take is determined by
	// checking whether or not there is solid material in a 75 degree angle
	// relative to the direction of movement.
	const C4Real Cos75 = Cos(itofix(75));
	const C4Real Sin75 = Sin(itofix(75));

	C4Real vx1 =  Cos75 * GetVx() + Sin75 * GetVy();
	C4Real vy1 = -Sin75 * GetVx() + Cos75 * GetVy();
	C4Real vx2 =  Cos75 * GetVx() - Sin75 * GetVy();
	C4Real vy2 =  Sin75 * GetVx() + Cos75 * GetVy();
	const C4Real v = ftofix(sqrt(fixtof(GetVx()*GetVx() + GetVy()*GetVy())));

	const C4Real l = rope->GetSegmentLength();

	// TODO: We should check more than a single pixel. There's some more potential for optimization here.
	if(v != Fix0 && !GBackSolid(fixtoi(GetX() + vx1*l/v), fixtoi(GetY() + vy1*l/v)))
		{ rx = vx1/v; ry = vy1/v; rdt = Fix1/4; } // Enable force redirection for 1/4th of a frame
	else if(v != Fix0 && !GBackSolid(fixtoi(GetX() + vx2/v), fixtoi(GetY() + vy2/v)))
		{ rx = vx2/v; ry = vy2/v; rdt = Fix1/4; } // Enable force redirection for 1/4th of a frame
}

C4Rope::C4Rope(C4PropList* Prototype, C4Object* first_obj, C4Object* second_obj, int32_t n_segments, C4DefGraphics* graphics):
	C4PropListNumbered(Prototype), Width(5.0f), Graphics(graphics), SegmentCount(n_segments),
	l(ObjectDistance(first_obj, second_obj) / (n_segments + 1)), k(Fix1*3), mu(Fix1*3), eta(Fix1*3), NumIterations(20)
{
	if(!PathFree(first_obj->GetX(), first_obj->GetY(), second_obj->GetX(), second_obj->GetY()))
		throw C4RopeError("Path between objects is blocked");
	if(n_segments < 1)
		throw C4RopeError("Segments < 1 given");
	if(Graphics->Type != C4DefGraphics::TYPE_Bitmap)
		throw C4RopeError("Can only use bitmap as rope graphics");

	Front = new C4RopeElement(first_obj, true);
	Back = new C4RopeElement(second_obj, false);

	const C4Real m(Fix1); // TODO: This should be a property

	C4RopeElement* prev_seg = Front;
	for(int32_t i = 0; i < n_segments; ++i)
	{
		// Create new element
		C4Real seg_x = first_obj->fix_x + (second_obj->fix_x - first_obj->fix_x) * (i+1) / (n_segments+1);
		C4Real seg_y = first_obj->fix_y + (second_obj->fix_y - first_obj->fix_y) * (i+1) / (n_segments+1);
		C4RopeElement* seg = new C4RopeElement(seg_x, seg_y, m, false);

		// Link it
		seg->Prev = prev_seg;
		prev_seg->Next = seg;
		prev_seg = seg;
	}

	// Link back segment
	prev_seg->Next = Back;
	Back->Prev = prev_seg;
}

C4Rope::~C4Rope()
{
	for(C4RopeElement* cur = Front, *next; cur != NULL; cur = next)
	{
		next = cur->Next;
		delete cur;
	}
}

void C4Rope::Solve(C4RopeElement* prev, C4RopeElement* next)
{
	// Rope forces
	C4Real dx = prev->GetX() - next->GetX();
	C4Real dy = prev->GetY() - next->GetY();

	C4Real d = ftofix(sqrt(fixtof(dx*dx + dy*dy))); 
	if(d != 0 && (ApplyRepulsive || d > l))
	{
		C4Real fx = (dx / d) * k * (d - l);
		C4Real fy = (dy / d) * k * (d - l);

		// If force redirection is active, move into direction of redirected force
		// instead. This is used to pull around corners in the landscape.

		if(prev->rdt == Fix0)
			prev->AddForce(-fx, -fy);
		else
			prev->AddForce(prev->rx * k * (d - l), prev->ry * k * (d - l));

		if(next->rdt == Fix0)
			next->AddForce(fx, fy);
		else
			next->AddForce(next->rx * k * (d - l), next->ry * k * (d - l));
	}

	// Inner friction
	// TODO: This is very sensitive to numerical instabilities for mid-to-high
	// eta values. We might want to prevent a sign change of either F or V induced
	// by this factor.
	// TODO: Don't apply inner friction for segments connected to fixed rope ends?
	C4Real fx = (prev->GetVx() - next->GetVx()) * eta;
	C4Real fy = (prev->GetVy() - next->GetVy()) * eta;
	prev->AddForce(-fx, -fy);
	next->AddForce(fx, fy);

	// Could add air/water friction here

	// TODO: Apply gravity separately. This is applied twice now for
	// non-end rope segments!

	// Don't apply gravity to objects since it's applied already in C4Object execution.
	prev->AddForce(Fix0, (prev->GetObject() ? Fix0 : prev->GetMass() * ::Landscape.Gravity/5));
	next->AddForce(Fix0, (prev->GetObject() ? Fix0 : next->GetMass() * ::Landscape.Gravity/5));
}

void C4Rope::Execute()
{
	C4Real dt = itofix(1, NumIterations);
	for(unsigned int i = 0; i < NumIterations; ++i)
	{
		for(C4RopeElement* cur = Front; cur->Next != NULL; cur = cur->Next)
			Solve(cur, cur->Next);

		for(C4RopeElement* cur = Front; cur != NULL; cur = cur->Next)
			cur->Execute(this, dt);
	}
}

// TODO: Move this to StdGL
void C4Rope::Draw(C4TargetFacet& cgo, C4BltTransform* pTransform)
{
#if 1
	Vertex Tmp[4];
	DrawVertex* Vertices = new DrawVertex[SegmentCount*2+4]; // TODO: Use a vbo and map it into memory instead?
	const float rsl = fixtof(l)/5.0 * Graphics->GetBitmap()->Wdt / Graphics->GetBitmap()->Hgt; // rope segment length mapped to Gfx bitmap

	VertexPos(Vertices[0], Vertices[1], Tmp[0], Tmp[1],
	          Vertex(fixtof(Front->GetX()), fixtof(Front->GetY())),
	          Vertex(fixtof(Front->Next->GetX()), fixtof(Front->Next->GetY())), Width);

	Vertices[0].u = 0.0f;
	Vertices[0].v = 0.0f;
	Vertices[1].u = 1.0f;
	Vertices[1].v = 0.0f;

	unsigned int i = 2;
	bool parity = true;
	for(C4RopeElement* cur = Front->Next; cur->Next != NULL; cur = cur->Next, i += 2)
	{
		Vertex v1(fixtof(cur->GetX()),
		          fixtof(cur->GetY()));
		Vertex v2(fixtof(cur->Next ? cur->Next->GetX() : Back->GetX()), 
		          fixtof(cur->Next ? cur->Next->GetY() : Back->GetY()));
		Vertex v3(fixtof(cur->Prev ? cur->Prev->GetX() : Front->GetX()),
		          fixtof(cur->Prev ? cur->Prev->GetY() : Front->GetY()));

		// Parity -- parity swaps for each pointed angle (<90 deg)
		float cx = v1.x - v3.x;
		float cy = v1.y - v3.y;
		float ex = v1.x - v2.x;
		float ey = v1.y - v2.y;
		
		// TODO: Another way to draw this would be to insert a "pseudo" segment so that there are no pointed angles at all
		if(cx*ex+cy*ey > 0)
			parity = !parity;

		// Obtain vertex positions
		if(parity)
			VertexPos(Tmp[2], Tmp[3], Vertices[i+2], Vertices[i+3], v1, v2, Width);
		else
			VertexPos(Tmp[3], Tmp[2], Vertices[i+3], Vertices[i+2], v1, v2, Width);

		Tmp[2].x = (Tmp[0].x + Tmp[2].x)/2.0f;
		Tmp[2].y = (Tmp[0].y + Tmp[2].y)/2.0f;
		Tmp[3].x = (Tmp[1].x + Tmp[3].x)/2.0f;
		Tmp[3].y = (Tmp[1].y + Tmp[3].y)/2.0f;

		// renormalize
		float dx = Tmp[3].x - Tmp[2].x;
		float dy = Tmp[3].y - Tmp[2].y;
		float dx2 = Vertices[i-1].x - Vertices[i-2].x;
		float dy2 = Vertices[i-1].y - Vertices[i-2].y;
		const float d = (dx2*dx2+dy2*dy2)/(dx*dx2+dy*dy2);
		Vertices[i  ].x = ( (Tmp[2].x + Tmp[3].x)/2.0f) - BoundBy((Tmp[3].x - Tmp[2].x)*d, -Width, Width)/2.0f;
		Vertices[i  ].y = ( (Tmp[2].y + Tmp[3].y)/2.0f) - BoundBy((Tmp[3].y - Tmp[2].y)*d, -Width, Width)/2.0f;
		Vertices[i+1].x = ( (Tmp[2].x + Tmp[3].x)/2.0f) + BoundBy((Tmp[3].x - Tmp[2].x)*d, -Width, Width)/2.0f;
		Vertices[i+1].y = ( (Tmp[2].y + Tmp[3].y)/2.0f) + BoundBy((Tmp[3].y - Tmp[2].y)*d, -Width, Width)/2.0f;

		Vertices[i].u = 0.0f; //parity ? 0.0f : 1.0f;
		Vertices[i].v = i/2 * rsl;
		Vertices[i+1].u = 1.0f; //parity ? 1.0f : 0.0f;
		Vertices[i+1].v = i/2 * rsl;

		Tmp[0] = Vertices[i+2];
		Tmp[1] = Vertices[i+3];
	}

	Vertices[i].u = 0.0f; //parity ? 0.0f : 1.0f;
	Vertices[i].v = i/2 * rsl;
	Vertices[i+1].u = 1.0f; //parity ? 1.0f : 0.0f;
	Vertices[i+1].v = i/2 * rsl;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, (*Graphics->GetBitmap()->ppTex)->texName);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glVertexPointer(2, GL_FLOAT, sizeof(DrawVertex), &Vertices->x);
	glTexCoordPointer(2, GL_FLOAT, sizeof(DrawVertex), &Vertices->u);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDrawArrays(GL_QUAD_STRIP, 0, SegmentCount*2+4);

	glDisable(GL_TEXTURE_2D);
	//glDisable(GL_BLEND);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	delete[] Vertices;

#else
	// Debug:
	for(C4RopeElement* cur = Front; cur != NULL; cur = cur->Next)
	{
		if(!cur->GetObject())
		{
			glBegin(GL_QUADS);
			glColor3f(1.0f, 0.0f, 0.0f);
			glVertex2f(fixtof(cur->x)-1.5f, fixtof(cur->y)-1.5f);
			glVertex2f(fixtof(cur->x)-1.5f, fixtof(cur->y)+1.5f);
			glVertex2f(fixtof(cur->x)+1.5f, fixtof(cur->y)+1.5f);
			glVertex2f(fixtof(cur->x)+1.5f, fixtof(cur->y)-1.5f);
			glEnd();
		}

		const float vx = fixtof(cur->GetVx());
		const float vy = fixtof(cur->GetVy());
		const float v = sqrt(vx*vx + vy*vy);
		if(v > 0.1)
		{
			glBegin(GL_TRIANGLES);
			glColor3f(BoundBy(v/2.5f, 0.0f, 1.0f), 0.0f, 1.0f);
			glVertex2f(fixtof(cur->GetX()) + vx/v*4.0f, fixtof(cur->GetY()) + vy/v*4.0f);
			glVertex2f(fixtof(cur->GetX()) - vy/v*1.5f, fixtof(cur->GetY()) + vx/v*1.5f);
			glVertex2f(fixtof(cur->GetX()) + vy/v*1.5f, fixtof(cur->GetY()) - vx/v*1.5f);
			glEnd();
		}

		const float rx = fixtof(cur->rx);
		const float ry = fixtof(cur->ry);
		const float r = sqrt(rx*rx + ry*ry);
		if(r > 0.1)
		{
			glBegin(GL_TRIANGLES);
			glColor3f(0.0f, BoundBy(r/2.5f, 0.0f, 1.0f), 1.0f);
			glVertex2f(fixtof(cur->x) + rx/r*4.0f, fixtof(cur->y) + ry/r*4.0f);
			glVertex2f(fixtof(cur->x) - ry/r*1.5f, fixtof(cur->y) + rx/r*1.5f);
			glVertex2f(fixtof(cur->x) + ry/r*1.5f, fixtof(cur->y) - rx/r*1.5f);
			glEnd();
		}
	}
#endif
}

C4RopeList::C4RopeList()
{
	for(unsigned int i = 0; i < Ropes.size(); ++i)
		delete Ropes[i];
}

C4Rope* C4RopeList::CreateRope(C4Object* first_obj, C4Object* second_obj, int32_t n_segments, C4DefGraphics* graphics)
{
	Ropes.push_back(new C4Rope(RopeAul.GetPropList(), first_obj, second_obj, n_segments, graphics));
	return Ropes.back();
}

void C4RopeList::Execute()
{
	for(unsigned int i = 0; i < Ropes.size(); ++i)
		Ropes[i]->Execute();
}

void C4RopeList::Draw(C4TargetFacet& cgo, C4BltTransform* pTransform)
{
	ZoomData z;
	pDraw->GetZoom(&z);

	glPushMatrix();
	ApplyZoomAndTransform(z.X, z.Y, z.Zoom, pTransform);
	glTranslatef(cgo.X-cgo.TargetX, cgo.Y-cgo.TargetY, 0.0f);

	for(unsigned int i = 0; i < Ropes.size(); ++i)
		Ropes[i]->Draw(cgo, pTransform);

	glPopMatrix();
}
