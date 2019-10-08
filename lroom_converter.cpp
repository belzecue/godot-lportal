//	Copyright (c) 2019 Lawnjelly

//	Permission is hereby granted, free of charge, to any person obtaining a copy
//	of this software and associated documentation files (the "Software"), to deal
//	in the Software without restriction, including without limitation the rights
//	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//	copies of the Software, and to permit persons to whom the Software is
//	furnished to do so, subject to the following conditions:

//	The above copyright notice and this permission notice shall be included in all
//	copies or substantial portions of the Software.

//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//	SOFTWARE.


#include "lroom_converter.h"
#include "lroom_manager.h"
#include "lportal.h"
#include "scene/3d/mesh_instance.h"
#include "core/math/quick_hull.h"
#include "ldebug.h"
#include "scene/3d/light.h"

// save typing, I am lazy
#define LMAN m_pManager



void LRoomConverter::Convert(LRoomManager &manager)
{
	// This just is simply used to set how much debugging output .. more during conversion, less during running
	// except when requested by explicitly clearing this flag.
	Lawn::LDebug::m_bRunning = false;
	LPRINT(5, "running convert");

	LMAN = &manager;

	// force clear all arrays
	manager.ReleaseResources(true);

	int count = CountRooms();

	int num_global_lights = LMAN->m_Lights.size();

	// make sure bitfield is right size for number of rooms
	LMAN->m_BF_visible_rooms.Create(count);

	LMAN->m_Rooms.resize(count);

	m_TempRooms.clear(true);
	m_TempRooms.resize(count);


	Convert_Rooms();
	Convert_Portals();
	Convert_Bounds();

	// make sure manager bitfields are the correct size for number of objects
	int num_sobs = LMAN->m_SOBs.size();
	LPRINT(5,"Total SOBs " + itos(num_sobs));
	LMAN->m_BF_caster_SOBs.Create(num_sobs);
	LMAN->m_BF_visible_SOBs.Create(num_sobs);
	LMAN->m_BF_master_SOBs.Create(num_sobs);
	LMAN->m_BF_master_SOBs_prev.Create(num_sobs);

	LMAN->m_BF_ActiveLights.Create(LMAN->m_Lights.size());
	LMAN->m_BF_ActiveLights_prev.Create(LMAN->m_Lights.size());

	// must be done after the bitfields
	Convert_Lights();
	Convert_ShadowCasters();

	// hide all in preparation for first frame
	Convert_HideAll();

	// temp rooms no longer needed
	m_TempRooms.clear(true);

	// clear out the local room lights, leave only global lights
	//LMAN->m_Lights.resize(num_global_lights);
	Lawn::LDebug::m_bRunning = true;
}




void LRoomConverter::Convert_Rooms()
{
	LPRINT(5,"Convert_Rooms");

	// first find all room empties and convert to LRooms
	int count = 0;

	for (int n=0; n<LMAN->get_child_count(); n++)
	{
		Node * pChild = LMAN->get_child(n);

		if (!Node_IsRoom(pChild))
			continue;

		Spatial * pSpat = Object::cast_to<Spatial>(pChild);
		assert (pSpat);

		Convert_Room(pSpat, count++);
	}

}

int LRoomConverter::FindRoom_ByName(String szName) const
{
	for (int n=0; n<LMAN->m_Rooms.size(); n++)
	{
		if (LMAN->m_Rooms[n].m_szName == szName)
			return n;
	}

	return -1;
}

void LRoomConverter::Convert_Room_SetDefaultCullMask_Recursive(Node * pParent)
{
	int nChildren = pParent->get_child_count();
	for (int n=0; n<nChildren; n++)
	{
		Node * pChild = pParent->get_child(n);

		// default cull mask should always be visible to camera and lights
		VisualInstance * pVI = Object::cast_to<VisualInstance>(pChild);
		if (pVI)
		{
//			LRoom::SoftShow(pVI, LRoom::LAYER_MASK_CAMERA | LRoom::LAYER_MASK_LIGHT);
		}

		Convert_Room_SetDefaultCullMask_Recursive(pChild);
	}
}


void LRoomConverter::Convert_Room_FindObjects_Recursive(Node * pParent, LRoom &lroom, LAABB &bb_room)
{
	int nChildren = pParent->get_child_count();
	for (int n=0; n<nChildren; n++)
	{
		Node * pChild = pParent->get_child(n);

		// we are not interested in portal meshes, as they will be deleted later in conversion
		if (Node_IsPortal(pChild))
			continue;
		// we can optionally ignore nodes (they will still be shown / hidden with the room though)
		if (Node_IsIgnore(pChild))
			continue;
		// not interested in bounds
		if (Node_IsBound(pChild))
			continue;

		// lights
		if (Node_IsLight(pChild))
		{
			LRoom_DetectedLight(lroom, pChild);
			continue;
		}

		VisualInstance * pVI = Object::cast_to<VisualInstance>(pChild);
		if (pVI)
		{


			LPRINT(2, "\t\tFound VI : " + pVI->get_name());


			// update bound to find centre of room roughly
			AABB bb = pVI->get_transformed_aabb();
			bb_room.ExpandToEnclose(bb);

			// store some info about the static object for use at runtime
			LSob sob;
			sob.m_ID = pVI->get_instance_id();
			sob.m_aabb = bb;

			//lroom.m_SOBs.push_back(sob);
			LRoom_PushBackSOB(lroom, sob);

			// take away layer 0 from the sob, so it can be culled effectively
			pVI->set_layer_mask(0);
		}
		else
		{
			// not visual instances
		}

		// does it have further children?
		Convert_Room_FindObjects_Recursive(pChild, lroom, bb_room);
	}

}

bool LRoomConverter::Convert_Room(Spatial * pNode, int lroomID)
{
	// get the room part of the name
	String szFullName = pNode->get_name();
	String szRoom = LPortal::FindNameAfter(pNode, "room_");

	LPRINT(4, "Convert_Room : " + szFullName);

	// get a reference to the lroom we are writing to
	LRoom &lroom = LMAN->m_Rooms[lroomID];

	// store the godot room
	lroom.m_GodotID = pNode->get_instance_id();
	lroom.m_RoomID = lroomID;

	// save the room ID on the godot room metadata
	// This is used when registering DOBs and teleporting them with hints
	// i.e. the Godot room is used to lookup the room ID of the startroom.
	LMAN->Obj_SetRoomNum(pNode, lroomID);

	// create a new LRoom to exchange the children over to, and delete the original empty
	lroom.m_szName = szRoom;

	// keep a running bounding volume as we go through the visual instances
	// to determine the overall bound of the room
	LAABB bb_room;
	bb_room.SetToMaxOpposite();

	// set default cull masks
	Convert_Room_SetDefaultCullMask_Recursive(pNode);

	// recursively find statics
	Convert_Room_FindObjects_Recursive(pNode, lroom, bb_room);

	// store the lroom centre and bound
	lroom.m_ptCentre = bb_room.FindCentre();

	// bound (untested)
	lroom.m_AABB.position = bb_room.m_ptMins;
	lroom.m_AABB.size = bb_room.m_ptMaxs - bb_room.m_ptMins;

	LPRINT(2, "\t\t\t" + String(lroom.m_szName) + " centre " + lroom.m_ptCentre);

	return true;
}

bool LRoomConverter::Bound_AddPlaneIfUnique(LVector<Plane> &planes, const Plane &p)
{
	for (int n=0; n<planes.size(); n++)
	{
		const Plane &o = planes[n];

		// this is a fudge factor for how close planes can be to be considered the same ...
		// to prevent ridiculous amounts of planes
		const float d = 0.08f;

		if (fabs(p.d - o.d) > d) continue;

		float dot = p.normal.dot(o.normal);
		if (dot < 0.98f) continue;

		// match!
		return false;
	}

	// test
//	Vector3 va(1, 0, 0);
//	Vector3 vb(1, 0.2, 0);
//	vb.normalize();
//	float dot = va.dot(vb);
//	print("va dot vb is " + String(Variant(dot)));

	// is unique
//	print("\t\t\t\tAdding bound plane : " + p);

	planes.push_back(p);
	return true;
}

bool LRoomConverter::Convert_Bound(LRoom &lroom, MeshInstance * pMI)
{
	LPRINT(2, "\tCONVERT_BOUND : '" + pMI->get_name() + "' for room '" + lroom.get_name() + "'");

	// some godot jiggery pokery to get the mesh verts in local space
	Ref<Mesh> rmesh = pMI->get_mesh();
	Array arrays = rmesh->surface_get_arrays(0);
	PoolVector<Vector3> p_vertices = arrays[VS::ARRAY_VERTEX];

	// convert to world space
	Transform trans = pMI->get_global_transform();
	Vector<Vector3> points;
	for (int n=0; n<p_vertices.size(); n++)
	{
		Vector3 ptWorld = trans.xform(p_vertices[n]);
		points.push_back(ptWorld);

		// expand the room AABB to make sure it encompasses the bound
		lroom.m_AABB.expand_to(ptWorld);
	}

	if (points.size() > 3)
	{
		Geometry::MeshData md;
		Error err = QuickHull::build(points, md);
		if (err == OK)
		{
			// get the planes
			for (int n=0; n<md.faces.size(); n++)
			{
				const Plane &p = md.faces[n].plane;
				Bound_AddPlaneIfUnique(lroom.m_Bound.m_Planes, p);
			}

			LPRINT(2, "\t\t\tcontained " + itos(lroom.m_Bound.m_Planes.size()) + " planes.");

			// make a copy of the mesh data for debugging
			// note this could be avoided in release builds? NYI
			lroom.m_Bound_MeshData = md;

//			for (int f=0; f<md.faces.size(); f++)
//			{
//				String sz;
//				sz = "face " + itos (f) + ", indices ";
//				for (int i=0; i<md.faces[f].indices.size(); i++)
//				{
//					sz += itos(md.faces[f].indices[i]) + ", ";
//				}
//				LPRINT(2, sz);
//			}

			return true;
		}
	}

	return false;
}

// hide all in preparation for first frame
void LRoomConverter::Convert_HideAll()
{
	for (int n=0; n<LMAN->m_SOBs.size(); n++)
	{
		LSob &sob = LMAN->m_SOBs[n];
		sob.Show(false);
	}
}

void LRoomConverter::Convert_Lights()
{
	// trace local lights out from rooms and add to each room the light affects
	for (int n=0; n<LMAN->m_Lights.size(); n++)
	{
		LLight &l = LMAN->m_Lights[n];
		if (l.IsGlobal())
			continue; // ignore globals .. affect all rooms

		Light_Trace(n);
	}
}

void LRoomConverter::Light_Trace(int iLightID)
{
	// blank this each time as it is used to create the list of casters
	LMAN->m_BF_caster_SOBs.Blank();

	// get the light
	LLight &l = LMAN->m_Lights[iLightID];

	LPRINT(5,"\nLight_Trace " + itos (iLightID) + " direction " + l.m_ptDir);

	// reset the planes pool for each render out from the source room
	LMAN->m_Pool.Reset();


	// the first set of planes are blank
	unsigned int pool_member = LMAN->m_Pool.Request();
	assert (pool_member != -1);

	LVector<Plane> &planes = LMAN->m_Pool.Get(pool_member);
	planes.clear();

	Lawn::LDebug::m_iTabDepth = 0;

	Light_TraceRecursive(0, LMAN->m_Rooms[l.m_RoomID], l, iLightID, planes);
}


void LRoomConverter::Light_TraceRecursive(int depth, LRoom &lroom, LLight &light,  int iLightID, const LVector<Plane> &planes)
{
	// prevent too much depth
	if (depth > 8)
	{
		LPRINT_RUN(2, "\t\t\tLight_TraceRecursive DEPTH LIMIT REACHED");
		return;
	}

	Lawn::LDebug::m_iTabDepth = depth;
	LPRINT_RUN(2, "ROOM " + lroom.get_name() + " affected by local light");


	// add to the local lights affecting this room
	// already in list?
	bool bAlreadyInList = false;
	for (int n=0; n<lroom.m_LocalLights.size(); n++)
	{
		if (lroom.m_LocalLights[n] == iLightID)
		{
			bAlreadyInList = true;
			break;
		}
	}
	// add to local lights if not already in list
	if (!bAlreadyInList)
	{
		lroom.m_LocalLights.push_back(iLightID);
	}

	// add each light caster that is within the planes to the light caster list
	// clip all objects in this room to the clipping planes
	int last_sob = lroom.m_iFirstSOB + lroom.m_iNumSOBs;
	for (int n=lroom.m_iFirstSOB; n<last_sob; n++)
	{
		LSob &sob = LMAN->m_SOBs[n];

		//LPRINT_RUN(2, "sob " + itos(n) + " " + sob.GetSpatial()->get_name());
		// already determined to be visible through another portal
//		if (LMAN->m_BF_caster_SOBs.GetBit(n))
//		{
//			//LPRINT_RUN(2, "\talready visible");
//			continue;
//		}

		bool bShow = true;


		// estimate the radius .. for now
		const AABB &bb = sob.m_aabb;

//		print("\t\t\tculling object " + pObj->get_name());

		for (int p=0; p<planes.size(); p++)
		{
//				float dist = planes[p].distance_to(pt);
//				print("\t\t\t\t" + itos(p) + " : dist " + String(Variant(dist)));

			float r_min, r_max;
			bb.project_range_in_plane(planes[p], r_min, r_max);

	//		print("\t\t\t\t" + itos(p) + " : r_min " + String(Variant(r_min)) + ", r_max " + String(Variant(r_max)));


			if (r_min > 0.0f)
			{
				bShow = false;
				break;
			}
		}

		if (bShow)
		{
			Light_AddCaster_SOB(light, n);
		}

	} // for through sobs



	// look through every portal out
	for (int n=0; n<lroom.m_iNumPortals; n++)
	{
		int portalID = lroom.m_iFirstPortal + n;

		const LPortal &port = LMAN->m_Portals[portalID];

		LPRINT_RUN(2, "\tPORTAL " + itos (n) + " (" + itos(portalID) + ") " + port.get_name() + " normal " + port.m_Plane.normal);

		float dot = port.m_Plane.normal.dot(light.m_ptDir);

		if (dot <= 0.0f)
		{
			LPRINT_RUN(2, "\t\tCULLED (wrong direction)");
			continue;
		}

		// is it culled by the planes?
		LPortal::eClipResult overall_res = LPortal::eClipResult::CLIP_INSIDE;

		// cull portal with planes
		for (int l=0; l<planes.size(); l++)
		{
			LPortal::eClipResult res = port.ClipWithPlane(planes[l]);

			switch (res)
			{
			case LPortal::eClipResult::CLIP_OUTSIDE:
				overall_res = res;
				break;
			case LPortal::eClipResult::CLIP_PARTIAL:
				overall_res = res;
				break;
			default: // suppress warning
				break;
			}

			if (overall_res == LPortal::eClipResult::CLIP_OUTSIDE)
				break;
		}

		// this portal is culled
		if (overall_res == LPortal::eClipResult::CLIP_OUTSIDE)
		{
			LPRINT_RUN(2, "\t\tCULLED (outside planes)");
			continue;
		}


		LRoom &linked_room = LMAN->Portal_GetLinkedRoom(port);


		// recurse into that portal
		unsigned int uiPoolMem = LMAN->m_Pool.Request();
		if (uiPoolMem != -1)
		{
			// get a vector of planes from the pool
			LVector<Plane> &new_planes = LMAN->m_Pool.Get(uiPoolMem);

			// copy the existing planes
			new_planes.copy_from(planes);

			// add the planes for the portal
			port.AddLightPlanes(*LMAN, light, new_planes, false);

			Light_TraceRecursive(depth + 1, linked_room, light, iLightID, new_planes);
			// for debugging need to reset tab depth
			Lawn::LDebug::m_iTabDepth = depth;

			// we no longer need these planes
			LMAN->m_Pool.Free(uiPoolMem);
		}
		else
		{
			// planes pool is empty!
			// This will happen if the view goes through shedloads of portals
			// The solution is either to increase the plane pool size, or build levels
			// with views through multiple portals. Looking through multiple portals is likely to be
			// slow anyway because of the number of planes to test.
			WARN_PRINT_ONCE("LRoom_FindShadowCasters_Recursive : Planes pool is empty");
		}


	}

}


void LRoomConverter::Convert_ShadowCasters()
{
	int nLights = LMAN->m_Lights.size();
	LPRINT(5,"\nConvert_ShadowCasters ... numlights " + itos (nLights));


	for (int l=0; l<nLights; l++)
	{
		const LLight &light = LMAN->m_Lights[l];
		String sz = "Light " + itos (l);
		if (light.IsGlobal())
			sz += " GLOBAL";
		else
			sz += " LOCAL from room " + itos(light.m_RoomID);

		LPRINT(5, sz + " direction " + light.m_ptDir);

		for (int n=0; n<LMAN->m_Rooms.size(); n++)
		{
			LRoom &lroom = LMAN->m_Rooms[n];

			// global lights affect every room
			bool bAffectsRoom = true;

			// if the light is local, does it affect this room?
			if (!light.IsGlobal())
			{
				// a local light .. does it affect this room?
				bAffectsRoom = false;
				for (int i=0; i<lroom.m_LocalLights.size(); i++)
				{
					// if the light id is found among the local lights for this room
					if (lroom.m_LocalLights[i] == l)
					{
						bAffectsRoom = true;
						break;
					}
				}
			}

			if (bAffectsRoom)
			{
				LPRINT(2,"\n\tAFFECTS room " + itos(n) + ", " + lroom.get_name());
				LRoom_FindShadowCasters_FromLight(lroom, light);
				//LRoom_FindShadowCasters(lroom, l, light);
			}
		}
	}
}


void LRoomConverter::Convert_Bounds()
{
	for (int n=0; n<LMAN->m_Rooms.size(); n++)
	{
		LRoom &lroom = LMAN->m_Rooms[n];

		//print("DetectBounds from room " + lroom.get_name());

		Spatial * pGRoom = lroom.GetGodotRoom();
		assert (pGRoom);


		for (int n=0; n<pGRoom->get_child_count(); n++)
		{
			Node * pChild = pGRoom->get_child(n);

			if (Node_IsBound(pChild))
			{
				MeshInstance * pMesh = Object::cast_to<MeshInstance>(pChild);
				assert (pMesh);
				Convert_Bound(lroom, pMesh);

				// delete the mesh
				pGRoom->remove_child(pChild);
				pChild->queue_delete();
			}
		}

	}

}

void LRoomConverter::Convert_Portals()
{
	for (int pass=0; pass<3; pass++)
	{
		LPRINT(2, "Convert_Portals pass " + itos(pass));
		LPRINT(2, "");

		for (int n=0; n<LMAN->m_Rooms.size(); n++)
		{
			LRoom &lroom = LMAN->m_Rooms[n];
			LTempRoom &troom = m_TempRooms[n];

			switch (pass)
			{
			case 0:
				LRoom_DetectPortalMeshes(lroom, troom);
				break;
			case 1:
				LRoom_MakePortalsTwoWay(lroom, troom, n);
				break;
			case 2:
				LRoom_MakePortalFinalList(lroom, troom);
				break;
			}

		}

	}
}


int LRoomConverter::CountRooms()
{
	int nChildren = LMAN->get_child_count();
	int count = 0;

	for (int n=0; n<nChildren; n++)
	{
		if (Node_IsRoom(LMAN->get_child(n)))
			count++;
	}

	return count;
}


// find all objects that cast shadows onto the objects in this room
//void LRoomConverter::LRoom_FindShadowCasters(LRoom &lroom, int lightID, const LLight &light)
//{
//	// each global light, and each light affecting this room
//	for (int n=0; n<LMAN->m_Lights.size(); n++)
//	{
//		// if the light is not a global light, we are only interested if it emits from this room
//		const LLight &l = LMAN->m_Lights[n];

//		bool bAffectsRoom = true;
//		if (l.m_RoomID != -1)
//		{
//			// a local light .. does it affect this room?
//			bAffectsRoom = false;
//			for (int i=0; i<lroom.m_LocalLights.size(); i++)
//			{
//				// if the light id is found among the local lights for this room
//				if (lroom.m_LocalLights[i] == n)
//				{
//					bAffectsRoom = true;
//					break;
//				}
//			}
//		}

//		if (bAffectsRoom)
//			LRoom_FindShadowCasters_FromLight(lroom, l);
//	}


//	return;
//}

void LRoomConverter::Light_AddCaster_SOB(LLight &light, int sobID)
{
	// we will reuse the rendering bitflags for shadow casters for this ... to check for double entries (fnaa fnaa)
	if (LMAN->m_BF_caster_SOBs.GetBit(sobID))
		return;


	LPRINT_RUN(2, "\t\t\tLightCaster " + itos(sobID));

	LMAN->m_BF_caster_SOBs.SetBit(sobID, true);


	// first?
	if (!light.m_NumCasters)
		light.m_FirstCaster = LMAN->m_LightCasters_SOB.size();

	LMAN->m_LightCasters_SOB.push_back(sobID);
	light.m_NumCasters++;
}


void LRoomConverter::LRoom_AddShadowCaster_SOB(LRoom &lroom, int sobID)
{
	// we will reuse the rendering bitflags for shadow casters for this ... to check for double entries (fnaa fnaa)
	if (LMAN->m_BF_caster_SOBs.GetBit(sobID))
		return;

	LMAN->m_BF_caster_SOBs.SetBit(sobID, true);

	// first?
	if (!lroom.m_iNumShadowCasters_SOB)
		lroom.m_iFirstShadowCaster_SOB = LMAN->m_ShadowCasters_SOB.size();

	LMAN->m_ShadowCasters_SOB.push_back(sobID);
	lroom.m_iNumShadowCasters_SOB++;
}


void LRoomConverter::LRoom_FindShadowCasters_FromLight(LRoom &lroom, const LLight &light)
{
	// blank this each time as it is used to create the list of casters
	LMAN->m_BF_caster_SOBs.Blank();

	// first add all objects in this room as casters
//	int last_sob = lroom.m_iFirstSOB + lroom.m_iNumSOBs;
//	for (int n=lroom.m_iFirstSOB; n<last_sob; n++)
//	{
//		//LSob &sob = manager.m_SOBs[n];
//		LRoom_AddShadowCaster_SOB(lroom, n);
//	}


	// just a constant light direction for now
//	LLight light;
//	light.m_ptDir = Vector3(1.0f, -1.0f, 0.0f);
//	light.m_ptDir.normalize();

	// reset the planes pool for each render out from the source room
	LMAN->m_Pool.Reset();


	// the first set of planes are blank
	unsigned int pool_member = LMAN->m_Pool.Request();
	assert (pool_member != -1);

	LVector<Plane> &planes = LMAN->m_Pool.Get(pool_member);
	planes.clear();

	Lawn::LDebug::m_iTabDepth = 0;
	LRoom_FindShadowCasters_Recursive(lroom, 1, lroom, light, planes);

}


void LRoomConverter::LRoom_FindShadowCasters_Recursive(LRoom &source_lroom, int depth, LRoom &lroom, const LLight &light, const LVector<Plane> &planes)
{
	// prevent too much depth
	if (depth > 8)
	{
		LPRINT_RUN(2, "\t\t\tLRoom_FindShadowCasters_Recursive DEPTH LIMIT REACHED");
//		WARN_PRINT_ONCE("LPortal Depth Limit reached (seeing through > 8 portals)");
		return;
	}

	Lawn::LDebug::m_iTabDepth = depth;
	LPRINT_RUN(2, "ROOM " + lroom.get_name());


	// every object in this room is added that is within the planes
	int last_sob = lroom.m_iFirstSOB + lroom.m_iNumSOBs;
	for (int n=lroom.m_iFirstSOB; n<last_sob; n++)
	{
		LSob &sob = LMAN->m_SOBs[n];

		// not a shadow caster? don't add to the list
		if (!sob.IsShadowCaster())
			continue;

		bool bShow = true;
		const AABB &bb = sob.m_aabb;

//		print("\t\t\tculling object " + pObj->get_name());

		for (int p=0; p<planes.size(); p++)
		{
//				float dist = planes[p].distance_to(pt);
//				print("\t\t\t\t" + itos(p) + " : dist " + String(Variant(dist)));

			float r_min, r_max;
			bb.project_range_in_plane(planes[p], r_min, r_max);

	//		print("\t\t\t\t" + itos(p) + " : r_min " + String(Variant(r_min)) + ", r_max " + String(Variant(r_max)));


			if (r_min > 0.0f)
//			if (r_max < 0.0f)
			{
				//LPRINT_RUN(2, "\tR_MIN is " + String(Variant(r_min)) + " R_MAX is " + String(Variant(r_max))+ ", for plane " + itos(p));

				bShow = false;
				break;
			}
		}

		if (bShow)
		{
			LPRINT_RUN(2, "\tcaster " + itos(n) + ", " + sob.GetSpatial()->get_name());
			LRoom_AddShadowCaster_SOB(source_lroom, n);
		}
		else
		{
			//LPRINT_RUN(2, "\tculled " + itos(n) + ", " + sob.GetSpatial()->get_name());
		}
	}

	// look through every portal out
	for (int n=0; n<lroom.m_iNumPortals; n++)
	{
		int portalID = lroom.m_iFirstPortal + n;

		const LPortal &port = LMAN->m_Portals[portalID];

		LPRINT_RUN(2, "\tPORTAL " + itos (n) + " (" + itos(portalID) + ") " + port.get_name() + " normal " + port.m_Plane.normal);

		// cull with light direction
		float dot;
		if (light.m_eType == LLight::LT_DIRECTIONAL)
		{
			dot = port.m_Plane.normal.dot(light.m_ptDir);
		}
		else
		{
			// cull with light direction to portal
			Vector3 ptLightToPort = port.m_ptCentre - light.m_ptPos;
			dot = port.m_Plane.normal.dot(ptLightToPort);
		}


//		float dot = port.m_Plane.normal.dot(light.m_ptDir);
		if (dot >= 0.0f)
		{
			LPRINT_RUN(2, "\t\tCULLED (wrong direction)");
			continue;
		}

		// is it culled by the planes?
		LPortal::eClipResult overall_res = LPortal::eClipResult::CLIP_INSIDE;

		// cull portal with planes
		for (int l=0; l<planes.size(); l++)
		{
			LPortal::eClipResult res = port.ClipWithPlane(planes[l]);

			switch (res)
			{
			case LPortal::eClipResult::CLIP_OUTSIDE:
				overall_res = res;
				break;
			case LPortal::eClipResult::CLIP_PARTIAL:
				overall_res = res;
				break;
			default: // suppress warning
				break;
			}

			if (overall_res == LPortal::eClipResult::CLIP_OUTSIDE)
				break;
		}

		// this portal is culled
		if (overall_res == LPortal::eClipResult::CLIP_OUTSIDE)
		{
			LPRINT_RUN(2, "\t\tCULLED (outside planes)");
			continue;
		}


		LRoom &linked_room = LMAN->Portal_GetLinkedRoom(port);


		// recurse into that portal
		unsigned int uiPoolMem = LMAN->m_Pool.Request();
		if (uiPoolMem != -1)
		{
			// get a vector of planes from the pool
			LVector<Plane> &new_planes = LMAN->m_Pool.Get(uiPoolMem);

			// copy the existing planes
			new_planes.copy_from(planes);

			// add the planes for the portal
			port.AddLightPlanes(*LMAN, light, new_planes, true);

			LRoom_FindShadowCasters_Recursive(source_lroom, depth + 1, linked_room, light, new_planes);
			// for debugging need to reset tab depth
			Lawn::LDebug::m_iTabDepth = depth;

			// we no longer need these planes
			LMAN->m_Pool.Free(uiPoolMem);
		}
		else
		{
			// planes pool is empty!
			// This will happen if the view goes through shedloads of portals
			// The solution is either to increase the plane pool size, or build levels
			// with views through multiple portals. Looking through multiple portals is likely to be
			// slow anyway because of the number of planes to test.
			WARN_PRINT_ONCE("LRoom_FindShadowCasters_Recursive : Planes pool is empty");
		}


	}

}


// go through the nodes hanging off the room looking for those that are meshes to mark portal locations
void LRoomConverter::LRoom_DetectPortalMeshes(LRoom &lroom, LTempRoom &troom)
{
	LPRINT(2, "DETECT_PORTALS from room " + lroom.get_name());

	Spatial * pGRoom = lroom.GetGodotRoom();
	assert (pGRoom);


	for (int n=0; n<pGRoom->get_child_count(); n++)
	{
		Node * pChild = pGRoom->get_child(n);

		if (Node_IsPortal(pChild))
		{

			MeshInstance * pMesh = Object::cast_to<MeshInstance>(pChild);
			assert (pMesh);

			// name must start with 'portal_'
			// and ends with the name of the room we want to link to (without the 'room_')
			String szLinkRoom = LPortal::FindNameAfter(pMesh, "portal_");
			LRoom_DetectedPortalMesh(lroom, troom, pMesh, szLinkRoom);
		}
	}

	// we need an enclosing while loop because we might be deleting children and mucking up the iterator
	bool bDetectedOne = true;

	while (bDetectedOne)
	{
		bDetectedOne = false;

		for (int n=0; n<pGRoom->get_child_count(); n++)
		{
			Node * pChild = pGRoom->get_child(n);

			if (Node_IsPortal(pChild))
			{
				// delete the original child, as it is no longer needed at runtime (except maybe for debugging .. NYI?)
				//	pMeshInstance->hide();
				pChild->get_parent()->remove_child(pChild);
				pChild->queue_delete();

				bDetectedOne = true;
			}

			if (bDetectedOne)
				break;
		} // for loop

	} // while

}

void LRoomConverter::LRoom_PushBackSOB(LRoom &lroom, const LSob &sob)
{
	// first added for this room?
	if (lroom.m_iNumSOBs == 0)
		lroom.m_iFirstSOB = LMAN->m_SOBs.size();

	LMAN->m_SOBs.push_back(sob);
	lroom.m_iNumSOBs++;
}


// handles the slight faff involved in getting a new portal in the manager contiguous list of portals
LPortal * LRoomConverter::LRoom_RequestNewPortal(LRoom &lroom)
{
	// is this the first portal?
	if (lroom.m_iNumPortals == 0)
		lroom.m_iFirstPortal = LMAN->m_Portals.size();

	lroom.m_iNumPortals++;

	return LMAN->m_Portals.request();
}

// convert the list on each room to a single contiguous list in the manager
void LRoomConverter::LRoom_MakePortalFinalList(LRoom &lroom, LTempRoom &troom)
{
	for (int n=0; n<troom.m_Portals.size(); n++)
	{
		LPortal &lport_final = *LRoom_RequestNewPortal(lroom);
		lport_final = troom.m_Portals[n];
	}
}


void LRoomConverter::LRoom_DetectedLight(LRoom &lroom, Node * pNode)
{
	Light * pLight = Object::cast_to<Light>(pNode);
	assert (pLight);

	LMAN->LightCreate(pLight, lroom.m_RoomID);
	/*
	// create new light
	LLight l;
	l.SetDefaults();
	l.m_GodotID = pLight->get_instance_id();
	// direction
	Transform tr = pLight->get_global_transform();
	l.m_ptPos = tr.origin;
	l.m_ptDir = -tr.basis.get_axis(2); // or possibly get_axis .. z is what we want
	l.m_fMaxDist = pLight->get_param(Light::PARAM_SHADOW_MAX_DISTANCE);

	// source room ID
	l.m_RoomID = lroom.m_RoomID;

	bool bOK = false;

	// what kind of light?
	SpotLight * pSL = Object::cast_to<SpotLight>(pNode);
	if (pSL)
	{
		LPRINT(2, "\tSPOTLIGHT detected " + pNode->get_name());
		l.m_eType = LLight::LT_SPOTLIGHT;
		l.m_fSpread = pSL->get_param(Light::PARAM_SPOT_ANGLE);

		bOK = true;
	}

	OmniLight * pOL = Object::cast_to<OmniLight>(pNode);
	if (pOL)
	{
		LPRINT(2, "\tOMNILIGHT detected " + pNode->get_name());
		l.m_eType = LLight::LT_OMNI;
		bOK = true;
	}

	DirectionalLight * pDL = Object::cast_to<DirectionalLight>(pNode);
	if (pDL)
	{
		LPRINT(2, "\tDIRECTIONALLIGHT detected " + pNode->get_name());
		l.m_eType = LLight::LT_DIRECTIONAL;
		bOK = true;
	}

	// don't add if not recognised
	if (!bOK)
	{
		LPRINT(2, "\tLIGHT type unrecognised " + pNode->get_name());
		return;
	}


	// turn the local light off to start with
	pLight->hide();

	LMAN->m_Lights.push_back(l);
	*/
}

// found a portal mesh! create a matching LPortal
void LRoomConverter::LRoom_DetectedPortalMesh(LRoom &lroom, LTempRoom &troom, MeshInstance * pMeshInstance, String szLinkRoom)
{
	LPRINT(2, "\tdetected to " + szLinkRoom);

	// which room does this portal want to link to?
	int iLinkRoom = FindRoom_ByName(szLinkRoom);
	if (iLinkRoom == -1)
	{
		LWARN(5, "portal to room " + szLinkRoom + ", room not found");
		//WARN_PRINTS("portal to room " + szLinkRoom + ", room not found");
		return;
	}

	// some godot jiggery pokery to get the mesh verts in local space
	Ref<Mesh> rmesh = pMeshInstance->get_mesh();
	Array arrays = rmesh->surface_get_arrays(0);
	PoolVector<Vector3> p_vertices = arrays[VS::ARRAY_VERTEX];

	// create a new LPortal to fill with this wonderful info
	LPortal &lport = *troom.m_Portals.request();
	lport.m_szName = szLinkRoom;
	lport.m_iRoomNum = iLinkRoom;

	// create the portal geometry
	lport.CreateGeometry(p_vertices, pMeshInstance->get_global_transform());


//	LPRINT(2, "\t\t\tnum portals now " + itos(troom.m_Portals.size()));
}


// This aims to make life easier for level designers. They only need to make a portal facing one way and LPortal
// will automatically create a mirror portal the other way.
void LRoomConverter::LRoom_MakePortalsTwoWay(LRoom &lroom, LTempRoom &troom, int iRoomNum)
{
	LPRINT(2, "MAKE_PORTALS_TWOWAY from room " + lroom.get_name());
	LPRINT(2, "\tcontains " + itos (troom.m_Portals.size()) + " portals");
	for (int n=0; n<troom.m_Portals.size(); n++)
	{
		const LPortal &portal_orig = troom.m_Portals[n];
		LPRINT(2, "\tconsidering portal " + portal_orig.get_name());

		// only make original portals into mirror portals, to prevent infinite recursion
		if (portal_orig.m_bMirror)
		{
			LPRINT (2, "\t\tis MIRROR, ignoring");
			continue;
		}

		LPRINT(2, "\t\tcreating opposite portal");

		// get the temproom this portal is linking to
		//LTempRoom &nroom = m_TempRooms[portal_orig.m_iRoomNum];

		// does a portal already exist back to the orig room?
		// NOTE this doesn't cope with multiple portals between pairs of rooms yet.
//		bool bAlreadyLinked =false;

//		for (int p=0; p<nroom.m_Portals.size(); p++)
//		{
//			if (nroom.m_Portals[p].m_iRoomNum == n)
//			{
//				bAlreadyLinked = true;
//				break;
//			}
//		}

//		if (bAlreadyLinked)
//			continue;

		// needs a new reverse link if got to here
		TRoom_MakeOppositePortal(portal_orig, iRoomNum);
	}
}

// There is a need for a mirror portal, let's make one!
void LRoomConverter::TRoom_MakeOppositePortal(const LPortal &port, int iRoomOrig)
{
	LTempRoom &nroom = m_TempRooms[port.m_iRoomNum];
	const LRoom &orig_lroom = LMAN->m_Rooms[iRoomOrig];

	// the new portal should have the name of the room the original came from
	LPortal &new_port = *nroom.m_Portals.request();
	new_port.m_szName = orig_lroom.m_szName;
	new_port.m_iRoomNum = iRoomOrig;
	new_port.m_bMirror = true;

	// the portal vertices should be the same but reversed (to flip the normal)
	new_port.CopyReversedGeometry(port);
}


///////////////////////////////////////////////////

// helper
bool LRoomConverter::Node_IsLight(Node * pNode) const
{
	Light * pLight = Object::cast_to<Light>(pNode);
	if (!pLight)
		return false;

	return true;
}


bool LRoomConverter::Node_IsRoom(Node * pNode) const
{
	Spatial * pSpat = Object::cast_to<Spatial>(pNode);
	if (!pSpat)
		return false;

	if (LPortal::NameStartsWith(pSpat, "room_"))
		return true;

	return false;
}

bool LRoomConverter::Node_IsIgnore(Node * pNode) const
{
	if (LPortal::NameStartsWith(pNode, "ignore_"))
		return true;

	return false;
}

bool LRoomConverter::Node_IsBound(Node * pNode) const
{
	MeshInstance * pMI = Object::cast_to<MeshInstance>(pNode);
	if (!pMI)
		return false;

	if (LPortal::NameStartsWith(pMI, "bound_"))
		return true;

	return false;
}

bool LRoomConverter::Node_IsPortal(Node * pNode) const
{
	MeshInstance * pMI = Object::cast_to<MeshInstance>(pNode);
	if (!pMI)
		return false;

	if (LPortal::NameStartsWith(pMI, "portal_"))
		return true;

	return false;
}



// keep the global namespace clean
#undef LMAN
