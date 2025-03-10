/* $Id$ */

/*
 * This file is part of FreeRCT.
 * FreeRCT is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * FreeRCT is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with FreeRCT. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file shop_type.cpp Implementation of the shop 'ride'. */

#include "stdafx.h"
#include "map.h"
#include "shop_type.h"
#include "person.h"
#include "people.h"
#include "fileio.h"

#include "generated/shops_strings.cpp"

static const int TOILET_TIME = 5000;  ///< Duration of visiting the toilet.
static const int CAPACITY_TOILET = 2; ///< Maximum number of guests that can use the toilet at the same time.

ShopType::ShopType() : RideType(RTK_SHOP)
{
	this->height = 0;
	std::fill_n(this->views, lengthof(this->views), nullptr);
}

ShopType::~ShopType()
{
	/* Images and texts are handled by the sprite collector, no need to release its memory here. */
}

const ImageData *ShopType::GetView(uint8 orientation) const
{
	return (orientation < 4) ? this->views[orientation] : nullptr;
}

RideInstance *ShopType::CreateInstance() const
{
	return new ShopInstance(this);
}

/**
 * Can the given value be safely interpreted as item type to sell?
 * @param val Value to inspect.
 * @return The provided value is a valid item type value.
 */
static bool IsValidItemType(uint8 val)
{
	switch (val) {
		case ITP_NOTHING:
		case ITP_DRINK:
		case ITP_ICE_CREAM:
		case ITP_NORMAL_FOOD:
		case ITP_SALTY_FOOD:
		case ITP_UMBRELLA:
		case ITP_BALLOON:
		case ITP_PARK_MAP:
		case ITP_SOUVENIR:
		case ITP_MONEY:
		case ITP_TOILET:
		case ITP_FIRST_AID:
			return true;

		default:
			return false;
	}
}

/**
 * Load a type of shop from the RCD file.
 * @param rcd_file Rcd file being loaded.
 * @param sprites Already loaded sprites.
 * @param texts Already loaded texts.
 * @return Loading was successful.
 */
bool ShopType::Load(RcdFileReader *rcd_file, const ImageMap &sprites, const TextMap &texts)
{
	if (rcd_file->version != 5 || rcd_file->size != 2 + 1 + 1 + 4 * 4 + 3 * 4 + 4 * 4 + 2 + 4) return false;
	uint16 width = rcd_file->GetUInt16(); /// \todo Widths other than 64.
	this->height = rcd_file->GetUInt8();
	if (this->height != 1) return false; // Other heights may fail.
	this->flags = rcd_file->GetUInt8() & 0xF;

	for (int i = 0; i < 4; i++) {
		ImageData *view;
		if (!LoadSpriteFromFile(rcd_file, sprites, &view)) return false;
		if (width != 64) continue; // Silently discard other sizes.
		this->views[i] = view;
	}

	for (uint i = 0; i < 3; i++) {
		uint32 recolour = rcd_file->GetUInt32();
		this->recolours.Set(i, RecolourEntry(recolour));
	}
	this->item_cost[0] = rcd_file->GetInt32();
	this->item_cost[1] = rcd_file->GetInt32();
	this->monthly_cost = rcd_file->GetInt32();
	this->monthly_open_cost = rcd_file->GetInt32();

	uint8 val = rcd_file->GetUInt8();
	if (!IsValidItemType(val)) return false;
	this->item_type[0] = (ItemType)val;

	val = rcd_file->GetUInt8();
	if (!IsValidItemType(val)) return false;
	this->item_type[1] = (ItemType)val;

	TextData *text_data;
	if (!LoadTextFromFile(rcd_file, texts, &text_data)) return false;
	StringID base = _language.RegisterStrings(*text_data, _shops_strings_table);
	this->SetupStrings(text_data, base, STR_GENERIC_SHOP_START, SHOPS_STRING_TABLE_END, SHOPS_NAME_TYPE, SHOPS_DESCRIPTION_TYPE);
	return true;
}

/**
 * Compute the capacity of onride guests in the shop.
 * @return Size of a batch in bits 0..7, and number of batches in bits 8..14. \c 0 means guests don't stay in the ride.
 */
int ShopType::GetRideCapacity() const
{
	for (int i = 0; i < NUMBER_ITEM_TYPES_SOLD; i++) {
		if (this->item_type[i] == ITP_TOILET) return 1 | (CAPACITY_TOILET << 8);
	}
	return 0;
}

const StringID *ShopType::GetInstanceNames() const
{
	static const StringID names[] = {SHOPS_NAME_INSTANCE1, SHOPS_NAME_INSTANCE2, STR_INVALID};
	return names;
}

/**
 * Constructor of a shop 'ride'.
 * @param type Kind of shop.
 */
ShopInstance::ShopInstance(const ShopType *type) : RideInstance(type)
{
	this->orientation = 0;

	int capacity = type->GetRideCapacity();
	assert(capacity == 0 || (capacity & 0xFF) == 1); ///< \todo Implement loading of guests into a batch.
	this->onride_guests.Configure(capacity & 0xFF, capacity >> 8);
}

ShopInstance::~ShopInstance()
{
}

/**
 * Get the shop type of the ride.
 * @return The shop type of the ride.
 */
const ShopType *ShopInstance::GetShopType() const
{
	assert(this->type->kind == RTK_SHOP);
	return static_cast<const ShopType *>(this->type);
}

void ShopInstance::GetSprites(uint16 voxel_number, uint8 orient, const ImageData *sprites[4]) const
{
	sprites[0] = nullptr;
	sprites[1] = this->type->GetView((4 + this->orientation - orient) & 3);
	sprites[2] = nullptr;
	sprites[3] = nullptr;
}

/**
 * Update a ride instance with its position in the world.
 * @param orientation Orientation of the shop.
 * @param pos Position of the shop.
 */
void ShopInstance::SetRide(uint8 orientation, const XYZPoint16 &pos)
{
	assert(this->state == RIS_ALLOCATED);
	assert(_world.GetTileOwner(pos.x, pos.y) == OWN_PARK); // May only place it in your own park.

	this->orientation = orientation;
	this->vox_pos = pos;
	this->flags = 0;
}

uint8 ShopInstance::GetEntranceDirections(const XYZPoint16 &vox) const
{
	if (vox != this->vox_pos) return 0;

	uint8 entrances = this->GetShopType()->flags & SHF_ENTRANCE_BITS;
	return ROL(entrances, 4, this->orientation);
}

RideEntryResult ShopInstance::EnterRide(int guest, TileEdge entry)
{
	if (this->onride_guests.num_batches == 0) { // No onride guests, handle it all now.
		Guest *g = _guests.Get(guest);
		g->ExitRide(this, entry);
		return RER_DONE;
	}

	/* Guest should wait for the ride to finish, find a spot. */
	int free_batch = this->onride_guests.GetFreeBatch();
	if (free_batch >= 0) {
		GuestBatch &gb = this->onride_guests.GetBatch(free_batch);
		if (gb.AddGuest(guest, entry)) {
			gb.Start(TOILET_TIME);
			return RER_ENTERED;
		}
	}
	return RER_REFUSED;
}

XYZPoint32 ShopInstance::GetExit(int guest, TileEdge entry_edge)
{
	/* Put the guest just outside the ride. */
	Point16 dxy = _exit_dxy[(entry_edge + 2) % 4];
	return XYZPoint32(this->vox_pos.x * 256 + dxy.x, this->vox_pos.y * 256 + dxy.y, this->vox_pos.z * 256);

}

void ShopInstance::RemoveAllPeople()
{
	for (GuestBatch &gb : this->onride_guests.batches) {
		if (gb.state == BST_EMPTY) continue;

		for (GuestData &gd : gb.guests) {
			if (!gd.IsEmpty()) {
				Guest *g = _guests.Get(gd.guest);
				g->ExitRide(this, gd.entry);

				gd.Clear();
			}
		}
		gb.state = BST_EMPTY;
	}
}

void ShopInstance::OnAnimate(int delay)
{
	this->onride_guests.OnAnimate(delay); // Update remaining time of onride guests.

	/* Kick out guests that are done. */
	int start = -1;
	for (;;) {
		start = this->onride_guests.GetFinishedBatch(start);
		if (start < 0) break;

		GuestBatch &gb = this->onride_guests.batches.at(start);
		GuestData & gd = gb.guests[0];
		if (!gd.IsEmpty()) {
			Guest *g = _guests.Get(gd.guest);
			g->ExitRide(this, gd.entry);

			gd.Clear();
		}
		gb.state = BST_EMPTY;
	}
}
