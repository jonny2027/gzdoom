/*
#include "actor.h"
#include "m_random.h"
#include "a_action.h"
#include "p_local.h"
#include "s_sound.h"
#include "m_random.h"
#include "a_strifeglobal.h"
#include "thingdef/thingdef.h"
*/

class ASpectralMonster : public AActor
{
	DECLARE_CLASS (ASpectralMonster, AActor)
public:
	void Touch (AActor *toucher);
};

IMPLEMENT_CLASS (ASpectralMonster)

void ASpectralMonster::Touch (AActor *toucher)
{
	P_DamageMobj (toucher, this, this, 5, NAME_Melee);
}


DEFINE_ACTION_FUNCTION(AActor, A_SpectralLightningTail)
{
	PARAM_ACTION_PROLOGUE;

	AActor *foo = Spawn("SpectralLightningHTail", self->x - self->velx, self->y - self->vely, self->z, ALLOW_REPLACE);

	foo->angle = self->angle;
	foo->FriendPlayer = self->FriendPlayer;
	return 0;
}

DEFINE_ACTION_FUNCTION(AActor, A_SpectralBigBallLightning)
{
	PARAM_ACTION_PROLOGUE;

	PClassActor *cls = PClass::FindActor("SpectralLightningH3");
	if (cls)
	{
		self->angle += ANGLE_90;
		P_SpawnSubMissile (self, cls, self->target);
		self->angle += ANGLE_180;
		P_SpawnSubMissile (self, cls, self->target);
		self->angle += ANGLE_90;
		P_SpawnSubMissile (self, cls, self->target);
	}
	return 0;
}

static FRandom pr_zap5 ("Zap5");

DEFINE_ACTION_FUNCTION(AActor, A_SpectralLightning)
{
	PARAM_ACTION_PROLOGUE;

	AActor *flash;
	fixed_t x, y;

	if (self->threshold != 0)
		--self->threshold;

	self->velx += pr_zap5.Random2(3) << FRACBITS;
	self->vely += pr_zap5.Random2(3) << FRACBITS;

	x = self->x + pr_zap5.Random2(3) * FRACUNIT * 50;
	y = self->y + pr_zap5.Random2(3) * FRACUNIT * 50;

	flash = Spawn (self->threshold > 25 ? PClass::FindActor(NAME_SpectralLightningV2) :
		PClass::FindActor(NAME_SpectralLightningV1), x, y, ONCEILINGZ, ALLOW_REPLACE);

	flash->target = self->target;
	flash->velz = -18*FRACUNIT;
	flash->FriendPlayer = self->FriendPlayer;

	flash = Spawn(NAME_SpectralLightningV2, self->x, self->y, ONCEILINGZ, ALLOW_REPLACE);

	flash->target = self->target;
	flash->velz = -18*FRACUNIT;
	flash->FriendPlayer = self->FriendPlayer;
	return 0;
}

// In Strife, this number is stored in the data segment, but it doesn't seem to be
// altered anywhere.
#define TRACEANGLE (0xe000000)

DEFINE_ACTION_FUNCTION(AActor, A_Tracer2)
{
	PARAM_ACTION_PROLOGUE;

	AActor *dest;
	angle_t exact;
	fixed_t dist;
	fixed_t slope;

	dest = self->tracer;

	if (!dest || dest->health <= 0 || self->Speed == 0 || !self->CanSeek(dest))
		return 0;

	// change angle
	exact = R_PointToAngle2 (self->x, self->y, dest->x, dest->y);

	if (exact != self->angle)
	{
		if (exact - self->angle > 0x80000000)
		{
			self->angle -= TRACEANGLE;
			if (exact - self->angle < 0x80000000)
				self->angle = exact;
		}
		else
		{
			self->angle += TRACEANGLE;
			if (exact - self->angle > 0x80000000)
				self->angle = exact;
		}
	}

	exact = self->angle >> ANGLETOFINESHIFT;
	self->velx = FixedMul (self->Speed, finecosine[exact]);
	self->vely = FixedMul (self->Speed, finesine[exact]);

	if (!(self->flags3 & (MF3_FLOORHUGGER|MF3_CEILINGHUGGER)))
	{
		// change slope
		dist = P_AproxDistance (dest->x - self->x, dest->y - self->y);
		dist /= self->Speed;

		if (dist < 1)
		{
			dist = 1;
		}
		if (dest->height >= 56*FRACUNIT)
		{
			slope = (dest->z+40*FRACUNIT - self->z) / dist;
		}
		else
		{
			slope = (dest->z + self->height*2/3 - self->z) / dist;
		}
		if (slope < self->velz)
		{
			self->velz -= FRACUNIT/8;
		}
		else
		{
			self->velz += FRACUNIT/8;
		}
	}
	return 0;
}
