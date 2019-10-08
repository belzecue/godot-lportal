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

/**
	@author lawnjelly <lawnjelly@gmail.com>
*/

#include "ldob.h"
#include "scene/3d/mesh_instance.h"


Spatial * LSob::GetSpatial() const
{
	Object * pObj = ObjectDB::get_instance(m_ID);
	Spatial * pSpat = Object::cast_to<Spatial>(pObj);
	return pSpat;
}


bool LSob::IsShadowCaster() const
{
	Object * pObj = ObjectDB::get_instance(m_ID);
	GeometryInstance * pGI = Object::cast_to<GeometryInstance>(pObj);

	if (pGI)
	{
		if (pGI->get_cast_shadows_setting() == GeometryInstance::SHADOW_CASTING_SETTING_OFF)
			return false;

		return true;
	}

	// not sure yet, maybe this should be true, depends what the non geometry objects are
	return false;
}


VisualInstance * LSob::GetVI() const
{
	Object * pObj = ObjectDB::get_instance(m_ID);
	VisualInstance * pVI = Object::cast_to<VisualInstance>(pObj);
	return pVI;
}


void LSob::Show(bool bShow)
{
	Spatial * pS = GetSpatial();
	if (!pS)
		return;

	// noop
	if (pS->is_visible() == bShow)
		return;

	if (bShow)
		pS->show();
	else
		pS->hide();

	GeometryInstance * pGI = Object::cast_to<GeometryInstance>(pS);
	if (pGI)
	{
		// godot visible bug workaround
		pGI->set_extra_cull_margin(0.0f);
	}

}



Spatial * LDob::GetSpatial() const
{
	Object * pObj = ObjectDB::get_instance(m_ID_Spatial);
	Spatial * pSpat = Object::cast_to<Spatial>(pObj);
	return pSpat;
}

VisualInstance * LDob::GetVI() const
{
	Object * pObj = ObjectDB::get_instance(m_ID_VI);
	VisualInstance * pVI = Object::cast_to<VisualInstance>(pObj);
	return pVI;
}

