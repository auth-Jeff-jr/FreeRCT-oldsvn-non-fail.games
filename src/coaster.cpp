/* $Id$ */

/*
 * This file is part of FreeRCT.
 * FreeRCT is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * FreeRCT is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with FreeRCT. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file coaster.cpp Coaster type data. */

#include <cmath>
#include "stdafx.h"
#include "sprite_store.h"
#include "coaster.h"
#include "fileio.h"
#include "memory.h"
#include "map.h"
#include "viewport.h"

#include "generated/coasters_strings.cpp"

CoasterPlatform _coaster_platforms[CPT_COUNT]; ///< Sprites for the coaster platforms.

static CarType _car_types[16]; ///< Car types available to the program (arbitrary limit).
static uint _used_types = 0;   ///< First free car type in #_car_types.

/**
 * Get a new car type.
 * @return A free car type, or \c nullptr if no free car type available.
 */
CarType *GetNewCarType()
{
	if (_used_types == lengthof(_car_types)) return nullptr;
	uint index = _used_types;
	_used_types++;
	return &_car_types[index];
}

CarType::CarType()
{
	std::fill_n(this->cars, lengthof(this->cars), nullptr);
}

/**
 * Load the data of a CARS block from file.
 * @param rcdfile Data file being loaded.
 * @param sprites Already loaded sprites.
 * @return Loading was a success.
 */
bool CarType::Load(RcdFileReader *rcdfile, const ImageMap &sprites)
{
	if (rcdfile->version != 2 || rcdfile->size != 2 + 2 + 4 + 4 + 2 + 2 + 16384) return false;

	this->tile_width = rcdfile->GetUInt16();
	if (this->tile_width != 64) return false; // Do not allow anything else than 64 pixels tile width.

	this->z_height = rcdfile->GetUInt16();
	if (this->z_height != this->tile_width / 4) return false;

	this->car_length = rcdfile->GetUInt32();
	if (this->car_length > 65535) return false; // Assumption is that a car fits in a single tile, at least some of the time.

	this->inter_car_length = rcdfile->GetUInt32();
	this->num_passengers = rcdfile->GetUInt16();
	this->num_entrances = rcdfile->GetUInt16();
	if (this->num_entrances == 0 || this->num_entrances > 4) return false; // Seems like a nice arbitrary upper limit on the number of rows of a car.
	uint16 pass_per_row = this->num_passengers / this->num_entrances;
	if (this->num_passengers != pass_per_row * this->num_entrances) return false;

	for (uint i = 0; i < 4096; i++) {
		if (!LoadSpriteFromFile(rcdfile, sprites, &this->cars[i])) return false;
	}
	return true;
}

CoasterType::CoasterType() : RideType(RTK_COASTER)
{
	this->voxels = {};
}

CoasterType::~CoasterType()
{
}

/**
 * Load a coaster type.
 * @param rcd_file Data file being loaded.
 * @param texts Already loaded text blocks.
 * @param piece_map Already loaded track pieces.
 * @return Loading was successful.
 */
bool CoasterType::Load(RcdFileReader *rcd_file, const TextMap &texts, const TrackPiecesMap &piece_map)
{
	uint32 length = rcd_file->size;
	if (rcd_file->version != 5 || length < 2 + 1 + 1 + 1 + 4 + 2) return false;
	length -= 2 + 1 + 1 + 1 + 4 + 2;
	this->coaster_kind = rcd_file->GetUInt16();
	this->platform_type = rcd_file->GetUInt8();
	this->max_number_trains = rcd_file->GetUInt8();
	this->max_number_cars = rcd_file->GetUInt8();
	if (this->coaster_kind == 0 || this->coaster_kind >= CST_COUNT) return false;
	if (this->platform_type == 0 || this->platform_type >= CPT_COUNT) return false;

	TextData *text_data;
	if (!LoadTextFromFile(rcd_file, texts, &text_data)) return false;
	StringID base = _language.RegisterStrings(*text_data, _coasters_strings_table);
	this->SetupStrings(text_data, base, STR_GENERIC_COASTER_START, COASTERS_STRING_TABLE_END, COASTERS_NAME_TYPE, COASTERS_DESCRIPTION_TYPE);

	int piece_count = rcd_file->GetUInt16();
	if (length != 4u * piece_count) return false;

	this->pieces.resize(piece_count);
	for (auto &piece : this->pieces) {
		uint32 val = rcd_file->GetUInt32();
		if (val == 0) return false; // We don't expect missing track pieces (they should not be included at all).
		auto iter = piece_map.find(val);
		if (iter == piece_map.end()) return false;
		piece = (*iter).second;
	}
	/* Setup a track voxel list for fast access in the type. */
	for (const auto &piece : this->pieces) {
		this->voxels.insert(this->voxels.end(), piece->track_voxels.begin(), piece->track_voxels.end());
	}
	return true;
}

/**
 * Select the default car type for this type of coaster.
 * @return The default car type, or \c nullptr is no car type is available.
 */
const CarType *CoasterType::GetDefaultCarType() const
{
	const CarType *car_type = &_car_types[0]; /// \todo Make a proper #CarType selection.
	return car_type;
}

bool CoasterType::CanMakeInstance() const
{
	return this->GetDefaultCarType() != nullptr;
}

RideInstance *CoasterType::CreateInstance() const
{
	const CarType *car_type = this->GetDefaultCarType();
	assert(car_type != nullptr); // Ensured by CanMakeInstance pre-check.

	return new CoasterInstance(this, car_type);
}

const ImageData *CoasterType::GetView(uint8 orientation) const
{
	return nullptr; // No preview available.
}

const StringID *CoasterType::GetInstanceNames() const
{
	static const StringID names[] = {COASTERS_NAME_INSTANCE, STR_INVALID};
	return names;
}

/**
 * Get the index of the provided track voxel for use as instance data.
 * @param tvx Track voxel to look for.
 * @return Index of the provided track voxel in this coaster type.
 * @see Voxel::SetInstanceData
 */
int CoasterType::GetTrackVoxelIndex(const TrackVoxel *tvx) const
{
	auto match = std::find_if(this->voxels.begin(), this->voxels.end(), [tvx](const TrackVoxel *tv){ return tv == tvx; });
	assert(match != this->voxels.end());
	return std::distance(this->voxels.begin(), match);
}

/** Default constructor, no sprites available yet. */
CoasterPlatform::CoasterPlatform()
{
	this->ne_sw_back  = nullptr;
	this->ne_sw_front = nullptr;
	this->se_nw_back  = nullptr;
	this->se_nw_front = nullptr;
	this->sw_ne_back  = nullptr;
	this->sw_ne_front = nullptr;
	this->nw_se_back  = nullptr;
	this->nw_se_front = nullptr;
}

/**
 * Load a coaster platform (CSPL) block.
 * @param rcdfile Data file being loaded.
 * @param sprites Sprites already loaded from the RCD file.
 * @return Loading the CSPL block succeeded.
 */
bool LoadCoasterPlatform(RcdFileReader *rcdfile, const ImageMap &sprites)
{
	if (rcdfile->version != 2 || rcdfile->size != 2 + 1 + 8 * 4) return false;

	uint16 width = rcdfile->GetUInt16();
	uint8 type = rcdfile->GetUInt8();
	if (width != 64 || type >= CPT_COUNT) return false;

	CoasterPlatform &platform = _coaster_platforms[type];
	platform.tile_width = width;
	platform.type = static_cast<CoasterPlatformType>(type);

	if (!LoadSpriteFromFile(rcdfile, sprites, &platform.ne_sw_back))  return false;
	if (!LoadSpriteFromFile(rcdfile, sprites, &platform.ne_sw_front)) return false;
	if (!LoadSpriteFromFile(rcdfile, sprites, &platform.se_nw_back))  return false;
	if (!LoadSpriteFromFile(rcdfile, sprites, &platform.se_nw_front)) return false;
	if (!LoadSpriteFromFile(rcdfile, sprites, &platform.sw_ne_back))  return false;
	if (!LoadSpriteFromFile(rcdfile, sprites, &platform.sw_ne_front)) return false;
	if (!LoadSpriteFromFile(rcdfile, sprites, &platform.nw_se_back))  return false;
	if (!LoadSpriteFromFile(rcdfile, sprites, &platform.nw_se_front)) return false;
	return true;
}

DisplayCoasterCar::DisplayCoasterCar() : VoxelObject(), yaw(0xff) // Mark everything as invalid.
{
}

const ImageData *DisplayCoasterCar::GetSprite(const SpriteStorage *sprites, ViewOrientation orient, const Recolouring **recolour) const
{
	*recolour = nullptr;
	return this->car_type->GetCar(this->pitch, this->roll, (this->yaw + orient * 4) & 0xF);
}

/**
 * Set the position and orientation of the car. It requests repainting of voxels.
 * @param vox_pos %Voxel position of car.
 * @param pix_pos Position within the voxel (may be outside the \c 0..255 boundary).
 * @param pitch Pitch of the car.
 * @param roll Roll of the car.
 * @param yaw Yaw of the car.
 */
void DisplayCoasterCar::Set(const XYZPoint16 &vox_pos, const XYZPoint16 &pix_pos, uint8 pitch, uint8 roll, uint8 yaw)
{
	bool change_voxel = this->vox_pos != vox_pos;

	if (!change_voxel && this->pix_pos == pix_pos && this->pitch == pitch && this->roll == roll && this->yaw == yaw) {
		return; // Nothing changed.
	}

	if (this->yaw != 0xff && change_voxel) {
		/* Valid data, and changing voxel -> remove self from the old voxel. */
		this->MarkDirty();
		Voxel *v = _world.GetCreateVoxel(this->vox_pos, false);
		this->RemoveSelf(v);
	}

	/* Update voxel and orientation. */
	this->vox_pos = vox_pos;
	this->pix_pos = pix_pos;
	this->pitch = pitch;
	this->roll = roll;
	this->yaw = yaw;

	if (this->yaw != 0xff) {
		this->MarkDirty(); // Voxel or orientation has changed, repaint the possibly new voxel.

		if (change_voxel) { // With a really new voxel, also add self to the new voxel.
			Voxel *v = _world.GetCreateVoxel(this->vox_pos, false);
			this->AddSelf(v);
		}
	}
}

/** Displayed car is about to be removed from the train, clean up if necessary. */
void DisplayCoasterCar::PreRemove()
{
	if (this->yaw == 0xff) return;
	this->MarkDirty();
}

CoasterTrain::CoasterTrain()
{
	this->coaster = nullptr; // Set later during CoasterInstance::CoasterInstance.
	this->back_position = 0;
	this->speed = 0;
	this->cur_piece = nullptr; // Set later.
}

/**
 * Change the length of the train.
 * @param length New length of the train.
 */
void CoasterTrain::SetLength(int length)
{
	if (length > static_cast<int>(this->cars.size())) {
		this->cars.resize(length);
		for (auto &car : this->cars) {
			car.front.car_type = this->coaster->car_type;
			car.back.car_type = this->coaster->car_type;
		}
		return;
	} else if (length < static_cast<int>(this->cars.size())) {
		CoasterCar *car = &this->cars[length];
		for (uint idx = length; idx < this->cars.size(); idx++) {
			car->PreRemove();
			car++;
		}
		this->cars.resize(length);
	}
}

/** Combined sin/cos table for 16 roll positions. sin table starts at 0, cos table start at 4. */
static const float _sin_cos_table[20] = {
	0.00, 0.38, 0.71, 0.92,
	1.00, 0.92, 0.71, 0.38, 0.00, -0.38, -0.71, -0.92, -1.00, -0.92, -0.71, -0.38,
	0.00, 0.38, 0.71, 0.92
};

/**
 * Revert the roll of the coaster car in the direction vector.
 * @param roll Amount of roll of the car.
 * @param dy [inout] Derivative of Y position.
 * @param dz [inout] Derivative of Z position.
 */
static void inline Unroll(uint roll, int32 *dy, int32 *dz)
{
	/* Rotates around X axis, thus dx does not change. */
	int32 new_dy = *dy * _sin_cos_table[roll + 4] - *dz * _sin_cos_table[roll];
	int32 new_dz = *dy * _sin_cos_table[roll]     + *dz * _sin_cos_table[roll + 4];
	*dy = new_dy;
	*dz = new_dz;
}

/**
 * Time has passed, update the position of the train.
 * @param delay Amount of time passed, in milliseconds.
 */
void CoasterTrain::OnAnimate(int delay)
{
	if (this->speed >= 0) {
		this->back_position += this->speed * delay;
		if (this->back_position >= this->coaster->coaster_length) {
			this->back_position -= this->coaster->coaster_length;
			this->cur_piece = this->coaster->pieces;
		}
		while (this->cur_piece->distance_base + this->cur_piece->piece->piece_length < this->back_position) this->cur_piece++;
	} else {
		uint32 change = -this->speed * delay;
		if (change > this->back_position) {
			this->back_position = this->back_position + this->coaster->coaster_length - change;
			/* Update the track piece as well. There is no simple way to get
			 * the last piece, so moving from the front will have to do. */
			this->cur_piece = this->coaster->pieces;
			while (this->cur_piece->distance_base + this->cur_piece->piece->piece_length < this->back_position) this->cur_piece++;
		} else {
			this->back_position -= change;
			while (this->cur_piece->distance_base > this->back_position) this->cur_piece--;
		}
	}
	uint32 car_length = this->coaster->car_type->car_length;
	uint32 position = this->back_position; // Back position of the train / last car.
	const PositionedTrackPiece *ptp = this->cur_piece;
	for (uint i = 0; i < this->cars.size(); i++) {
		CoasterCar &car = this->cars[i];
		if (position >= this->coaster->coaster_length) {
			position -= this->coaster->coaster_length;
			ptp = this->coaster->pieces;
		}
		while (ptp->distance_base + ptp->piece->piece_length < position) ptp++;

		/* Get position of the back of the car. */
		int32 xpos_back = ptp->piece->car_xpos->GetValue(position - ptp->distance_base) + (ptp->base_voxel.x << 8);
		int32 ypos_back = ptp->piece->car_ypos->GetValue(position - ptp->distance_base) + (ptp->base_voxel.y << 8);
		int32 zpos_back = ptp->piece->car_zpos->GetValue(position - ptp->distance_base) * 2 + (ptp->base_voxel.z << 8);

		/* Get roll from the center of the car. */
		position += car_length / 2;
		if (position >= this->coaster->coaster_length) {
			position -= this->coaster->coaster_length;
			ptp = this->coaster->pieces;
		}
		while (ptp->distance_base + ptp->piece->piece_length < position) ptp++;
		uint roll = static_cast<uint>(ptp->piece->car_roll->GetValue(position - ptp->distance_base) + 0.5) & 0xf;

		/* Get position of the front of the car. */
		position += car_length / 2;
		if (position >= this->coaster->coaster_length) {
			position -= this->coaster->coaster_length;
			ptp = this->coaster->pieces;
		}
		while (ptp->distance_base + ptp->piece->piece_length < position) ptp++;
		int32 xpos_front = ptp->piece->car_xpos->GetValue(position - ptp->distance_base) + (ptp->base_voxel.x << 8);
		int32 ypos_front = ptp->piece->car_ypos->GetValue(position - ptp->distance_base) + (ptp->base_voxel.y << 8);
		int32 zpos_front = ptp->piece->car_zpos->GetValue(position - ptp->distance_base) * 2 + (ptp->base_voxel.z << 8);

		int32 xder = xpos_front - xpos_back;
		int32 yder = ypos_front - ypos_back;
		int32 zder = (zpos_front - zpos_back) / 2; // Tile height is half the width.

		int32 xpos_middle = xpos_back + xder / 2; // Compute center point of the car as position to render the car.
		int32 ypos_middle = ypos_back + yder / 2;
		int32 zpos_middle = zpos_back + zder;

		float total_speed  = sqrt(xder * xder + yder * yder + zder * zder);
		/* Gravity */
		this->speed -= zder / total_speed * 9.8;

		/** \todo Air and rail friction */

		if (this->speed < 65536 / 1000 && (this->cur_piece->piece->HasPower() || this->cur_piece->piece->HasPlatform())) {
			this->speed = 65536 / 1000;
		}

		/* Unroll the orientation vector. */
		Unroll(roll, &yder, &zder);
		float horizontal_speed = std::hypot(xder, yder);

		static const double TAN11_25 = 0.198912367379658;  // tan(11.25 degrees).
		static const double TAN33_75 = 0.6681786379192989; // tan(3*11.25 degrees).

		/* Compute pitch. */
		bool swap_dz = false;
		if (zder < 0) {
			swap_dz = true;
			zder = -zder;
		}

		uint pitch;
		if (horizontal_speed < zder) {
			if (horizontal_speed < zder * TAN11_25) {
				pitch = 4;
			} else if (horizontal_speed < zder * TAN33_75) {
				pitch = 3;
			} else {
				pitch = 2;
			}
		} else {
			if (zder < horizontal_speed * TAN11_25) {
				pitch = 0;
			} else if (zder < horizontal_speed * TAN33_75) {
				pitch = 1;
			} else {
				pitch = 2;
			}
		}
		if (swap_dz) pitch = (16 - pitch) & 0xf;

		/* Compute yaw. */
		bool swap_dx = false;
		if (xder > 0) {
			swap_dx = true;
			xder = -xder;
		}
		bool swap_dy = false;
		if (yder > 0) {
			swap_dy = true;
			yder = -yder;
		}
		uint yaw;
		/* There are 16 yaw directions, the 360 degrees needs to be split in 16 pieces.
		 * However the x and y axes are in the middle of a piece, so 360 degrees is split
		 * in 32 parts, and 2 parts form one piece. */
		if (xder < yder) {
			/* In the first 45 degrees. It is split in 4 parts (4*11.25 degrees)
			 * where the 1st part is for direction 0. The 2nd and 3rd part are for direction 1,
			 * and the 4th part is for direction 2. */
			if (xder * TAN11_25 < yder) {
				yaw = 0;
			} else if (xder * TAN33_75 < yder) {
				yaw = 1;
			} else {
				yaw = 2;
			}
		} else {
			/* In the second 45 degrees. It is also split in 4 parts (4*11.25 degrees)
			 * where the 1st part is for direction 2. The 2nd and 3rd part are for direction 3,
			 * and the 4th part is for direction 4.
			 *
			 * Rather than re-inventing a solution, re-use the same checks as
			 * above with swapped xder and yder. */
			if (yder * TAN11_25 < xder) {
				yaw = 4;
			} else if (yder * TAN33_75 < xder) {
				yaw = 3;
			} else {
				yaw = 2;
			}
		}
		if (swap_dx) yaw = 8 - yaw;
		if (swap_dy) yaw = (16 - yaw) & 0xf;

		xpos_back  &= 0xFFFFFF00; // Back and front positions to the north bottom corner of the voxel.
		ypos_back  &= 0xFFFFFF00;
		zpos_back  &= 0xFFFFFF00;
		xpos_front &= 0xFFFFFF00;
		ypos_front &= 0xFFFFFF00;
		zpos_front &= 0xFFFFFF00;
		XYZPoint16 back( xpos_back  >> 8, ypos_back  >> 8, zpos_back  >> 8);
		XYZPoint16 front(xpos_front >> 8, ypos_front >> 8, zpos_front >> 8);

		XYZPoint16 back_pix( xpos_middle - xpos_back,  ypos_middle - ypos_back,  zpos_middle - zpos_back);
		XYZPoint16 front_pix(xpos_middle - xpos_front, ypos_middle - ypos_front, zpos_middle - zpos_front);

		car.back.Set (back,  back_pix,  pitch, roll, yaw);
		car.front.Set(front, front_pix, pitch, roll, yaw);
		position += this->coaster->car_type->inter_car_length;
	}
}

/**
 * Constructor of a roller coaster instance.
 * @param ct Coaster type being built.
 * @param car_type Kind of cars used for the coaster.
 */
CoasterInstance::CoasterInstance(const CoasterType *ct, const CarType *car_type) : RideInstance(ct)
{
	for (int i = 0; i < NUMBER_ITEM_TYPES_SOLD; i++) this->item_price[i] = ct->item_cost[i] * 2;
	this->pieces = new PositionedTrackPiece[MAX_PLACED_TRACK_PIECES]();
	this->capacity = MAX_PLACED_TRACK_PIECES;
	for (uint i = 0; i < lengthof(this->trains); i++) {
		CoasterTrain &train = this->trains[i];
		train.coaster = this;
		train.cur_piece = this->pieces;
	}
	this->car_type = car_type;
}

CoasterInstance::~CoasterInstance()
{
	delete[] this->pieces;
}

/**
 * Can the user click in the world to re-open the coaster instance window for this coaster?
 * @return \c true if an object in the world exists, \c false otherwise.
 */
bool CoasterInstance::IsAccessible()
{
	return this->GetFirstPlacedTrackPiece() >= 0;
}

void CoasterInstance::OnAnimate(int delay)
{
	for (uint i = 0; i < lengthof(this->trains); i++) {
		CoasterTrain &train = this->trains[i];
		if (train.cars.size() == 0) break;
		train.OnAnimate(delay);
	}
}

void CoasterInstance::GetSprites(uint16 voxel_number, uint8 orient, const ImageData *sprites[4]) const
{
	const CoasterType *ct = this->GetCoasterType();
	assert(voxel_number < ct->voxels.size());
	const TrackVoxel *tv = ct->voxels[voxel_number];

	sprites[1] = tv->back[orient];  // SO_RIDE
	sprites[2] = tv->front[orient]; // SO_RIDE_FRONT
	if ((tv->back[orient] == nullptr && tv->front[orient] == nullptr) || !tv->HasPlatform() || ct->platform_type >= CPT_COUNT) {
		sprites[0] = nullptr; // SO_PLATFORM_BACK
		sprites[3] = nullptr; // SO_PLATFORM_FRONT
	} else {
		const CoasterPlatform &platform = _coaster_platforms[ct->platform_type];
		TileEdge edge = static_cast<TileEdge>((tv->GetPlatformDirection() + 4 - orient) & 3);
		switch (edge) {
			case EDGE_NE:
				sprites[0] = platform.ne_sw_back;
				sprites[3] = platform.ne_sw_front;
				break;

			case EDGE_SE:
				sprites[0] = platform.se_nw_back;
				sprites[3] = platform.se_nw_front;
				break;

			case EDGE_SW:
				sprites[0] = platform.sw_ne_back;
				sprites[3] = platform.sw_ne_front;
				break;

			case EDGE_NW:
				sprites[0] = platform.nw_se_back;
				sprites[3] = platform.nw_se_front;
				break;

			default: NOT_REACHED();
		}
	}
}

uint8 CoasterInstance::GetEntranceDirections(const XYZPoint16 &vox) const
{
	return 0; /// \todo add entrance bits for the coaster.
}

RideEntryResult CoasterInstance::EnterRide(int guest, TileEdge edge)
{
	return RER_REFUSED; /// \todo Store the guest number.
}

XYZPoint32 CoasterInstance::GetExit(int guest, TileEdge entry_edge)
{
	assert(false); // Not yet implemented.
}

void CoasterInstance::RemoveAllPeople()
{
	assert(false); // Not yet implemented.
}

/**
 * Check the state of the coaster ride, and set the #state flag.
 * @return The new coaster instance state.
 */
RideInstanceState CoasterInstance::DecideRideState()
{
	bool modified;
	if (!MakePositionedPiecesLooping(&modified)) {
		this->state = RIS_BUILDING;
		return (RideInstanceState)this->state;
	}
	if (modified || (this->state != RIS_CLOSED && this->state != RIS_OPEN)) {
		if (this->GetNumberOfTrains() < 1) { // It's safe to switch to testing, ensure there is at least one train.
			this->SetNumberOfTrains(1);
			this->SetNumberOfCars(1);
		}
		this->state = RIS_TESTING;
	}
	return (RideInstanceState)this->state;
}

/**
 * Retrieve the first placed track piece, if available.
 * @return The index of the first placed track piece, or \c -1 if no such piece available.
 */
int CoasterInstance::GetFirstPlacedTrackPiece() const
{
	for (int i = 0; i < this->capacity; i++) {
		if (this->pieces[i].piece != nullptr) return i;
	}
	return -1;
}

/**
 * Find the first placed track piece at a given position with a given entry connection.
 * @param vox Required voxel position.
 * @param entry_connect Required entry connection.
 * @param start First index to search.
 * @param end End of the search (one beyond the last positioned track piece to search).
 * @return Index of the requested positioned track piece if it exists, else \c -1.
 */
int CoasterInstance::FindSuccessorPiece(const XYZPoint16 &vox, uint8 entry_connect, int start, int end)
{
	if (start < 0) start = 0;
	if (end > MAX_PLACED_TRACK_PIECES) end = MAX_PLACED_TRACK_PIECES;
	for (; start < end; start++) {
		if (this->pieces[start].CanBeSuccessor(vox, entry_connect)) return start;
	}
	return -1;
}

/**
 * Find the first placed track piece that follows a provided placed track piece.
 * @param placed Already placed track piece.
 * @return Index of the requested positioned track piece if it exists, else \c -1.
 */
int CoasterInstance::FindSuccessorPiece(const PositionedTrackPiece &placed)
{
	return this->FindSuccessorPiece(placed.GetEndXYZ(), placed.piece->exit_connect);
}

/**
 * Find the first placed track piece that precedes a provided placed track piece.
 * @param placed Already placed track piece.
 * @return Index of the requested positioned track piece if it exists, else \c -1.
 */
int CoasterInstance::FindPredecessorPiece(const PositionedTrackPiece &placed)
{
	for (int i = 0; i < this->capacity; i++) {
		if (placed.CanBeSuccessor(this->pieces[i])) {
			return i;
		}
	}
	return -1;
}

/**
 * Try to make a loop with the current set of positioned track pieces.
 * @param modified [out] If not \c nullptr, \c false after the call denotes the pieces array was not modified
 *                       (while \c true denotes it was changed).
 * @return Whether the current set of positioned track pieces can be used as a ride.
 * @note May re-order positioned track pieces.
 */
bool CoasterInstance::MakePositionedPiecesLooping(bool *modified)
{
	if (modified != nullptr) *modified = false;

	/* First step, move all non-null track pieces to the start of the array. */
	int count = 0;
	for (int i = 0; i < this->capacity; i++) {
		PositionedTrackPiece *ptp = this->pieces + i;
		if (ptp->piece == nullptr) continue;
		if (i == count) {
			count++;
			continue;
		}
		std::swap(this->pieces[count], *ptp);
		if (modified != nullptr) *modified = true;
		ptp->piece = nullptr;
		count++;
	}

	/* Second step, find a loop from start to end. */
	if (count < 2) return false; // 0 or 1 positioned pieces won't ever make a loop.

	PositionedTrackPiece *ptp = this->pieces;
	uint32 distance = 0;
	if (ptp->distance_base != distance) {
		if (modified != nullptr) *modified = true;
		ptp->distance_base = distance;
	}
	distance += ptp->piece->piece_length;

	for (int i = 1; i < count; i++) {
		int j = this->FindSuccessorPiece(ptp->GetEndXYZ(), ptp->piece->exit_connect, i, count);
		if (j < 0) return false;
		ptp++; // Now points to pieces[i].
		if (i != j) {
			std::swap(*ptp, this->pieces[j]); // Make piece 'j' the next positioned piece.
			if (modified != nullptr) *modified = true;
		}
		if (ptp->distance_base != distance) {
			if (modified != nullptr) *modified = true;
			ptp->distance_base = distance;
		}
		distance += ptp->piece->piece_length;
	}
	this->coaster_length = distance;
	return this->pieces[0].CanBeSuccessor(*ptp);
}

/**
 * Try to add a positioned track piece to the coaster instance.
 * @param placed New positioned track piece to add.
 * @return If the new piece can be added, its index is returned, else \c -1.
 */
int CoasterInstance::AddPositionedPiece(const PositionedTrackPiece &placed)
{
	if (placed.piece == nullptr || !placed.IsOnWorld()) return -1;

	for (int i = 0; i < this->capacity; i++) {
		if (this->pieces[i].piece == nullptr) {
			this->pieces[i] = placed;
			return i;
		}
	}
	return -1;
}

/**
 * Try to remove a positioned track piece from the coaster instance.
 * @param piece Positioned track piece to remove.
 */
void CoasterInstance::RemovePositionedPiece(PositionedTrackPiece &piece)
{
	assert(piece.piece != nullptr);
	this->RemoveTrackPieceInAdditions(piece);
	piece.piece = nullptr;
}

/**
 * Add the positioned track piece to #_additions.
 * @param placed Track piece to place.
 * @pre placed->CanBePlaced() should hold.
 */
void CoasterInstance::PlaceTrackPieceInAdditions(const PositionedTrackPiece &placed)
{
	assert(placed.CanBePlaced());
	assert(this->GetIndex() >= SRI_FULL_RIDES && this->GetIndex() <= SRI_LAST);
	SmallRideInstance ride_number = (SmallRideInstance)this->GetIndex();

	const CoasterType *ct = this->GetCoasterType();
	for (const auto& tvx : placed.piece->track_voxels) {
		Voxel *vx = _additions.GetCreateVoxel(placed.base_voxel + tvx->dxyz, true);
		// assert(vx->CanPlaceInstance()): Checked by this->CanBePlaced().
		vx->SetInstance(ride_number);
		vx->SetInstanceData(ct->GetTrackVoxelIndex(tvx));
	}
}

/**
 * Add 'removal' of the positioned track piece to #_additions.
 * @param placed Track piece to be removed.
 */
void CoasterInstance::RemoveTrackPieceInAdditions(const PositionedTrackPiece &placed)
{
	for (const auto& tvx : placed.piece->track_voxels) {
		Voxel *vx = _additions.GetCreateVoxel(placed.base_voxel + tvx->dxyz, false);
		assert(vx->GetInstance() == (SmallRideInstance)this->GetIndex());
		vx->SetInstance(SRI_FREE);
		vx->SetInstanceData(0); // Not really needed.
	}
}

/**
 * Retrieve how many trains can be used at this roller coaster.
 * @return The maximum number of trains at this coaster.
 * @todo Take the positioned track pieces of the coaster into account.
 */
int CoasterInstance::GetMaxNumberOfTrains() const
{
	int max_num = this->GetCoasterType()->max_number_trains;
	return std::min(max_num, (int)lengthof(this->trains));
}

/**
 * Set the number of trains to use at this coaster.
 * @param number_trains Number of trains to use (\c 0 means 'default').
 * @pre If not default, \a number_trains should be at least 1, and at most #GetMaxNumberOfTrains.
 */
void CoasterInstance::SetNumberOfTrains(int number_trains)
{
	if (number_trains == 0) number_trains = 1; // Default number of trains.
	assert(number_trains >= 1 && number_trains <= this->GetMaxNumberOfTrains());

	int number_cars = this->GetNumberOfCars();
	for (uint i = 0; i < lengthof(this->trains); i++) {
		if (i == (uint)number_trains) number_cars = 0; // From now on, trains are not used.
		CoasterTrain &train = this->trains[i];
		train.SetLength(number_cars);
		train.back_position = 0; /// \todo Needs a better value, coaster trains do not stack.
		train.speed = 0;
		train.cur_piece = this->pieces;
	}
}

/**
 * Get the current number of trains that ride on the coaster.
 * @return current number of trains on the coaster.
 */
int CoasterInstance::GetNumberOfTrains() const
{
	int count = 0;
	for (uint i = 0; i < lengthof(this->trains); i++) {
		if (this->trains[i].cars.size() == 0) break;
		count++;
	}
	return count;
}

/**
 * Get the maximum number of cars of a train.
 * @return The maximum number of cars in a train.
 * @todo Take coaster length and number of trains into account.
 * @todo Take car type into account.
 */
int CoasterInstance::GetMaxNumberOfCars() const
{
	int max_num = this->GetCoasterType()->max_number_cars;
	return max_num;
}

/**
 * Set the number of cars in a single train.
 * @param number_cars Number of cars of a single train.
 * @pre \a number_cars should be at least 1 and at most #GetMaxNumberOfCars.
 */
void CoasterInstance::SetNumberOfCars(int number_cars)
{
	assert(number_cars >= 1 && number_cars <= this->GetMaxNumberOfCars());
	for (uint i = 0; i < lengthof(this->trains); i++) {
		CoasterTrain &train = this->trains[i];
		if (train.cars.size() != 0) train.SetLength(number_cars);
	}
}

/**
 * Retrieve the number of cars available in a single train of the coaster.
 * @return Number of cars in a train.
 */
int CoasterInstance::GetNumberOfCars() const
{
	int number_cars = this->trains[0].cars.size();
	if (number_cars == 0) number_cars = 1;
	return number_cars;
}
