/*
	Arrow
	Author: Newton
	
	The arrow is the standard ammo for the bow and base object for all other
	types of arrows. This object is stackable (up to 20) as it is required
	from the bow.
	The arrow employs the HitCheck effect (in System.c4g/HitChecks.c) originally
	from Hazard to search for targets during flight.
*/

#include L_ST

public func IsArrow() { return true; }
public func MaxStackCount() { return 15; }
public func ArrowStrength() { return 10; }

protected func Construction()
{
	SetR(90);
	return _inherited(...);
}

public func Launch(int angle, int str, object shooter)
{
	var xdir = Sin(angle,str);
	var ydir = Cos(angle,-str);
	SetXDir(xdir);
	SetYDir(ydir);
	SetR(angle);
	
	AddEffect("HitCheck", this, 1,1, nil,nil, shooter);
	AddEffect("InFlight", this, 1,1, this);
}

private func Stick()
{
	SetXDir(0);
	SetYDir(0);
	SetRDir(0);
	
	if(GetEffect("InFlight",this))
	{
		RemoveEffect("HitCheck",this);
		RemoveEffect("InFlight",this);
	
		var x=Sin(GetR(),+12);
		var y=Cos(GetR(),-12);
		var mat = GetMaterial(x,y);
		if(mat != -1)
		{
			if(GetMaterialVal("DigFree","Material",mat))
			{
			// stick in landscape
			SetVertex(2,VTX_Y,-15,1);
			}
		}
	}
}

public func HitObject(object obj)
{
	var speed = Sqrt(GetXDir()*GetXDir()+GetYDir()*GetYDir());
	Stick();

	if(obj->~QueryCatchBlow(this)) return;
  
	// arrow hit
	obj->~OnArrowHit(this);
	if(!this) return;
	// ouch!
	var dmg = ArrowStrength()*speed/50;
	if(obj->GetAlive())
	{
		obj->DoEnergy(-dmg);
	    obj->~CatchBlow(-dmg,this);
	}
	
	// target could have done something with this arrow
	if(!this) return;
	
	// tumble target
    if(obj->GetAlive())
    {
		obj->SetAction("Tumble");
		obj->SetSpeed(obj->GetXDir()+GetXDir()/3,obj->GetYDir()+GetYDir()/3-1);
    }
}

public func Hit()
{
	Stick();
}

// rotate arrow according to speed
public func FxInFlightStart(object target, int effect, int temp)
{
	if(temp) return;
	EffectVar(0,target,effect) = target->GetX();
	EffectVar(1,target,effect) = target->GetY();
}
public func FxInFlightTimer(object target, int effect, int time)
{
	var oldx = EffectVar(0,target,effect);
	var oldy = EffectVar(1,target,effect);
	var newx = GetX();
	var newy = GetY();
	
	// and additionally, we need one check: If the arrow has no speed
	// anymore but is still in flight, we'll remove the hit check
	if(oldx == newx && oldy == newy)
	{
		// but we give the arrow 5 frames to speed up again
		EffectVar(2,target,effect)++;
		if(EffectVar(2,target,effect) >= 10)
			return Hit();
	}
	else
		EffectVar(2,target,effect) = 0;

	// rotate arrow according to speed
	var anglediff = Normalize(Angle(oldx,oldy,newx,newy)-GetR(),-180);
	SetRDir(anglediff/2);
	EffectVar(0,target,effect) = newx;
	EffectVar(1,target,effect) = newy;

}

func UpdatePicture()
{
	var count = GetStackCount();
	if(count >= 9) SetGraphics(nil);
	else SetGraphics(Format("%d",count));
	_inherited(...);
}

func Entrance()
{
	// reset sticky-vertex
	SetVertex(2,VTX_Y,0,1);
}

func RejectEntrance()
{
	// not while flying
	if(GetEffect("InFlight",this)) return true;

	return _inherited(...);
}

func Definition(def) {
  SetProperty("Name", "$Name$", def);
}