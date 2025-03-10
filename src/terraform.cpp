/* $Id$ */

/*
 * This file is part of FreeRCT.
 * FreeRCT is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * FreeRCT is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with FreeRCT. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file terraform.cpp Terraforming code. */

#include "stdafx.h"
#include "map.h"
#include "viewport.h"
#include "terraform.h"
#include "gamemode.h"
#include "math_func.h"
#include "memory.h"

TileTerraformMouseMode _terraformer; ///< Terraform coordination object.

/**
 * Structure describing a corner at a voxel stack.
 * @ingroup map_group
 */
struct VoxelCorner {
	Point16 rel_xy;    ///< Relative voxel stack position.
	TileCorner corner; ///< Corner of the voxel.
};

/**
 * Description of neighbouring corners of a corner at a ground tile.
 * #left_neighbour and #right_neighbour are neighbours at the same tile, while
 * #neighbour_tiles list neighbouring corners at the three tiles around the
 * corner.
 * @ingroup map_group
 */
struct CornerNeighbours {
	TileCorner left_neighbour;      ///< Left neighbouring corner.
	TileCorner right_neighbour;     ///< Right neighbouring corner.
	VoxelCorner neighbour_tiles[3]; ///< Neighbouring corners at other tiles.
};

/**
 * Neighbouring corners of each corner.
 * @ingroup map_group
 */
static const CornerNeighbours neighbours[4] = {
	{TC_EAST,  TC_WEST,  { {{-1, -1}, TC_SOUTH}, {{-1,  0}, TC_WEST }, {{ 0, -1}, TC_EAST }} }, // TC_NORTH
	{TC_NORTH, TC_SOUTH, { {{-1,  0}, TC_SOUTH}, {{-1,  1}, TC_WEST }, {{ 0,  1}, TC_NORTH}} }, // TC_EAST
	{TC_EAST,  TC_WEST,  { {{ 0,  1}, TC_WEST }, {{ 1,  1}, TC_NORTH}, {{ 1,  0}, TC_EAST }} }, // TC_SOUTH
	{TC_SOUTH, TC_NORTH, { {{ 0, -1}, TC_SOUTH}, {{ 1, -1}, TC_EAST }, {{ 1,  0}, TC_NORTH}} }, // TC_WEST
};

/**
 * Construct a #GroundData structure.
 * @param height Height of the voxel containing the surface (for steep slopes, the base height).
 * @param orig_slope Original slope (that is, before the raise or lower).
 */
GroundData::GroundData(uint8 height, uint8 orig_slope)
{
	this->height = height;
	this->orig_slope = orig_slope;
	this->modified = 0;
}

/**
 * Get original height (before changing).
 * @param corner Corner to get height.
 * @return Original height of the indicated corner.
 */
uint8 GroundData::GetOrigHeight(TileCorner corner) const
{
	assert(corner >= TC_NORTH && corner < TC_END);
	if ((this->orig_slope & TSB_STEEP) == 0) { // Normal slope.
		if ((this->orig_slope & (1 << corner)) == 0) return this->height;
		return this->height + 1;
	}
	// Steep slope (note that the constructor made sure this->height is at the base of the steep slope).
	if ((this->orig_slope & (1 << corner)) != 0) return this->height + 2;
	corner = (TileCorner)((corner + 2) % 4);
	if ((this->orig_slope & (1 << corner)) != 0) return this->height;
	return this->height + 1;
}

/**
 * Set the given corner as modified.
 * @param corner Corner to set.
 * @return corner is modified.
 */
void GroundData::SetCornerModified(TileCorner corner)
{
	assert(corner >= TC_NORTH && corner < TC_END);
	this->modified |= 1 << corner;
}

/**
 * Return whether the given corner is modified or not.
 * @param corner Corner to test.
 * @return corner is modified.
 */
bool GroundData::GetCornerModified(TileCorner corner) const
{
	assert(corner >= TC_NORTH && corner < TC_END);
	return (this->modified & (1 << corner)) != 0;
}

/**
 * Terrain changes storage constructor.
 * @param base Base coordinate of the part of the world which is smoothly updated.
 * @param xsize Horizontal size of the world part.
 * @param ysize Vertical size of the world part.
 */
TerrainChanges::TerrainChanges(const Point32 &base, uint16 xsize, uint16 ysize)
{
	assert(base.x >= 0 && base.y >= 0 && xsize > 0 && ysize > 0
			&& base.x + xsize <= _world.GetXSize() && base.y + ysize <= _world.GetYSize());
	this->base = base;
	this->xsize = xsize;
	this->ysize = ysize;
}

/** Destructor. */
TerrainChanges::~TerrainChanges()
{
}

/**
 * Get ground data of a voxel stack.
 * @param pos Voxel stack position.
 * @return Pointer to the ground data, or \c nullptr if outside the world.
 */
GroundData *TerrainChanges::GetGroundData(const Point32 &pos)
{
	if (pos.x < this->base.x || pos.x >= this->base.x + this->xsize) return nullptr;
	if (pos.y < this->base.y || pos.y >= this->base.y + this->ysize) return nullptr;

	auto iter = this->changes.find(pos);
	if (iter == this->changes.end()) {
		uint8 height = _world.GetGroundHeight(pos.x, pos.y);
		const Voxel *v = _world.GetVoxel(XYZPoint16(pos.x, pos.y, height));
		assert(v != nullptr && v->GetGroundType() != GTP_INVALID);
		std::pair<Point32, GroundData> p(pos, GroundData(height, ExpandTileSlope(v->GetGroundSlope())));
		iter = this->changes.insert(p).first;
	}
	return &(*iter).second;
}

/**
 * Test every corner of the given voxel for its original height, and find the extreme value.
 * @param pos %Voxel position.
 * @param direction Levelling direction (decides whether to find the lowest or highest corner).
 * @param height [inout] Extremest height found so far.
 */
void TerrainChanges::UpdatelevellingHeight(const Point32 &pos, int direction, uint8 *height)
{
	const GroundData *gd = this->GetGroundData(pos);

	if (direction > 0) { // Raising terrain, find the lowest corner.
		*height = std::min(*height, gd->GetOrigHeight(TC_NORTH));
		*height = std::min(*height, gd->GetOrigHeight(TC_EAST));
		*height = std::min(*height, gd->GetOrigHeight(TC_SOUTH));
		*height = std::min(*height, gd->GetOrigHeight(TC_WEST));
	} else { // Lowering terrain, find the highest corner.
		*height = std::max(*height, gd->GetOrigHeight(TC_NORTH));
		*height = std::max(*height, gd->GetOrigHeight(TC_EAST));
		*height = std::max(*height, gd->GetOrigHeight(TC_SOUTH));
		*height = std::max(*height, gd->GetOrigHeight(TC_WEST));
	}
}

/**
 * Change corners of a voxel if they are within the height constraint.
 * @param pos %Voxel position.
 * @param height Minimum or maximum height of the corners to modify.
 * @param direction Levelling direction (decides what constraint to use).
 * @return Change is OK for the map.
 */
bool TerrainChanges::ChangeVoxel(const Point32 &pos, uint8 height, int direction)
{
	GroundData *gd = this->GetGroundData(pos);
	bool ok = true;
	if (direction > 0) { // Raising terrain, raise everything at or below 'height'.
		if (gd->GetOrigHeight(TC_NORTH) <= height) ok &= this->ChangeCorner(pos, TC_NORTH, direction);
		if (gd->GetOrigHeight(TC_EAST)  <= height) ok &= this->ChangeCorner(pos, TC_EAST,  direction);
		if (gd->GetOrigHeight(TC_SOUTH) <= height) ok &= this->ChangeCorner(pos, TC_SOUTH, direction);
		if (gd->GetOrigHeight(TC_WEST)  <= height) ok &= this->ChangeCorner(pos, TC_WEST,  direction);
	} else { // Lowering terrain, lower everything above or at 'height'.
		if (gd->GetOrigHeight(TC_NORTH) >= height) ok &= this->ChangeCorner(pos, TC_NORTH, direction);
		if (gd->GetOrigHeight(TC_EAST)  >= height) ok &= this->ChangeCorner(pos, TC_EAST,  direction);
		if (gd->GetOrigHeight(TC_SOUTH) >= height) ok &= this->ChangeCorner(pos, TC_SOUTH, direction);
		if (gd->GetOrigHeight(TC_WEST)  >= height) ok &= this->ChangeCorner(pos, TC_WEST,  direction);
	}
	return ok;
}

/**
 * Change the height of a corner. Call this function for every corner you want to change.
 * @param pos Position of the voxel stack.
 * @param corner Corner to change.
 * @param direction Direction of change.
 * @return Change is OK for the map.
 */
bool TerrainChanges::ChangeCorner(const Point32 &pos, TileCorner corner, int direction)
{
	assert(corner >= TC_NORTH && corner < TC_END);
	assert(direction == 1 || direction == -1);

	GroundData *gd = this->GetGroundData(pos);
	if (gd == nullptr) return true; // Out of the bounds in the world, silently ignore.
	if (gd->GetCornerModified(corner)) return true; // Corner already changed.

	if (_game_mode_mgr.InPlayMode() && _world.GetTileOwner(pos.x, pos.y) != OWN_PARK) return false;

	uint8 old_height = gd->GetOrigHeight(corner);
	if (direction > 0 && old_height == WORLD_Z_SIZE) return false; // Cannot change above top.
	if (direction < 0 && old_height == 0) return false; // Cannot change below bottom.

	gd->SetCornerModified(corner); // Mark corner as modified.

	/* Change neighbouring corners at the same tile. */
	if (direction > 0) {
		uint8 h = gd->GetOrigHeight(neighbours[corner].left_neighbour);
		if (h < old_height && !this->ChangeCorner(pos, neighbours[corner].left_neighbour, direction)) return false;
		h = gd->GetOrigHeight(neighbours[corner].right_neighbour);
		if (h < old_height && !this->ChangeCorner(pos, neighbours[corner].right_neighbour, direction)) return false;
	} else {
		uint8 h = gd->GetOrigHeight(neighbours[corner].left_neighbour);
		if (h > old_height && !this->ChangeCorner(pos, neighbours[corner].left_neighbour, direction)) return false;
		h = gd->GetOrigHeight(neighbours[corner].right_neighbour);
		if (h > old_height && !this->ChangeCorner(pos, neighbours[corner].right_neighbour, direction)) return false;
	}

	for (uint i = 0; i < 3; i++) {
		const VoxelCorner &vc = neighbours[corner].neighbour_tiles[i];
		Point32 pos2(pos.x + vc.rel_xy.x, pos.y + vc.rel_xy.y);
		gd = this->GetGroundData(pos2);
		if (gd == nullptr) continue;
		if (_world.GetTileOwner(pos2.x, pos2.y) != OWN_PARK) continue;
		uint height = gd->GetOrigHeight(vc.corner);
		if (old_height == height && !this->ChangeCorner(pos2, vc.corner, direction)) return false;
	}
	return true;
}

/**
 * Set an upper boundary of the height of each tile corner based on the contents of a voxel.
 * @param v %Voxel to examine.
 * @param height Height of the voxel.
 * @param bounds [inout] Updated constraints.
 * @note Length of the \a bounds array should be 4.
 */
static void SetUpperBoundary(const Voxel *v, uint8 height, uint8 *bounds)
{
	if (v == nullptr || v->IsEmpty()) return;

	assert(v->GetGroundType() == GTP_INVALID);
	uint16 instance = v->GetInstance();
	if (instance >= SRI_FULL_RIDES) { // A ride needs the entire voxel.
		std::fill_n(bounds, 4, height);
		return;
	}

	if (instance < SRI_RIDES_START) return;

	/* Small rides, that is, a path. */
	if (!HasValidPath(v)) return;
	PathSprites ps = GetImplodedPathSlope(v);
	switch (ps) {
		case PATH_RAMP_NE:
			bounds[TC_NORTH] = height;     bounds[TC_EAST] = height;
			bounds[TC_SOUTH] = height + 1; bounds[TC_WEST] = height + 1;
			break;

		case PATH_RAMP_NW:
			bounds[TC_NORTH] = height;     bounds[TC_WEST] = height;
			bounds[TC_SOUTH] = height + 1; bounds[TC_EAST] = height + 1;
			break;

		case PATH_RAMP_SE:
			bounds[TC_SOUTH] = height;     bounds[TC_EAST] = height;
			bounds[TC_NORTH] = height + 1; bounds[TC_WEST] = height + 1;
			break;

		case PATH_RAMP_SW:
			bounds[TC_SOUTH] = height;     bounds[TC_WEST] = height;
			bounds[TC_NORTH] = height + 1; bounds[TC_EAST] = height + 1;
			break;

		default:
			assert(ps < PATH_FLAT_COUNT);
			std::fill_n(bounds, 4, height);
			break;
	}
}

/**
 * Set foundations in a stack.
 * @param stack %Voxel stack to change (may be \c nullptr).
 * @param my_first Height of my first corner.
 * @param my_second Height of my second corner.
 * @param other_first Height of corner opposite to \a my_first.
 * @param other_second Height of corner opposite to \a my_second.
 * @param first_bit Bit value to use for a raised foundation at the first corner.
 * @param second_bit Bit value to use for a raised foundation at the second corner.
 */
static void SetFoundations(VoxelStack *stack, uint8 my_first, uint8 my_second, uint8 other_first, uint8 other_second, uint8 first_bit, uint8 second_bit)
{
	if (stack == nullptr) return;

	uint8 and_bits = ~(first_bit | second_bit);
	bool is_higher = my_first > other_first || my_second > other_second; // At least one of my corners must be higher to ever add foundations.

	uint8 h = stack->base;
	uint8 highest = stack->base + stack->height;

	if (other_first  < h) h = other_first;
	if (other_second < h) h = other_second;

	while (h < highest) {
		uint8 bits = 0;
		if (is_higher && (h >= other_first || h >= other_second)) {
			if (h < my_first)  bits |= first_bit;
			if (h < my_second) bits |= second_bit;
		}

		Voxel *v = stack->GetCreate(h, true);
		h++;
		if (bits == 0) { // Delete foundations.
			if (v->GetFoundationType() == FDT_INVALID) continue;
			bits = v->GetFoundationSlope() & and_bits;
			v->SetFoundationSlope(bits);
			if (bits == 0) v->SetFoundationType(FDT_INVALID);
			continue;
		} else { // Add foundations.
			if (v->GetFoundationType() == FDT_INVALID) {
				v->SetFoundationType(FDT_GROUND); // XXX We have nice foundation types, but no way to select them here.
			} else {
				bits |= (v->GetFoundationSlope() & and_bits);
			}
			v->SetFoundationSlope(bits);
			continue;
		}
	}
}

/**
 * Update the foundations in two #_additions voxel stacks along the SW edge of the first stack and the NE edge of the second stack.
 * @param xpos X position of the first voxel stack.
 * @param ypos Y position of the first voxel stack.
 * @note The first or the second voxel stack may be off-world.
 */
static void SetXFoundations(int xpos, int ypos)
{
	VoxelStack *first = (xpos < 0) ? nullptr : _additions.GetModifyStack(xpos, ypos);
	VoxelStack *second = (xpos + 1 == _world.GetXSize()) ? nullptr : _additions.GetModifyStack(xpos + 1, ypos);
	assert(first != nullptr || second != nullptr);

	/* Get ground height at all corners. */
	uint8 first_south = 0;
	uint8 first_west  = 0;
	if (first != nullptr) {
		for (uint i = 0; i < first->height; i++) {
			const Voxel *v = &first->voxels[i];
			if (v->GetGroundType() == GTP_INVALID) continue;
			uint8 heights[4];
			ComputeCornerHeight(ExpandTileSlope(v->GetGroundSlope()), first->base + i, heights);
			first_south = heights[TC_SOUTH];
			first_west  = heights[TC_WEST];
			break;
		}
	}

	uint8 second_north = 0;
	uint8 second_east  = 0;
	if (second != nullptr) {
		for (uint i = 0; i < second->height; i++) {
			const Voxel *v = &second->voxels[i];
			if (v->GetGroundType() == GTP_INVALID) continue;
			uint8 heights[4];
			ComputeCornerHeight(ExpandTileSlope(v->GetGroundSlope()), second->base + i, heights);
			second_north = heights[TC_NORTH];
			second_east  = heights[TC_EAST];
			break;
		}
	}

	SetFoundations(first,  first_south, first_west, second_east, second_north, 0x10, 0x20);
	SetFoundations(second, second_north, second_east, first_west, first_south, 0x01, 0x02);
}

/**
 * Update the foundations in two #_additions voxel stacks along the SE edge of the first stack and the NW edge of the second stack.
 * @param xpos X position of the first voxel stack.
 * @param ypos Y position of the first voxel stack.
 * @note The first or the second voxel stack may be off-world.
 */
static void SetYFoundations(int xpos, int ypos)
{
	VoxelStack *first = (ypos < 0) ? nullptr : _additions.GetModifyStack(xpos, ypos);
	VoxelStack *second = (ypos + 1 == _world.GetYSize()) ? nullptr : _additions.GetModifyStack(xpos, ypos + 1);
	assert(first != nullptr || second != nullptr);

	/* Get ground height at all corners. */
	uint8 first_south = 0;
	uint8 first_east  = 0;
	if (first != nullptr) {
		for (uint i = 0; i < first->height; i++) {
			const Voxel *v = &first->voxels[i];
			if (v->GetGroundType() == GTP_INVALID) continue;
			uint8 heights[4];
			ComputeCornerHeight(ExpandTileSlope(v->GetGroundSlope()), first->base + i, heights);
			first_south = heights[TC_SOUTH];
			first_east  = heights[TC_EAST];
			break;
		}
	}

	uint8 second_north = 0;
	uint8 second_west  = 0;
	if (second != nullptr) {
		for (uint i = 0; i < second->height; i++) {
			const Voxel *v = &second->voxels[i];
			if (v->GetGroundType() == GTP_INVALID) continue;
			uint8 heights[4];
			ComputeCornerHeight(ExpandTileSlope(v->GetGroundSlope()), second->base + i, heights);
			second_north = heights[TC_NORTH];
			second_west  = heights[TC_WEST];
			break;
		}
	}

	SetFoundations(first,  first_south, first_east, second_west, second_north, 0x08, 0x04);
	SetFoundations(second, second_north, second_west, first_east, first_south, 0x80, 0x40);
}

/**
 * Perform the proposed changes in #_additions (and committing them afterwards).
 * @param direction Direction of change.
 * @return Whether the change could actually be performed (else nothing is changed).
 */
bool TerrainChanges::ModifyWorld(int direction)
{
	assert(_mouse_modes.GetMouseMode() == MM_TILE_TERRAFORM);

	_additions.Clear();

	/* First iteration: Change the ground of the tiles, checking
	 * whether the change is actually allowed with the other game elements. */
	for (auto &iter : this->changes) {
		const Point32 &pos = iter.first;
		const GroundData &gd = iter.second;
		if (gd.modified == 0) continue;

		uint8 current[4]; // Height of each corner after applying modification.
		ComputeCornerHeight(static_cast<TileSlope>(gd.orig_slope), gd.height, current);

		/* Apply modification. */
		for (uint8 i = TC_NORTH; i < TC_END; i++) {
			if ((gd.modified & (1 << i)) == 0) continue; // Corner was not changed.
			current[i] += direction;
		}

		/* Clear the current ground from the stack. */
		VoxelStack *vs = _additions.GetModifyStack(pos.x, pos.y);
		Voxel *v = vs->GetCreate(gd.height, false); // Should always exist.
		GroundType gt = v->GetGroundType();
		assert(gt != GTP_INVALID);
		FoundationType ft = v->GetFoundationType();
		FenceType fence[4];
 		for (uint8 i = 0; i < 4; i++) fence[i] = v->GetFenceType((TileEdge)i);
		TileSlope slope = ExpandTileSlope(v->GetGroundSlope());
		v->SetGroundType(GTP_INVALID);
		v->SetFoundationType(FDT_INVALID);
		v->SetGroundSlope(0);
		v->SetFoundationSlope(0);
		for (uint8 i = 0; i < 4; i++) v->SetFenceType((TileEdge)i, FENCE_TYPE_INVALID);
		if (MayHaveGroundFenceInVoxelAbove(slope)) {
			Voxel *w = vs->GetCreate(gd.height + 1, false);
			if ((slope & TSB_STEEP) != 0) {
				assert(w->GetGroundType() == gt); // Should be the same type of ground as the base voxel.
				w->SetFoundationType(FDT_INVALID);
				w->SetGroundType(GTP_INVALID);
				w->SetGroundSlope(0);
				w->SetFoundationSlope(0);
			}
			if (w != nullptr) { // w will always exist if steep slope, but not always for non-steep.
				for (uint8 i = 0; i < 4; i++) {
					FenceType f = w->GetFenceType((TileEdge)i);
					if (f != FENCE_TYPE_INVALID) fence[i] = f;
					w->SetFenceType((TileEdge)i, FENCE_TYPE_INVALID);
				}
			}
		}

		if (direction > 0) {
			/* Moving upwards, compute upper bound on corner heights. */
			uint8 max_above[4];
			std::fill_n(max_above, lengthof(max_above), gd.height + 3);

			for (int i = 2; i >= 0; i--) {
				SetUpperBoundary(vs->Get(gd.height + i), gd.height + i, max_above);
			}

			/* Check boundaries. */
			for (uint i = 0; i < 4; i++) {
				if (current[i] > max_above[i]) return false;
			}
		} /* else: Moving downwards always works, since there is nothing underground yet. */

		/* Add new ground to the stack. */
		TileSlope new_slope;
		uint8 height;
		ComputeSlopeAndHeight(current, &new_slope, &height);
		TileSlope new_exp_slope = ExpandTileSlope(new_slope);
		if (height >= WORLD_Z_SIZE) return false;

		v = vs->GetCreate(height, true);
		v->SetGroundSlope(new_slope);
		v->SetGroundType(gt);
		v->SetFoundationType(ft);
		v->SetFoundationSlope(0);

		for (uint8 i = 0; i < 4; i++) {
			v->SetFenceType((TileEdge)i, StoreFenceInUpperVoxel(new_exp_slope, (TileEdge)i) ? FENCE_TYPE_INVALID : fence[i]);
		}

		if (MayHaveGroundFenceInVoxelAbove(new_exp_slope)) {
			v = vs->GetCreate(height + 1, true);
			if ((TSB_STEEP & new_exp_slope) != 0) {
				/* Only for steep slopes, the upper voxel will have actual ground. */
				v->SetGroundType(gt);
				v->SetGroundSlope(new_slope + 4); // Set top-part as well for steep slopes.
				v->SetFoundationType(ft);
				v->SetFoundationSlope(0);
			}
			for (uint8 i = 0; i < 4; i++) {
				v->SetFenceType((TileEdge)i, StoreFenceInUpperVoxel(new_exp_slope, (TileEdge)i) ? fence[i] : FENCE_TYPE_INVALID);
			}
		}
	}

	/* Second iteration: Add foundations to every changed tile edge.
	 * The general idea is that each modified voxel handles adding
	 * of foundation to its SE and SW edge. If the NE or NW voxel is not
	 * modified, the voxel will have to perform adding of foundations
	 * there as well. */
	for (auto &iter : this->changes) {
		const Point32 &pos = iter.first;
		const GroundData &gd = iter.second;
		if (gd.modified == 0) continue;

		SetXFoundations(pos.x, pos.y);
		SetYFoundations(pos.x, pos.y);

		Point32 pt(pos.x - 1, pos.y);
		auto iter2 = this->changes.find(pt);
		if (iter2 == this->changes.end()) {
			SetXFoundations(pt.x, pt.y);
		} else {
			const GroundData &gd = iter2->second;
			if (gd.modified == 0) SetXFoundations(pt.x, pt.y);
		}

		pt.x = pos.x;
		pt.y = pos.y - 1;
		iter2 = this->changes.find(pt);
		if (iter2 == this->changes.end()) {
			SetYFoundations(pt.x, pt.y);
		} else {
			const GroundData &gd = (*iter2).second;
			if (gd.modified == 0) SetYFoundations(pt.x, pt.y);
		}
	}

	_additions.Commit();
	return true;
}

TileTerraformMouseMode::TileTerraformMouseMode() : MouseMode(WC_NONE, MM_TILE_TERRAFORM)
{
	this->state = TFS_OFF;
	this->mouse_state = 0;
	this->xsize = 1;
	this->ysize = 1;
	this->pixel_counter = 0;
	this->levelling = true;
}

/** Terraform GUI window just opened. */
void TileTerraformMouseMode::OpenWindow()
{
	if (this->state == TFS_OFF) {
		this->state = TFS_NO_MOUSE;
		_mouse_modes.SetMouseMode(this->mode);
	}
}

/** Terraform GUI window just closed. */
void TileTerraformMouseMode::CloseWindow()
{
	if (this->state == TFS_ON) {
		this->state = TFS_OFF; // Prevent enabling again.
		_mouse_modes.SetViewportMousemode();
	}
	this->state = TFS_OFF; // In case the mouse mode was not active.
}

/**
 * Update the size of the terraform area.
 * @param xsize Horizontal length of the area. May be \c 0.
 * @param ysize Vertical length of the area. May be \c 0.
 */
void TileTerraformMouseMode::SetSize(int xsize, int ysize)
{
	this->xsize = xsize;
	this->ysize = ysize;
	if (this->state != TFS_ON) return;
	this->SetCursors();
}

/**
 * Set the 'levelling' mode. This has no further visible effect until a raise/lower action is performed.
 * @param levelling The new levelling setting.
 */
void TileTerraformMouseMode::Setlevelling(bool levelling)
{
	this->levelling = levelling;
}

bool TileTerraformMouseMode::MayActivateMode()
{
	return this->state != TFS_OFF;
}

void TileTerraformMouseMode::ActivateMode(const Point16 &pos)
{
	this->mouse_state = 0;
	this->state = TFS_ON;
	this->SetCursors();
}

/** Set/modify the cursors of the viewport. */
void TileTerraformMouseMode::SetCursors()
{
	Viewport *vp = GetViewport();
	if (vp == nullptr) return;

	bool single = this->xsize <= 1 && this->ysize <= 1;
	FinderData fdata(CS_GROUND, single ? FW_CORNER : FW_TILE);
	if (vp->ComputeCursorPosition(&fdata) != CS_NONE) {
		if (single) {
			vp->tile_cursor.SetCursor(fdata.voxel_pos, fdata.cursor);
			vp->area_cursor.SetInvalid();
		} else {
			Rectangle32 rect(fdata.voxel_pos.x - this->xsize / 2, fdata.voxel_pos.y - this->ysize / 2, this->xsize, this->ysize);
			vp->tile_cursor.SetInvalid();
			vp->area_cursor.SetCursor(rect, CUR_TYPE_TILE);
		}
	}
}

void TileTerraformMouseMode::LeaveMode()
{
	Viewport *vp = GetViewport();
	if (vp != nullptr) {
		vp->tile_cursor.SetInvalid();
		vp->area_cursor.SetInvalid();
	}
	if (this->state == TFS_ON) this->state = TFS_NO_MOUSE;
}

bool TileTerraformMouseMode::EnableCursors()
{
	return this->state != TFS_OFF;
}

void TileTerraformMouseMode::OnMouseButtonEvent(Viewport *vp, uint8 state)
{
	this->mouse_state = state & MB_CURRENT;
}

/**
 * Change the terrain while in 'dot' mode (i.e. a single corner or a single tile changing the entire world).
 * @param vp %Viewport displaying the world.
 * @param levelling If \c true, use levelling mode (only change the lowest/highest corners of a tile), else move every corner.
 * @param direction Direction of change.
 * @param dot_mode Using dot-mode (infinite world changes).
 */
static void ChangeTileCursorMode(Viewport *vp, bool levelling, int direction, bool dot_mode)
{
	Cursor *c = &vp->tile_cursor;
	if (_game_mode_mgr.InPlayMode() && _world.GetTileOwner(c->cursor_pos.x, c->cursor_pos.y) != OWN_PARK) return;

	Point32 p;
	uint16 w, h;

	if (dot_mode) { // Change entire world.
		p = {0, 0};
		w = _world.GetXSize();
		h = _world.GetYSize();
	} else { // Single tile mode.
		p = {c->cursor_pos.x, c->cursor_pos.y};
		w = 1;
		h = 1;
	}
	TerrainChanges changes(p, w, h);

	p = {c->cursor_pos.x, c->cursor_pos.y};

	bool ok;
	switch (c->type) {
		case CUR_TYPE_NORTH:
		case CUR_TYPE_EAST:
		case CUR_TYPE_SOUTH:
		case CUR_TYPE_WEST:
			ok = changes.ChangeCorner(p, (TileCorner)(c->type - CUR_TYPE_NORTH), direction);
			break;

		case CUR_TYPE_TILE: {
			uint8 height = (direction > 0) ? WORLD_Z_SIZE : 0;
			if (levelling) changes.UpdatelevellingHeight(p, direction, &height);
			ok = changes.ChangeVoxel(p, height, direction);
			break;
		}

		default:
			NOT_REACHED();
	}

	if (ok) {
		ok = changes.ModifyWorld(direction);
		_additions.Clear();
		if (!ok) return;

		/* Move voxel selection along with the terrain to allow another mousewheel event at the same place.
		 * Note that the mouse cursor position is not changed at all, it still points at the original position.
		 * The coupling is restored with the next mouse movement.
		 */
		c->cursor_pos.z = _world.GetGroundHeight(c->cursor_pos.x, c->cursor_pos.y);
		for (const auto &iter : changes.changes) {
			const Point32 &pt = iter.first;
			vp->MarkVoxelDirty(XYZPoint16(pt.x, pt.y, iter.second.height));
		}
	}
}

/**
 * Change the terrain while in 'area' mode (i.e. a rectangle of tiles that changes).
 * @param vp %Viewport displaying the world.
 * @param levelling If \c true, use levelling mode (only change the lowest/highest corners of a tile), else move every corner.
 * @param direction Direction of change.
 */
static void ChangeAreaCursorMode(Viewport *vp, bool levelling, int direction)
{
	Point32 p;

	MultiCursor *c = &vp->area_cursor;
	TerrainChanges changes(c->rect.base, c->rect.width, c->rect.height);

	uint8 height = (direction > 0) ? WORLD_Z_SIZE : 0;
	if (levelling) {
		for (uint dx = 0; dx < c->rect.width; dx++) {
			p.x = c->rect.base.x + dx;
			for (uint dy = 0; dy < c->rect.height; dy++) {
				p.y = c->rect.base.y + dy;
				if (_game_mode_mgr.InEditorMode() || _world.GetTileOwner(p.x, p.y) == OWN_PARK) {
					changes.UpdatelevellingHeight(p, direction, &height);
				}
			}
		}
	}

	/* Make the change in 'changes'. */
	for (uint dx = 0; dx < c->rect.width; dx++) {
		p.x = c->rect.base.x + dx;
		for (uint dy = 0; dy < c->rect.height; dy++) {
			p.y = c->rect.base.y + dy;
			if (_game_mode_mgr.InEditorMode() || _world.GetTileOwner(p.x, p.y) == OWN_PARK) {
				if (!changes.ChangeVoxel(p, height, direction)) return;
			}
		}
	}

	bool ok = changes.ModifyWorld(direction);
	_additions.Clear();
	if (!ok) return;

	/* Like the dotmode, the cursor position is changed, but the mouse position is not touched to allow more
	 * mousewheel events to happen at the same place. */
	for (const auto &iter : changes.changes) {
		const Point32 &pt = iter.first;
		c->ResetZPosition(pt);
		vp->MarkVoxelDirty(XYZPoint16(pt.x, pt.y, iter.second.height));
	}
}

void TileTerraformMouseMode::OnMouseWheelEvent(Viewport *vp, int direction)
{
	if (vp->tile_cursor.type != CUR_TYPE_INVALID) {
		ChangeTileCursorMode(vp, this->levelling, direction, this->xsize == 0 && this->ysize == 0);
	} else {
		ChangeAreaCursorMode(vp, this->levelling, direction);
	}
}

void TileTerraformMouseMode::OnMouseMoveEvent(Viewport *vp, const Point16 &old_pos, const Point16 &pos)
{
	if ((this->mouse_state & MB_RIGHT) != 0) {
		/* Drag the window if button is pressed down. */
		vp->MoveViewport(pos.x - old_pos.x, pos.y - old_pos.y);

	} else if ((this->mouse_state & MB_LEFT) != 0) {
		/* Terraforming with holding left mouse button and move mouse up and down. */

		if (pos.y - old_pos.y > 0) {
			this->pixel_counter--;
		} else if (pos.y - old_pos.y < 0) {
			this->pixel_counter++;
		} else {
			return;
		}

		if (this->pixel_counter == 10 || this->pixel_counter == -10) {
			if (vp->tile_cursor.type != CUR_TYPE_INVALID) {
				bool dot_mode = this->xsize == 0 && this->ysize == 0;
				ChangeTileCursorMode(vp, this->levelling, pixel_counter / 10, dot_mode);
			} else {
				ChangeAreaCursorMode(vp, this->levelling, pixel_counter / 10);
			}
			this->pixel_counter = 0;
		}

	} else {
		this->pixel_counter = 0; // Reset counter when LMB is not pressed.
		this->SetCursors();
	}
}
