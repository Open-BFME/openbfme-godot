// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of the interface of ZH Libraries/Source/WWVegas/WW3D2/hanim.h (HAnimClass), with the BFME
// fade accessor (BFME1 hanim.h _bfme_hanim_fade, default 1.0). Reference counting and the hash
// manager are not ported: animations are plain objects owned by the asset catalog.

#pragma once

#include "Libraries/WWVegas/WW3D2/motchan.h"
#include "Libraries/WWVegas/WWMath/quat.h"

#include <stdexcept>
#include <string>


class HAnimClass
{
public:
	virtual ~HAnimClass() = default;

	virtual const std::string &Get_Name() const = 0;  // "HIERARCHY.ANIM" (hcanim.cpp:260-262)
	virtual const std::string &Get_HName() const = 0; // hierarchy name from the header

	virtual int Get_Num_Frames() const = 0;
	virtual float Get_Frame_Rate() const = 0;
	virtual float Get_Total_Time() const { return (float)Get_Num_Frames() / Get_Frame_Rate(); }
	virtual int Get_Num_Pivots() const = 0;

	virtual void Get_Translation(Vector3 &translation, int pividx, float frame) const = 0;
	virtual bool Get_Orientation(Quaternion &orientation, int pividx, float frame) const = 0;
	virtual bool Get_Visibility(int pividx, float frame) const = 0;
	virtual float Get_Fade(int pividx, float frame) const = 0; // BFME: 1.0 when the pivot has no fade channel

	virtual bool Is_Node_Motion_Present(int pividx) const = 0;

	// False when a getter would throw UndefinedFrameError for this pivot and frame.
	virtual bool Frame_Is_Defined(int pividx, float frame) const = 0;
};
