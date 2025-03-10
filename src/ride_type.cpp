/* $Id$ */

/*
 * This file is part of FreeRCT.
 * FreeRCT is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * FreeRCT is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with FreeRCT. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ride_type.cpp Ride type storage and retrieval. */

#include "stdafx.h"
#include "string_func.h"
#include "sprite_store.h"
#include "language.h"
#include "sprite_store.h"
#include "map.h"
#include "fileio.h"
#include "ride_type.h"
#include "bitmath.h"
#include "gamelevel.h"
#include "window.h"
#include "finances.h"
#include "person.h"
#include "people.h"
#include "random.h"

/**
 * \page Rides
 *
 * Rides are the central concept in what guests 'do' to have fun.
 * The main classes are #RideType and #RideInstance.
 *
 * - The #RideType represents the type of a ride, e.g. "the kiosk" or a "basic steel roller coaster".
 *   - Shop types are implement in #ShopType.
 *   - Coaster types are implemented in #CoasterType.
 *
 * - The #RideInstance represents actual rides in the park.
 *   - Shops instances are implemented in #ShopInstance.
 *   - Coasters instances are implemented in #CoasterInstance.
 *
 * The #RidesManager (#_rides_manager) manages both ride types and ride instances.
 */

RidesManager _rides_manager; ///< Storage and retrieval of ride types and rides in the park.

/**
 * Ride type base class constructor.
 * @param rtk Kind of ride.
 */
RideType::RideType(RideTypeKind rtk) : kind(rtk)
{
	this->monthly_cost      = 12345; // Arbitrary non-zero cost.
	this->monthly_open_cost = 12345; // Arbitrary non-zero cost.
	std::fill_n(this->item_type, NUMBER_ITEM_TYPES_SOLD, ITP_NOTHING);
	std::fill_n(this->item_cost, NUMBER_ITEM_TYPES_SOLD, 12345); // Artbitary non-zero cost.
	this->SetupStrings(nullptr, 0, 0, 0, 0, 0);
}

RideType::~RideType()
{
}

/**
 * Are all resources available for building an instance of this type?
 * For example, are all sprites available? Default implementation always allows building an instance.
 * @return Whether an instance of the ride type can be created.
 */
bool RideType::CanMakeInstance() const
{
	return true;
}

/**
 * \fn RideInstance *RideType::CreateInstance()
 * Construct a ride instance of the ride type.
 * @return An ride of its own type.
 */

/**
 * \fn const ImageData *RideType::GetView(uint8 orientation) const
 * Get a display of the ride type for the purchase screen.
 * @param orientation Orientation of the ride type. Many ride types have \c 4 view orientations,
 *                    but some types may have only a view for orientation \c 0.
 * @return An image to display in the purchase window, or \c nullptr if the queried orientation has no view.
 */

/**
 * \fn const StringID *RideType::GetInstanceNames() const
 * Get the instance base names of rides.
 * @return Array of proposed names, terminated with #STR_INVALID.
 */

/**
 * Setup the the strings of the ride type.
 * @param text Strings from the RCD file.
 * @param base Assigned base number for strings of this type.
 * @param start First generic string number of this type.
 * @param end One beyond the last generic string number of this type.
 * @param name String containing the name of this ride type.
 * @param desc String containing the description of this ride type.
 */
void RideType::SetupStrings(TextData *text, StringID base, StringID start, StringID end, StringID name, StringID desc)
{
	this->text = text;
	this->str_base = base;
	this->str_start = start;
	this->str_end = end;
	this->str_name = name;
	this->str_description = desc;
}

/**
 * Retrieve the string with the name of this type of ride.
 * @return The name of this ride type.
 */
StringID RideType::GetTypeName() const
{
	return this->str_name;
}

/**
 * Retrieve the string with the description of this type of ride.
 * @return The description of this ride type.
 */
StringID RideType::GetTypeDescription() const
{
	return this->str_description;
}

/**
 * Get the string instance for the generic ride string of \a number.
 * @param number Generic ride string number to retrieve.
 * @return The instantiated string for this ride type.
 */
StringID RideType::GetString(uint16 number) const
{
	assert(number >= this->str_start && number < this->str_end);
	return this->str_base + (number - this->str_start);
}

/**
 * Constructor of the base ride instance class.
 * @param rt Type of the ride instance.
 */
RideInstance::RideInstance(const RideType *rt)
{
	this->name[0] = '\0';
	this->type = rt;
	this->state = RIS_ALLOCATED;
	this->flags = 0;
	this->recolours = rt->recolours;
	this->recolours.AssignRandomColours();
	std::fill_n(this->item_price, NUMBER_ITEM_TYPES_SOLD, 12345); // Arbitrary non-zero amount.
	std::fill_n(this->item_count, NUMBER_ITEM_TYPES_SOLD, 0);

	this->reliability = 365 / 2; // \todo Make different reliabilities for different rides; read from RCDs
	this->breakdown_ctr = -1;
	this->breakdown_state = BDS_UNOPENED;
}

RideInstance::~RideInstance()
{
	/* Nothing to do currently. In the future, free instance memory. */
}

/**
 * Get the kind of the ride.
 * @return the kind of the ride.
 */
RideTypeKind RideInstance::GetKind() const
{
	return this->type->kind;
}

/**
 * Get the ride type of the instance.
 * @return The ride type.
 */
const RideType *RideInstance::GetRideType() const
{
	return this->type;
}

/**
 * \fn void RideInstance::GetSprites(uint16 voxel_number, uint8 orient, const ImageData *sprites[4]) const
 * Get the sprites to display for the provided voxel number.
 * @param voxel_number Number of the voxel to draw (copied from the world voxel data).
 * @param orient View orientation.
 * @param sprites [out] Sprites to draw, from back to front, #SO_PLATFORM_BACK, #SO_RIDE, #SO_RIDE_FRONT, and #SO_PLATFORM_FRONT.
 */

/**
 * \fn uint8 RideInstance::GetEntranceDirections(const XYZPoint16 &vox) const
 * Get the set edges with an entrance to the ride.
 * @param vox Position of the voxel with the ride.
 * @return Bit set of #TileEdge that allows entering the ride (seen from the ride).
 */

/**
 * \fn RideEntryResult RideInstance::EnterRide(int guest, TileEdge entry_edge)
 * The given guest tries to enter the ride. Meaning and further handling of the ride visit depends on the return value.
 * - If the call returns #RER_REFUSED, the guest is not given entry (ride is full), it should be tried again at another time.
 * - If the call returns #RER_ENTERED, the guest is given access, and is now staying in the ride. The ride calls Guest::ExitRide when it is done.
 * - If the call returns #RER_DONE, the guest is given access, and the ride is also immediately visited (Guest::ExitRide is called before returning this answer).
 * @param guest Number of the guest entering the ride.
 * @param entry_edge Edge of entering the ride. Value is passed back through Guest::ExitRide, to use as parameter in RideInstance::GetExit.
 * @return Whether the guest is accepted, and if so, if the guest is staying inside.
 */

/**
 * \fn XYZPoint32 RideInstance::GetExit(int guest, TileEdge entry_edge)
 * Get the exit coordinates of the ride, is near the middle of a tile edge.
 * @param guest Number of the guest querying the exit coordinates.
 * @param entry_edge %Edge used for entering the ride.
 * @return World position of the exit.
 */

/**
 * \fn void RideInstance::RemoveAllPeople()
 * Immediately remove all guests and staff which is inside the ride.
 */

/**
 * Can the ride be visited, assuming it is approached from direction \a edge?
 * @param vox Position of the voxel with the ride.
 * @param edge Direction of movement (exit direction of the neighbouring voxel).
 * @return Whether the ride can be visited.
 */
bool RideInstance::CanBeVisited(const XYZPoint16 &vox, TileEdge edge) const
{
	if (this->state != RIS_OPEN) return false;
	return GB(this->GetEntranceDirections(vox), (edge + 2) % 4, 1) != 0;
}

/**
 * Sell an item to a customer.
 * @param item_index Index of the item being sold.
 */
void RideInstance::SellItem(int item_index)
{
	assert(item_index >= 0 && item_index < NUMBER_ITEM_TYPES_SOLD);

	this->item_count[item_index]++;
	Money profit = this->item_price[item_index] - this->type->item_cost[item_index];
	this->total_sell_profit += profit;
	this->total_profit += profit;

	_finances_manager.PayShopStock(this->type->item_cost[item_index]);
	_finances_manager.EarnShopSales(this->item_price[item_index]);
	NotifyChange(WC_SHOP_MANAGER, this->GetIndex(), CHG_DISPLAY_OLD, 0);
}

/**
 * Get the type of items sold by a ride.
 * @param item_index Index in the items being sold (\c 0 to #NUMBER_ITEM_TYPES_SOLD).
 * @return Type of item sold.
 */
ItemType RideInstance::GetSaleItemType(int item_index) const
{
	assert(item_index >= 0 && item_index < NUMBER_ITEM_TYPES_SOLD);
	return this->type->item_type[item_index];
}

/**
 * Get the price of an item sold by a ride.
 * @param item_index Index in the items being sold (\c 0 to #NUMBER_ITEM_TYPES_SOLD).
 * @return Price to pay for the item.
 */
Money RideInstance::GetSaleItemPrice(int item_index) const
{
	assert(item_index >= 0 && item_index < NUMBER_ITEM_TYPES_SOLD);
	return this->item_price[item_index];
}

/**
 * Some time has passed, update the state of the ride. Default implementation does nothing.
 * @param delay Number of milliseconds that passed.
 */
void RideInstance::OnAnimate(int delay)
{
}

/** Monthly update of the shop administration. */
void RideInstance::OnNewMonth()
{
	this->total_profit -= this->type->monthly_cost;
	_finances_manager.PayStaffWages(this->type->monthly_cost);
	SB(this->flags, RIF_MONTHLY_PAID, 1, 1);
	if (this->state == RIS_OPEN) {
		this->total_profit -= this->type->monthly_open_cost;
		_finances_manager.PayStaffWages(this->type->monthly_open_cost);
		SB(this->flags, RIF_OPENED_PAID, 1, 1);
	} else {
		SB(this->flags, RIF_OPENED_PAID, 1, 0);
	}

	NotifyChange(WC_SHOP_MANAGER, this->GetIndex(), CHG_DISPLAY_OLD, 0);
}

/** Daily update of breakages and maintenance. */
void RideInstance::OnNewDay()
{
	this->HandleBreakdown();
}

/** Handle breakdowns of rides. */
void RideInstance::HandleBreakdown()
{
	if (this->state != RIS_OPEN) return;

	switch (this->breakdown_state) {
		case BDS_UNOPENED:
		case BDS_BROKEN:
			break;

		case BDS_NEEDS_NEW_CTR:
			this->breakdown_ctr = this->rnd.Exponential(this->reliability);
			this->breakdown_state = BDS_WILL_BREAK;
			break;

		case BDS_WILL_BREAK:
			this->breakdown_ctr--;
			break;

		default:
			NOT_REACHED();
	}

	if (this->breakdown_ctr <= 0) {
		this->breakdown_state = BDS_BROKEN;
		/* \todo Call a mechanic. */
	}
}

/**
 * Open the ride for the public.
 * @pre The ride is open.
 */
void RideInstance::OpenRide()
{
	assert(this->state == RIS_CLOSED);
	this->state = RIS_OPEN;
	if (this->breakdown_state == BDS_UNOPENED) {
		this->breakdown_ctr = this->rnd.Exponential(this->reliability) + BREAKDOWN_GRACE_PERIOD;
		this->breakdown_state = BDS_WILL_BREAK;
	}

	/* Perform payments if they have not been done this month. */
	bool money_paid = false;
	if (GB(this->flags, RIF_MONTHLY_PAID, 1) == 0) {
		this->total_profit -= this->type->monthly_cost;
		_finances_manager.PayStaffWages(this->type->monthly_cost);
		SB(this->flags, RIF_MONTHLY_PAID, 1, 1);
		money_paid = true;
	}
	if (GB(this->flags, RIF_OPENED_PAID, 1) == 0) {
		this->total_profit -= this->type->monthly_open_cost;
		_finances_manager.PayStaffWages(this->type->monthly_open_cost);
		SB(this->flags, RIF_OPENED_PAID, 1, 1);
		money_paid = true;
	}
	if (money_paid) NotifyChange(WC_SHOP_MANAGER, this->GetIndex(), CHG_DISPLAY_OLD, 0);
}

/**
 * Close the ride for the public.
 * @todo Currently closing is instantly, we may want to have a transition phase here.
 * @pre The ride is open.
 */
void RideInstance::CloseRide()
{
	this->state = RIS_CLOSED;
}

/**
 * Switch the ride to being constructed.
 * @pre The ride should not be open.
 */
void RideInstance::BuildRide()
{
	assert(this->state != RIS_OPEN);
	this->state = RIS_BUILDING;
}

/** Default constructor of the rides manager. */
RidesManager::RidesManager()
{
	std::fill_n(this->ride_types, lengthof(this->ride_types), nullptr);
	std::fill_n(this->instances, lengthof(this->instances), nullptr);
}

RidesManager::~RidesManager()
{
	for (uint i = 0; i < lengthof(this->ride_types); i++) delete this->ride_types[i];
	for (uint i = 0; i < lengthof(this->instances); i++) delete this->instances[i];
}

/**
 * Some time has passed, update the state of the rides.
 * @param delay Number of milliseconds that passed.
 */
void RidesManager::OnAnimate(int delay)
{
	for (uint16 i = 0; i < lengthof(this->instances); i++) {
		if (this->instances[i] == nullptr || this->instances[i]->state == RIS_ALLOCATED) continue;
		this->instances[i]->OnAnimate(delay);
	}
}

/** A new month has started; perform monthly payments. */
void RidesManager::OnNewMonth()
{
	for (uint16 i = 0; i < lengthof(this->instances); i++) {
		if (this->instances[i] == nullptr || this->instances[i]->state == RIS_ALLOCATED) continue;
		this->instances[i]->OnNewMonth();
	}
}

/** A new day has started; break rides randomly. */
void RidesManager::OnNewDay()
{
	for (uint16 i = 0; i < lengthof(this->instances); i++) {
		if (this->instances[i] == nullptr || this->instances[i]->state == RIS_ALLOCATED) continue;
		this->instances[i]->OnNewDay();
	}
}

/**
 * Get the requested ride instance.
 * @param num Ride number to retrieve.
 * @return The requested ride, or \c nullptr if not available.
 */
RideInstance *RidesManager::GetRideInstance(uint16 num)
{
	assert(num >= SRI_FULL_RIDES && num < SRI_LAST);
	num -= SRI_FULL_RIDES;
	if (num >= lengthof(this->instances)) return nullptr;
	return this->instances[num];
}

/**
 * Get the requested ride instance (read-only).
 * @param num Ride instance to retrieve.
 * @return The requested ride, or \c nullptr if not available.
 */
const RideInstance *RidesManager::GetRideInstance(uint16 num) const
{
	assert(num >= SRI_FULL_RIDES && num < SRI_LAST);
	num -= SRI_FULL_RIDES;
	if (num >= lengthof(this->instances)) return nullptr;
	return this->instances[num];
}

/**
 * Get the ride instance index number.
 * @return Ride instance index.
 */
uint16 RideInstance::GetIndex() const
{
	for (uint i = 0; i < lengthof(_rides_manager.instances); i++) {
		if (_rides_manager.instances[i] == this) return i + SRI_FULL_RIDES;
	}
	NOT_REACHED();
}

/**
 * Add a new ride type to the manager.
 * @param type New ride type to add.
 * @return \c true if the addition was successful, else \c false.
 */
bool RidesManager::AddRideType(RideType *type)
{
	for (uint i = 0; i < lengthof(this->ride_types); i++) {
		if (this->ride_types[i] == nullptr) {
			this->ride_types[i] = type;
			return true;
		}
	}
	return false;
}

/**
 * Check whether the ride type can actually be created.
 * @param type Ride type that should be created.
 * @return Index of a free instance if it exists (claim it immediately using #CreateInstance), or #INVALID_RIDE_INSTANCE if all instances are used.
 */
uint16 RidesManager::GetFreeInstance(const RideType *type)
{
	uint16 idx;
	for (idx = 0; idx < lengthof(this->instances); idx++) {
		if (this->instances[idx] == nullptr) break;
	}
	return (idx < lengthof(this->instances) && type->CanMakeInstance()) ? idx + SRI_FULL_RIDES : INVALID_RIDE_INSTANCE;
}

/**
 * Create a new ride instance.
 * @param type Type of ride to construct.
 * @param num Ride number.
 * @return The created ride.
 */
RideInstance *RidesManager::CreateInstance(const RideType *type, uint16 num)
{
	assert(num >= SRI_FULL_RIDES && num < SRI_LAST);
	num -= SRI_FULL_RIDES;
	assert(num < lengthof(this->instances));
	assert(this->instances[num] == nullptr);
	this->instances[num] = type->CreateInstance();
	return this->instances[num];
}

/**
 * Check whether a ride exists with the given name.
 * @param name Name of the ride to find.
 * @return The ride with the given name, or \c nullptr if no such ride exists.
 */
RideInstance *RidesManager::FindRideByName(const uint8 *name)
{
	for (uint16 i = 0; i < lengthof(this->instances); i++) {
		if (this->instances[i] == nullptr || this->instances[i]->state == RIS_ALLOCATED) continue;
		if (StrEqual(name, this->instances[i]->name)) return this->instances[i];
	}
	return nullptr;
}

/**
 * A new ride instance was added. Initialize it further.
 * @param num Ride number of the new ride.
 */
void RidesManager::NewInstanceAdded(uint16 num)
{
	RideInstance *ri = this->GetRideInstance(num);
	const RideType *rt = ri->GetRideType();
	assert(ri->state == RIS_ALLOCATED);

	/* Find a new name for the instance. */
	const StringID *names = rt->GetInstanceNames();
	assert(names != nullptr && names[0] != STR_INVALID); // Empty array of names loops forever below.
	int idx = 0;
	int shop_num = 1;
	for (;;) {
		if (names[idx] == STR_INVALID) {
			shop_num++;
			idx = 0;
		}

		/* Construct a new name. */
		if (shop_num == 1) {
			DrawText(rt->GetString(names[idx]), ri->name, lengthof(ri->name));
		} else {
			_str_params.SetStrID(1, rt->GetString(names[idx]));
			_str_params.SetNumber(2, shop_num);
			DrawText(GUI_NUMBERED_INSTANCE_NAME, ri->name, lengthof(ri->name));
		}

		if (this->FindRideByName(ri->name) == nullptr) break;

		idx++;
	}

	/* Initialize money and counters. */
	ri->total_profit = 0;
	ri->total_sell_profit = 0;
	for (int i = 0; i < NUMBER_ITEM_TYPES_SOLD; i++) {
		ri->item_price[i] = rt->item_cost[i] * 12 / 10; // Make 20% profit.
		ri->item_count[i] = 0;
	}

	switch (ri->GetKind()) {
		case RTK_SHOP:
			ri->CloseRide();
			break;

		case RTK_COASTER:
			ri->BuildRide();
			break;

		default:
			NOT_REACHED(); /// \todo Add other ride types.
	}
}

/**
 * Destroy the indicated instance.
 * @param num The ride number to destroy.
 * @todo The small matter of cleaning up in the world map.
 * @pre Instance must be closed.
 */
void RidesManager::DeleteInstance(uint16 num)
{
	assert(num >= SRI_FULL_RIDES && num < SRI_LAST);
	num -= SRI_FULL_RIDES;
	assert(num < lengthof(this->instances));
	this->instances[num]->RemoveAllPeople();
	_guests.NotifyRideDeletion(this->instances[num]);
	delete this->instances[num];
	this->instances[num] = nullptr;
}

/**
 * Check that no rides are under construction at the moment of calling.
 * @note This is just a checking function, perhaps eventually remove it?
 */
void RidesManager::CheckNoAllocatedRides() const
{
	for (uint i = 0; i < lengthof(this->instances); i++) {
		assert(this->instances[i] == nullptr || this->instances[i]->state != RIS_ALLOCATED);
	}
}

/**
 * Does a ride entrance exists at/to the bottom the given voxel in the neighbouring voxel?
 * @param pos Coordinate of the voxel.
 * @param edge Direction to move to get the neighbouring voxel.
 * @pre voxel coordinate must be valid in the world.
 * @return The ride at the neighbouring voxel, if available (else \c nullptr is returned).
 */
RideInstance *RideExistsAtBottom(XYZPoint16 pos, TileEdge edge)
{
	pos.x += _tile_dxy[edge].x;
	pos.y += _tile_dxy[edge].y;
	if (!IsVoxelstackInsideWorld(pos.x, pos.y)) return nullptr;

	const Voxel *vx = _world.GetVoxel(pos);
	if (vx == nullptr || vx->GetInstance() < SRI_FULL_RIDES) {
		/* No ride here, check the voxel below. */
		if (pos.z == 0) return nullptr;
		vx = _world.GetVoxel(pos + XYZPoint16(0, 0, -1));
	}
	if (vx == nullptr || vx->GetInstance() < SRI_FULL_RIDES) return nullptr;
	return _rides_manager.GetRideInstance(vx->GetInstance());
}
